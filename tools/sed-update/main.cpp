// sed-update: adapted F-14 updater over the rewrite's release-package
// machinery. Feed fetch is injectable through sed::update::IUpdateSource;
// this binary wires the local-file source (tests use local feeds only).
// Exit codes: 0 ok, 2 usage/malformed, 3 version refused (equal/downgrade/
// none), 4 conflict (staging/target exists), 5 I/O or receipt failure.
#include "sed/update/updater.hpp"

#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace
{
using namespace sed::update;

std::string jsonString(std::string_view value)
{
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for ( const unsigned char c : value )
    {
        if ( c == '"' || c == '\\' )
        {
            result += '\\';
            result += static_cast<char>(c);
        }
        else if ( c < 0x20 )
        {
            result += "\\u00";
            result += hex[c >> 4];
            result += hex[c & 0x0f];
        }
        else
        {
            result += static_cast<char>(c);
        }
    }
    result += '"';
    return result;
}

int codeFor(const UpdateFailure& failure)
{
    if (failure.code == "equal-version" || failure.code == "downgrade-refused"
        || failure.code == "no-release")
        return 3;
    if (failure.code == "interrupted-staging" || failure.code == "already-activated")
        return 4;
    if (failure.code == "malformed-feed" || failure.code == "malformed-manifest"
        || failure.code == "malformed-version" || failure.code == "unsafe-path")
        return 2;
    return 5;
}

int report(const UpdateFailure& failure)
{
    std::cout << "{\"ok\":false,\"code\":" << jsonString(failure.code) << ",\"error\":"
              << jsonString(failure.message) << "}\n";
    return codeFor(failure);
}

std::optional<std::string> opt(const std::map<std::string, std::string>& args, const char* key)
{
    const auto it = args.find(key);
    return it == args.end() ? std::nullopt : std::optional<std::string>(it->second);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: sed-update <check|stage|activate|verify> [options]\n"
                     "  check    --feed <feed.json> --current <version> [--channel <name>]\n"
                     "  stage    --feed <feed.json> --install <dir> --current <version> [--channel <name>]\n"
                     "  activate --staging <dir> [--target <dir>]\n"
                     "  verify   --dir <dir> [--staged]\n"
                     "The update feed is not independently signed; see docs/port/UPDATER.md.\n";
        return 2;
    }
    const std::string command = argv[1];
    std::map<std::string, std::string> args;
    for (int i = 2; i + 1 < argc; i += 2)
    {
        std::string key = argv[i];
        if (key.rfind("--", 0) != 0) { std::cerr << "expected --option\n"; return 2; }
        args[key.substr(2)] = argv[i + 1];
    }
    // Flag without a value (e.g. --staged) leaves a dangling final token.
    if ((argc - 2) % 2 == 1 && argc > 2)
    {
        const std::string last = argv[argc - 1];
        if (last.rfind("--", 0) == 0) args[last.substr(2)] = "true";
    }
    UpdateFailure failure;
    const std::string channel = opt(args, "channel").value_or("stable");

    if (command == "check" || command == "stage")
    {
        const auto feed = opt(args, "feed");
        const auto current = opt(args, "current");
        if (!feed || !current) { std::cerr << command << " requires --feed and --current\n"; return 2; }
        FileUpdateSource source(*feed);
        if (command == "check")
        {
            const auto text = source.fetchFeedText(&failure);
            if (!text) return report(failure);
            const auto parsed = parseFeed(*text, &failure);
            if (!parsed) return report(failure);
            const auto release = selectUpdate(*parsed, *current, channel, &failure);
            if (!release) return report(failure);
            std::cout << "{\"ok\":true,\"status\":\"update-available\",\"version\":"
                      << jsonString(release->version) << "}\n";
            return 0;
        }
        const auto install = opt(args, "install");
        if (!install) { std::cerr << "stage requires --install\n"; return 2; }
        const auto staged = stageUpdate(source, *install, *current, channel, &failure);
        if (!staged) return report(failure);
        std::cout << "{\"ok\":true,\"status\":\"staged\",\"version\":" << jsonString(staged->version)
                  << ",\"staging\":" << jsonString(staged->stagingDir.string()) << "}\n";
        return 0;
    }
    if (command == "activate")
    {
        const auto staging = opt(args, "staging");
        if (!staging) { std::cerr << "activate requires --staging\n"; return 2; }
        std::optional<std::filesystem::path> target;
        if (const auto value = opt(args, "target")) target = *value;
        const auto activated = activateUpdate(*staging, target, &failure);
        if (!activated) return report(failure);
        std::cout << "{\"ok\":true,\"status\":\"activated\",\"dir\":" << jsonString(activated->string()) << "}\n";
        return 0;
    }
    if (command == "verify")
    {
        const auto dir = opt(args, "dir");
        if (!dir) { std::cerr << "verify requires --dir\n"; return 2; }
        if (!verifyDirectory(*dir, args.count("staged") > 0, &failure)) return report(failure);
        std::cout << "{\"ok\":true,\"status\":\"verified\"}\n";
        return 0;
    }
    std::cerr << "unknown command: " << command << "\n";
    return 2;
}
