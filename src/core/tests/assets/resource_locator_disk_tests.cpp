#include "test_common.hpp"
#include "resource_locator_test_fixtures.hpp"

#include "sed/assets/episode.hpp"
#include "sed/assets/jlh.hpp"
#include "sed/assets/resource_locator.hpp"
#include "sed/assets/tpl.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace sed::tests
{

void runResourceLocatorDiskTests()
{
    const auto unique                      = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path root       = "tmp_res_root_" + unique;
    const std::filesystem::path file       = root / "mat" / "test.mat";
    const std::filesystem::path doubleFile = root / "mat" / "double.mat.mat";

    {
        // Resource names are UTF-8. Exercise a real non-ASCII leaf and a
        // separately occupied ANSI-decoded spelling on Windows, not just a
        // Unicode parent containing an ASCII file.
        ResourceLocatorFixture fixture;
        const auto nativeRoot = fixture.root / std::filesystem::path(u8"模型-É");
        const auto leaf = std::filesystem::path(u8"源-É.3do");
        const auto native = nativeRoot / "3do" / leaf;
        std::filesystem::create_directories(native.parent_path());
        const std::string expected = "native-unicode-model";
        {
            std::ofstream output(native, std::ios::binary);
            output << expected;
            check(static_cast<bool>(output), "ResourceLocator.disk.unicode creates native leaf");
        }
        const auto utf8 = [](const std::filesystem::path& value)
        {
            const auto text = value.u8string();
            return std::string(text.begin(), text.end());
        };
        const std::string relative = utf8(leaf);
        const auto wronglyDecoded = native.parent_path() / std::filesystem::path(relative);
        if ( wronglyDecoded != native )
        {
            std::ofstream wrong(wronglyDecoded, std::ios::binary);
            wrong << "wrong-file";
            check(static_cast<bool>(wrong), "ResourceLocator.disk.unicode creates occupied ANSI spelling");
        }
        assets::ResourceLocator locator;
        locator.addRoot(nativeRoot);
        for ( const auto& name : {relative, std::string("3do/") + relative, utf8(native)} )
        {
            check(locator.findOnDisk(name) == native && locator.findSelectedOnDisk(name) == native,
                "ResourceLocator.disk.unicode metadata identifies exact native entry");
            const auto unbounded = locator.tryReadBinary(name);
            std::filesystem::path supplied;
            const auto bounded = locator.tryReadBinary(name, expected.size(), &supplied);
            const auto matches = [&](const auto& data)
            {
                return data && std::string(reinterpret_cast<const char*>(data->data()), data->size()) == expected;
            };
            check(matches(unbounded) && matches(bounded) && supplied == native,
                "ResourceLocator.disk.unicode reads exact UTF-8 winner through both APIs");
            check(!locator.tryReadBinary(name, expected.size() - 1, &supplied) && supplied.empty(),
                "ResourceLocator.disk.unicode keeps byte ceiling and refusal provenance");
        }
        std::filesystem::remove(native);
        check(!locator.tryReadBinary(relative) && !locator.findOnDisk(relative),
            "ResourceLocator.disk.unicode never falls back to the occupied ANSI spelling");
    }

    {
        // Archive keys are opaque bytes. The loose roots are probed before
        // this entry, even when its legacy spelling cannot be decoded as UTF-8.
        ResourceLocatorFixture fixture;
        const std::string leaf = "caf\xe9.3do";
        const auto packed = assets::buildGobBytes({{"3do/" + leaf, "legacy"}});
        {
            std::ofstream archive(fixture.firstGob, std::ios::binary | std::ios::trunc);
            archive.write(packed.data(), static_cast<std::streamsize>(packed.size()));
            check(static_cast<bool>(archive), "ResourceLocator.disk.legacy writes byte-named GOB entry");
        }
        assets::ResourceLocator locator;
        locator.addRoot(fixture.root);
        locator.mountGob(fixture.firstGob);
        locator.setPrimaryResourceOverride("other.3do", [] { return std::vector<std::byte>{}; });
        std::filesystem::path source = fixture.firstGob;
        check(!locator.findOnDisk(leaf) && !locator.findSelectedOnDisk(leaf)
                && locator.tryReadText(leaf) == "legacy",
            "ResourceLocator.disk.legacy preserves raw byte lookup through candidate and primary checks");
        const auto bounded = locator.tryReadBinary(leaf, 6u, &source);
        check(bounded && std::string(reinterpret_cast<const char*>(bounded->data()), bounded->size()) == "legacy"
                && source.empty(),
            "ResourceLocator.disk.legacy bounded read returns the original archive entry");
        bool exceeded = false;
        check(!locator.tryReadBinary(leaf, 5u, exceeded) && exceeded,
            "ResourceLocator.disk.legacy preserves the archive winner's byte ceiling");

        // If the platform previously admitted this spelling as a native loose
        // file, it must still take priority over its matching GOB entry.
        std::optional<std::filesystem::path> legacyLoose;
        try
        {
            legacyLoose = fixture.root / std::filesystem::path(leaf);
        }
        catch ( const std::system_error& )
        {
            // Some Windows filesystem code pages cannot represent this byte.
        }
        if ( legacyLoose )
        {
            {
                std::ofstream out(*legacyLoose, std::ios::binary);
                out << "disk";
                check(static_cast<bool>(out), "ResourceLocator.disk.legacy creates native loose spelling");
            }
            check(locator.findSelectedOnDisk(leaf) == legacyLoose && locator.tryReadText(leaf) == "disk",
                "ResourceLocator.disk.legacy keeps a representable non-UTF-8 loose winner ahead of its GOB");
        }
    }

    {
        sed::assets::ResourceLocator locator{};
        locator.addRoot({});
        check(locator.roots().empty(),
            "ResourceLocator.disk.roots ignores an empty root");

        locator.addRoot("first");
        locator.addRoot("second");
        check(locator.roots()
                == std::vector<std::filesystem::path>{ "first", "second" },
            "ResourceLocator.disk.roots preserves configured source order");
        locator.clearRoots();
        check(locator.roots().empty(),
            "ResourceLocator.disk.roots clear removes every configured root");
    }

    {
        sed::assets::ResourceLocator original;
        const auto emptyIdentity = original.previewCacheIdentity();
        original.addRoot({});
        original.clearRoots();
        original.clearGobs();
        original.reloadGobs();
        check(original.previewCacheIdentity() == emptyIdentity,
            "ResourceLocator cache identity is stable for configuration no-ops");
        original.addRoot("first");
        const auto firstIdentity = original.previewCacheIdentity();
        sed::assets::ResourceLocator copy = original;
        check(firstIdentity != emptyIdentity && copy.previewCacheIdentity() == firstIdentity,
            "ResourceLocator immutable copies retain their source configuration identity");
        original.addRoot("second");
        check(original.previewCacheIdentity() != firstIdentity
                && copy.previewCacheIdentity() == firstIdentity
                && copy.roots() == std::vector<std::filesystem::path>{"first"},
            "ResourceLocator successful mutation updates identity without changing retained copies");
        const auto beforeFailure = original.previewCacheIdentity();
        bool failed = false;
        try { original.mountGob(root / "absent-cache-identity.gob"); }
        catch ( const std::exception& ) { failed = true; }
        check(failed && original.previewCacheIdentity() == beforeFailure
                && original.gobs().empty(),
            "ResourceLocator failed archive mount preserves configuration identity");
        copy = original;
        check(copy.previewCacheIdentity() == original.previewCacheIdentity()
                && copy.roots() == original.roots(),
            "ResourceLocator assignment publishes configuration and hint identity together");
        auto movedFrom = copy;
        const auto transferredIdentity = movedFrom.previewCacheIdentity();
        auto movedTo = std::move(movedFrom);
        check(movedTo.previewCacheIdentity() == transferredIdentity
                && movedTo.roots() == copy.roots() && movedFrom.roots().empty()
                && movedFrom.gobs().empty() && movedFrom.previewCacheIdentity() == emptyIdentity,
            "ResourceLocator move transfers the hint identity and resets the empty source identity");
        movedFrom = std::move(movedTo);
        check(movedFrom.previewCacheIdentity() == transferredIdentity
                && movedFrom.roots() == copy.roots() && movedTo.roots().empty()
                && movedTo.gobs().empty() && movedTo.previewCacheIdentity() == emptyIdentity,
            "ResourceLocator move assignment preserves source and destination identity coherence");
        original.clearRoots();
        check(original.previewCacheIdentity() != beforeFailure && original.roots().empty()
                && copy.previewCacheIdentity() == beforeFailure && copy.roots().size() == 2,
            "ResourceLocator clearing invalidates only that mutable source's hints");
    }

    {
        sed::assets::ResourceLocator locator{};
        struct CandidateCase
        {
            std::string_view name;
            std::vector<std::string> expected;
        };
        const CandidateCase candidateCases[]{
            {
                "  folder\\effect.WAV  ",
                {
                    "folder/effect.WAV",
                    "sound/effect.WAV",
                    "voice/effect.WAV",
                    "voiceuu/effect.WAV",
                },
            },
            {
                "material.mat",
                {
                    "material.mat",
                    "mat/material.mat",
                    "3do/mat/material.mat",
                },
            },
            {
                "object.3DO",
                {
                    "object.3DO",
                    "3do/object.3DO",
                    "hi3do/object.3DO",
                },
            },
            { "items.dat", { "items.dat", "misc/items.dat" } },
            { "walk.key", { "walk.key", "3do/key/walk.key" } },
            { "actor.ai", { "actor.ai", "misc/ai/actor.ai" } },
            { "actor.ai0", { "actor.ai0", "misc/ai/actor.ai0" } },
            { "actor.ai2", { "actor.ai2", "misc/ai/actor.ai2" } },
            { "level.cmp", { "level.cmp", "misc/cmp/level.cmp" } },
            { "dust.par", { "dust.par", "misc/par/dust.par" } },
            { "rules.per", { "rules.per", "misc/per/rules.per" } },
            { "actor.pup", { "actor.pup", "misc/pup/actor.pup" } },
            { "actor.snd", { "actor.snd", "misc/snd/actor.snd" } },
            { "actor.spr", { "actor.spr", "misc/spr/actor.spr" } },
            { "startup.cog", { "startup.cog", "cog/startup.cog" } },
            { "master.tpl", { "master.tpl", "misc/master.tpl" } },
            {
                "cogstrings.uni",
                {
                    "cogstrings.uni",
                    "misc/cogstrings.uni",
                    "ui/cogstrings.uni",
                },
            },
            {
                "mat/already.mat",
                { "mat/already.mat", "3do/mat/already.mat" },
            },
            {
                "3do/mat/already.mat",
                { "3do/mat/already.mat", "mat/already.mat" },
            },
            {
                "sound/already.wav",
                {
                    "sound/already.wav",
                    "voice/already.wav",
                    "voiceuu/already.wav",
                },
            },
            {
                "3do/already.3do",
                { "3do/already.3do", "hi3do/already.3do" },
            },
            { "misc/cmp/already.cmp", { "misc/cmp/already.cmp" } },
            { "folder/unknown.bin", { "folder/unknown.bin" } },
        };
        bool allCandidatesMatch = true;
        for ( const CandidateCase& candidateCase : candidateCases )
        {
            allCandidatesMatch = allCandidatesMatch
                && locator.candidates(candidateCase.name)
                    == candidateCase.expected;
        }
        check(allCandidatesMatch,
            "ResourceLocator.disk.candidates preserves every kind folder, order, "
            "case, and rooted-path duplicate suppression");
        check(locator.candidates(" \t\r\n ").empty(),
            "ResourceLocator.disk.candidates rejects blank names");
    }

    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    check(!ec, "ResourceLocator.disk.create_directories");

    {
        std::ofstream out(file, std::ios::binary);
        if (!out)
        {
            check(false, "ResourceLocator.disk.create_file");
        }
        else
        {
            out.write("DISK", 4);
        }
    }

    {
        std::ofstream out(doubleFile, std::ios::binary);
        if (!out)
        {
            check(false, "ResourceLocator.disk.create_double_ext_file");
        }
        else
        {
            out.write("DOUBLE", 6);
        }
    }

    try
    {
        sed::assets::ResourceLocator locator{};
        locator.addRoot(root);

        auto bytesToString = [](const std::vector<std::byte>& bytes)
        {
            std::string out{};
            out.reserve(bytes.size());
            for (const std::byte b : bytes)
            {
                out.push_back(static_cast<char>(std::to_integer<unsigned char>(b)));
            }
            return out;
        };

        const auto bytes = locator.readBinary("test.mat");
        const std::string got = bytesToString(bytes);
        check(got == "DISK", "ResourceLocator.disk.readBinary");
        check(locator.readText("test.mat") == "DISK", "ResourceLocator.disk.readText");
        check(locator.readText("  mat\\test.mat  ") == "DISK",
            "ResourceLocator.disk.readText normalizes a trimmed backslash path");
        const auto found = locator.findOnDisk(" test.mat ");
        check(found.has_value() && *found == file,
            "ResourceLocator.disk.findOnDisk returns the exact path selected by candidate resolution");

        {
            const auto bytesOpt = locator.tryReadBinaryWithExt("test", ".mat");
            check(bytesOpt.has_value(), "ResourceLocator.disk.tryReadBinaryWithExt.append.has_value");
            check(bytesOpt ? (bytesToString(*bytesOpt) == "DISK") : false, "ResourceLocator.disk.tryReadBinaryWithExt.append.content");
        }
        check(locator.tryReadTextWithExt("test", ".mat").value_or("") == "DISK", "ResourceLocator.disk.tryReadTextWithExt.append");

        {
            const auto bytesOpt = locator.tryReadBinaryWithExt("test.mat", ".mat");
            check(bytesOpt.has_value(), "ResourceLocator.disk.tryReadBinaryWithExt.with_ext.has_value");
            check(bytesOpt ? (bytesToString(*bytesOpt) == "DISK") : false, "ResourceLocator.disk.tryReadBinaryWithExt.with_ext.content");
        }
        check(locator.tryReadTextWithExt("test.mat", ".mat").value_or("") == "DISK", "ResourceLocator.disk.tryReadTextWithExt.with_ext");

        check(!locator.tryReadBinaryWithExt("double.mat", ".mat").has_value(), "ResourceLocator.disk.tryReadBinaryWithExt.no_double_ext");
        check(!locator.tryReadTextWithExt("double.mat", ".mat").has_value(), "ResourceLocator.disk.tryReadTextWithExt.no_double_ext");
        check(!locator.tryReadBinary("missing.mat").has_value()
                && !locator.tryReadText("missing.mat").has_value()
                && !locator.findOnDisk("missing.mat").has_value(),
            "ResourceLocator.disk unavailable try/find operations remain non-throwing");
        checkThrowsWithMessage(
            [&locator]
            {
                (void)locator.readBinary("missing.mat");
            },
            "Resource not found: missing.mat",
            "ResourceLocator.disk readBinary reports the requested unavailable resource");
        checkThrowsWithMessage(
            [&locator]
            {
                (void)locator.readText("missing.mat");
            },
            "Resource not found: missing.mat",
            "ResourceLocator.disk readText reports the requested unavailable resource");

        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write("CHANGED", 7);
        }
        check(bytesToString(bytes) == "DISK"
                && locator.readText("test.mat") == "CHANGED",
            "ResourceLocator.disk rereads a same-path external replacement while a previously returned byte snapshot remains stable");

        std::filesystem::remove(file, ec);
        check(!ec
                && !locator.tryReadText("test.mat").has_value()
                && bytesToString(bytes) == "DISK",
            "ResourceLocator.disk observes external removal without invalidating an existing caller-owned byte snapshot");
    }
    catch (const std::exception& e)
    {
        check(false, e.what());
    }

    std::filesystem::remove_all(root, ec);

    {
        const std::filesystem::path root1 = "tmp_res_root1_" + unique;
        const std::filesystem::path root2 = "tmp_res_root2_" + unique;

        const std::filesystem::path file1 = root1 / "mat" / "test.mat";
        const std::filesystem::path file2 = root2 / "mat" / "test.mat";

        std::filesystem::create_directories(file1.parent_path(), ec);
        check(!ec, "ResourceLocator.disk.precedence.create_directories1");
        std::filesystem::create_directories(file2.parent_path(), ec);
        check(!ec, "ResourceLocator.disk.precedence.create_directories2");

        {
            std::ofstream out(file1, std::ios::binary);
            out.write("FIRST", 5);
        }
        {
            std::ofstream out(file2, std::ios::binary);
            out.write("SECOND", 6);
        }

        try
        {
            sed::assets::ResourceLocator locator{};
            locator.addRoot(root1);
            locator.addRoot(root2);

            check(locator.readText("test.mat") == "SECOND", "ResourceLocator.disk.precedence.last_root_wins");
            check(locator.tryReadTextWithExt("test", ".mat").value_or("")
                    == "SECOND",
                "ResourceLocator.disk.precedence.tryReadTextWithExt");
        }
        catch ( const std::exception& e )
        {
            check(false, e.what());
        }

        std::filesystem::remove_all(root1, ec);
        std::filesystem::remove_all(root2, ec);
    }

    {
        const std::filesystem::path firstRoot =
            "tmp_res_snapshot_first_" + unique;
        const std::filesystem::path secondRoot =
            "tmp_res_snapshot_second_" + unique;
        const std::filesystem::path firstFile =
            firstRoot / "mat" / "snapshot.mat";
        const std::filesystem::path secondFile =
            secondRoot / "mat" / "snapshot.mat";
        std::filesystem::create_directories(firstFile.parent_path(), ec);
        check(!ec,
            "ResourceLocator.disk.snapshot creates first root");
        std::filesystem::create_directories(secondFile.parent_path(), ec);
        check(!ec,
            "ResourceLocator.disk.snapshot creates second root");
        {
            std::ofstream out(firstFile, std::ios::binary);
            out.write("FIRST", 5);
        }
        {
            std::ofstream out(secondFile, std::ios::binary);
            out.write("SECOND", 6);
        }

        try
        {
            std::shared_ptr<const sed::assets::ResourceLocator>
                retainedSnapshot{};
            {
                sed::assets::ResourceLocator publisher{};
                publisher.addRoot(firstRoot);
                retainedSnapshot = std::make_shared<
                    const sed::assets::ResourceLocator>(publisher);

                publisher.clearRoots();
                publisher.addRoot(secondRoot);
                check(retainedSnapshot->roots()
                            == std::vector<std::filesystem::path>{ firstRoot }
                        && publisher.roots()
                            == std::vector<std::filesystem::path>{ secondRoot }
                        && retainedSnapshot->readText("snapshot.mat")
                            == "FIRST"
                        && publisher.readText("snapshot.mat") == "SECOND",
                    "ResourceLocator.disk.snapshot keeps an immutable root configuration while a new publisher configuration is installed");

                {
                    std::ofstream out(
                        firstFile,
                        std::ios::binary | std::ios::trunc);
                    out.write("FIRST-CHANGED", 13);
                }
                check(retainedSnapshot->readText("snapshot.mat")
                            == "FIRST-CHANGED"
                        && publisher.readText("snapshot.mat") == "SECOND",
                    "ResourceLocator.disk.snapshot retains source ownership but still observes an external file replacement in its own root");

                publisher.clearRoots();
                check(publisher.roots().empty()
                        && retainedSnapshot->roots()
                            == std::vector<std::filesystem::path>{ firstRoot }
                        && retainedSnapshot->readText("snapshot.mat")
                            == "FIRST-CHANGED",
                    "ResourceLocator.disk.snapshot outlives later publisher clearing without sharing mutable root vectors");
            }
            check(retainedSnapshot
                    && retainedSnapshot->roots()
                        == std::vector<std::filesystem::path>{ firstRoot }
                    && retainedSnapshot->readText("snapshot.mat")
                        == "FIRST-CHANGED",
                "ResourceLocator.disk.snapshot remains usable after the publishing locator is destroyed");
        }
        catch (const std::exception& e)
        {
            check(false, e.what());
        }

        std::filesystem::remove_all(firstRoot, ec);
        std::filesystem::remove_all(secondRoot, ec);
    }

    {
        const std::filesystem::path root1 = "tmp_res_exact1_" + unique;
        const std::filesystem::path root2 = "tmp_res_exact2_" + unique;
        const std::filesystem::path exact = root1 / "priority.mat";
        const std::filesystem::path laterFallback = root2 / "mat" / "priority.mat";

        std::filesystem::create_directories(exact.parent_path(), ec);
        check(!ec, "ResourceLocator.disk.candidate_precedence.create_directories1");
        std::filesystem::create_directories(laterFallback.parent_path(), ec);
        check(!ec, "ResourceLocator.disk.candidate_precedence.create_directories2");
        {
            std::ofstream out(exact, std::ios::binary);
            out.write("EXACT", 5);
        }
        {
            std::ofstream out(laterFallback, std::ios::binary);
            out.write("FALLBACK", 8);
        }

        try
        {
            sed::assets::ResourceLocator locator{};
            locator.addRoot(root1);
            locator.addRoot(root2);

            check(locator.readText("priority.mat") == "EXACT",
                "ResourceLocator.disk exact relative name outranks a later root's kind-folder fallback");
            const auto found = locator.findOnDisk("priority.mat");
            check(found.has_value() && *found == exact,
                "ResourceLocator.disk findOnDisk uses the same candidate-before-root precedence as reads");

            std::filesystem::remove(exact, ec);
            check(!ec && locator.readText("priority.mat") == "FALLBACK",
                "ResourceLocator.disk removal reveals the next deterministic candidate without rebuilding the locator");

            std::filesystem::create_directory(exact, ec);
            const auto foundAfterDirectoryReplacement =
                locator.findOnDisk("priority.mat");
            check(!ec
                    && locator.readText("priority.mat") == "FALLBACK"
                    && foundAfterDirectoryReplacement
                    && *foundAfterDirectoryReplacement == laterFallback,
                "ResourceLocator.disk a directory replacing a higher-priority file cannot shadow the next readable candidate");
        }
        catch ( const std::exception& e )
        {
            check(false, e.what());
        }

        std::filesystem::remove_all(root1, ec);
        std::filesystem::remove_all(root2, ec);
    }

    {
        const std::filesystem::path root1 = "tmp_res_graph_first_" + unique;
        const std::filesystem::path root2 = "tmp_res_graph_second_" + unique;
        auto writeTextFile = [](const std::filesystem::path& path,
                                 const std::string_view text)
        {
            std::error_code writeEc;
            std::filesystem::create_directories(path.parent_path(), writeEc);
            if ( writeEc )
            {
                return false;
            }

            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if ( !out )
            {
                return false;
            }
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            return static_cast<bool>(out);
        };

        const bool wroteGraph =
            writeTextFile(
                root1 / "master.tpl",
                "# DESC: Exact candidate template\n"
                "exact_actor none thingflags=0x10\n")
            && writeTextFile(
                root2 / "misc" / "master.tpl",
                "# DESC: Later-root fallback template\n"
                "fallback_actor none thingflags=0x20\n")
            && writeTextFile(
                root1 / "project_resources.jlh",
                "SUB First Resources\n"
                "LIST Actors First Actors\n"
                "SUBEND\n")
            && writeTextFile(
                root1 / "project_resources.jll",
                "[Actors]\n"
                "first_actor.3do\n")
            && writeTextFile(
                root2 / "project_resources.jlh",
                "SUB Selected Resources\n"
                "LIST Actors Selected Actors\n"
                "SUBEND\n")
            && writeTextFile(
                root2 / "project_resources.jll",
                "[Actors]\n"
                "selected_actor.3do\n")
            && writeTextFile(
                root1 / "episode.jk",
                "\"First Episode\"\n"
                "TYPE 1\n"
                "SEQ 1\n"
                "10: 0 1 LEVEL first.jkl 0 0 -1 -1\n"
                "END\n")
            && writeTextFile(
                root2 / "episode.jk",
                "\"Selected Episode\"\n"
                "TYPE 1\n"
                "SEQ 1\n"
                "10: 0 1 LEVEL selected.jkl 0 0 -1 -1\n"
                "END\n");
        check(wroteGraph,
            "ResourceLocator.disk.resource_graph writes deterministic loose fixtures");

        if ( wroteGraph )
        {
            try
            {
                sed::assets::ResourceLocator locator{};
                locator.addRoot(root1);
                locator.addRoot(root2);

                const sed::assets::Templates templates =
                    sed::assets::loadTemplates(locator, "master.tpl");
                const sed::assets::ThingTemplate* exactActor =
                    templates.find("EXACT_ACTOR");
                const sed::assets::JlhTree categories =
                    sed::assets::loadFromJLH("project_resources.jlh", locator);
                std::istringstream episodeInput(locator.readText("episode.jk"));
                const sed::assets::EpisodeFile episode =
                    sed::assets::loadFromEpisodeJk(episodeInput);

                check(exactActor
                        && exactActor->desc == "Exact candidate template"
                        && !templates.find("FALLBACK_ACTOR")
                        && categories.roots.size() == 1u
                        && categories.roots[0].label == "Selected Resources"
                        && categories.roots[0].children.size() == 1u
                        && categories.roots[0].children[0].list
                        && categories.roots[0].children[0].list->entries
                            == std::vector<std::string>{ "selected_actor.3do" }
                        && episode.name == "Selected Episode"
                        && episode.sequences.size() == 1u
                        && episode.sequences[0].fileName == "selected.jkl",
                    "ResourceLocator.disk.resource_graph feeds candidate-first and "
                    "last-root loose bytes through TPL, JLH/JLL, and episode parsers");
            }
            catch ( const std::exception& e )
            {
                check(false, e.what());
            }
        }

        std::filesystem::remove_all(root1, ec);
        std::filesystem::remove_all(root2, ec);
    }

    // Archive-only mode: names resolve against mounted archives alone, so a
    // name taken from a level can never open a file on disk.
    {
        const std::filesystem::path outside = std::filesystem::absolute(
            std::filesystem::temp_directory_path() / ("sed-archive-only-" + unique + ".snd"));
        {
            std::ofstream out(outside, std::ios::binary);
            out << "secret 1\n";
        }
        const std::string absoluteName = outside.generic_string();
        const ResourceLocatorFixture fixture;

        sed::assets::ResourceLocator open;
        check(open.tryReadBinary(absoluteName).has_value() && !open.archivesOnly(),
            "ResourceLocator.archive_only an ordinary locator reads an absolute name from disk");

        sed::assets::ResourceLocator locator;
        locator.addRoot(root);
        locator.mountGob(fixture.firstGob);
        const auto before = locator.previewCacheIdentity();
        locator.setArchivesOnly(true);
        check(locator.archivesOnly() && locator.roots().empty() && locator.previewCacheIdentity() != before,
            "ResourceLocator.archive_only clears loose-file roots and changes the configuration identity");
        bool exceeded = true;
        std::filesystem::path source = "unset";
        check(!locator.tryReadBinary(absoluteName).has_value()
                && !locator.tryReadBinary(absoluteName, 1024u).has_value()
                && !locator.tryReadBinary(absoluteName, 1024u, exceeded).has_value() && !exceeded
                && !locator.tryReadBinary(absoluteName, 1024u, &source).has_value() && source.empty()
                && !locator.tryReadText(absoluteName).has_value()
                && !locator.findOnDisk(absoluteName).has_value()
                && !locator.findSelectedOnDisk(absoluteName).has_value(),
            "ResourceLocator.archive_only an absolute name reads as missing through every lookup");
        const auto entry = locator.tryReadBinary("3do/fixture.3do");
        check(entry.has_value() && entry->size() == 1u && (*entry)[0] == std::byte{ 'x' },
            "ResourceLocator.archive_only mounted archive entries still resolve");
        bool refused = false;
        try { locator.addRoot(root); }
        catch ( const std::logic_error& ) { refused = true; }
        check(refused && locator.roots().empty(), "ResourceLocator.archive_only refuses a loose-file root");
        const sed::assets::ResourceLocator copy = locator;
        check(copy.archivesOnly() && !copy.tryReadBinary(absoluteName).has_value(),
            "ResourceLocator.archive_only copies keep the mode");
        locator.setArchivesOnly(false);
        check(!locator.archivesOnly() && locator.tryReadBinary(absoluteName).has_value() && copy.archivesOnly(),
            "ResourceLocator.archive_only can be turned off without changing earlier copies");
        std::error_code removeError;
        std::filesystem::remove(outside, removeError);
    }

    {
        // Resource names are case-insensitive in the game and inside
        // archives. Loose files must resolve the same way on a file system
        // that distinguishes case, where an installation copied from Windows
        // keeps spellings such as MISC/CogStrings.uni.
        const std::filesystem::path caseRoot = "tmp_res_case_" + unique;
        const std::filesystem::path lowRoot = "tmp_res_case_low_" + unique;
        const auto write = [](const std::filesystem::path& path, const std::string_view text)
        {
            std::error_code directoryError;
            std::filesystem::create_directories(path.parent_path(), directoryError);
            std::ofstream out(path, std::ios::binary);
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            return static_cast<bool>(out);
        };
        const std::filesystem::path strings = caseRoot / "MISC" / "CogStrings.uni";
        const std::filesystem::path model = caseRoot / "3DO" / "Crate.3DO";
        check(write(strings, "STRINGS") && write(model, "MODEL") && write(lowRoot / "misc" / "cogstrings.uni", "LOW"),
            "ResourceLocator.disk.case creates its fixture");
        // Whether this file system folds case decides which paths can exist.
        std::error_code probeError;
        const bool foldsCase = std::filesystem::exists(caseRoot / "misc" / "cogstrings.uni", probeError);
        try
        {
            sed::assets::ResourceLocator locator{};
            locator.addRoot(caseRoot);
            check(locator.tryReadText("misc/cogstrings.uni").value_or("") == "STRINGS"
                    && locator.tryReadText("MISC\\COGSTRINGS.UNI").value_or("") == "STRINGS",
                "ResourceLocator.disk.case reads a loose file whose folder and name differ only in case");
            check(locator.tryReadText("crate.3do").value_or("") == "MODEL",
                "ResourceLocator.disk.case applies to the kind-folder candidates");
            bool exceeded = true;
            std::filesystem::path source;
            const auto bounded = locator.tryReadBinary("misc/cogstrings.uni", 64u, &source);
            std::error_code equivalentError;
            check(bounded.has_value() && bounded->size() == 7u
                    && std::filesystem::equivalent(source, strings, equivalentError) && !equivalentError
                    && (foldsCase || source == strings),
                "ResourceLocator.disk.case reports the file's own spelling as the disk source");
            check(!locator.tryReadBinary("misc/cogstrings.uni", 3u, exceeded).has_value() && exceeded,
                "ResourceLocator.disk.case keeps the byte budget on a case-folded match");
            const auto onDisk = locator.findOnDisk("misc/cogstrings.uni");
            const auto selected = locator.findSelectedOnDisk("misc/cogstrings.uni");
            check(onDisk.has_value() && selected.has_value()
                    && (foldsCase || (*onDisk == strings && *selected == strings)),
                "ResourceLocator.disk.case finds the file through both path queries");
            check(!locator.tryReadText("misc").has_value() && !locator.tryReadText("3do").has_value()
                    && !locator.findOnDisk("MISC").has_value(),
                "ResourceLocator.disk.case never returns a folder for a file name");
            check(!locator.tryReadText("misc/cogstrings.un").has_value()
                    && !locator.tryReadText("misc/cogstrings.uni/x").has_value()
                    && !locator.tryReadText("misc/../MISC/CogStrings.uni/..").has_value(),
                "ResourceLocator.disk.case still requires the whole name and a regular file");

            // The later root wins whatever its spelling, as it would where
            // the file system folds case.
            sed::assets::ResourceLocator layered{};
            layered.addRoot(lowRoot);
            layered.addRoot(caseRoot);
            check(layered.tryReadText("misc/cogstrings.uni").value_or("") == "STRINGS",
                "ResourceLocator.disk.case keeps root precedence ahead of an exact spelling in an earlier root");
            sed::assets::ResourceLocator reversed{};
            reversed.addRoot(caseRoot);
            reversed.addRoot(lowRoot);
            check(reversed.tryReadText("MISC/CogStrings.uni").value_or("") == "LOW",
                "ResourceLocator.disk.case keeps root precedence for a later root with a different spelling");

            if ( !foldsCase )
            {
                // The case-folded search never walks "." or "..": a name
                // spelled with them resolves only as the file system itself
                // resolves it.
                // (.bin has no kind folder, so the name is its only candidate.)
                check(write(caseRoot / "DATA" / "Blob.BIN", "BLOB")
                        && locator.tryReadText("data/blob.bin").value_or("") == "BLOB"
                        && locator.tryReadText("DATA/../DATA/Blob.BIN").value_or("") == "BLOB"
                        && !locator.tryReadText("DATA/../data/blob.bin").has_value()
                        && !locator.tryReadText("./data/blob.bin").has_value(),
                    "ResourceLocator.disk.case does not fold case across dot components");
                // Two spellings in one folder: the exact one wins, and
                // without an exact one the first in byte order does.
                check(write(caseRoot / "MISC" / "cogstrings.UNI", "UPPER-EXT")
                        && write(caseRoot / "MISC" / "COGSTRINGS.uni", "UPPER-NAME"),
                    "ResourceLocator.disk.case adds two more spellings");
                check(locator.tryReadText("MISC/CogStrings.uni").value_or("") == "STRINGS"
                        && locator.tryReadText("MISC/cogstrings.UNI").value_or("") == "UPPER-EXT",
                    "ResourceLocator.disk.case prefers the exact spelling among several");
                check(locator.tryReadText("misc/cogstrings.uni").value_or("") == "UPPER-NAME",
                    "ResourceLocator.disk.case picks the first spelling in byte order when none is exact");
                // A folder spelled like the file is passed over for the file.
                std::error_code directoryError;
                std::filesystem::create_directories(caseRoot / "MISC" / "ALPHA.DAT", directoryError);
                check(!directoryError && write(caseRoot / "MISC" / "alpha.dat", "FILE")
                        && locator.tryReadText("misc/Alpha.dat").value_or("") == "FILE",
                    "ResourceLocator.disk.case passes over a folder spelled like the file");

                // Folder listings are kept between lookups once a folder has
                // been quiet for a while, and dropped when it changes.
                const std::filesystem::path quiet = caseRoot / "QUIET";
                std::error_code timeError;
                check(write(quiet / "Old.DAT", "OLD"), "ResourceLocator.disk.case creates a quiet folder");
                std::filesystem::last_write_time(quiet,
                    std::filesystem::file_time_type::clock::now() - std::chrono::hours(1), timeError);
                check(!timeError && locator.tryReadText("quiet/old.dat").value_or("") == "OLD"
                        && locator.tryReadText("quiet/OLD.dat").value_or("") == "OLD",
                    "ResourceLocator.disk.case reads repeatedly through a kept folder listing");
                check(write(quiet / "New.DAT", "NEW") && locator.tryReadText("quiet/new.dat").value_or("") == "NEW",
                    "ResourceLocator.disk.case sees a file added after its folder was listed");
                std::filesystem::last_write_time(quiet,
                    std::filesystem::file_time_type::clock::now() - std::chrono::minutes(30), timeError);
                check(!timeError && locator.tryReadText("quiet/new.dat").value_or("") == "NEW",
                    "ResourceLocator.disk.case lists the folder again once it is quiet");
                std::error_code removeError;
                std::filesystem::remove(quiet / "Old.DAT", removeError);
                check(!removeError && !locator.tryReadText("quiet/old.dat").has_value()
                        && locator.tryReadText("quiet/new.dat").value_or("") == "NEW",
                    "ResourceLocator.disk.case sees a removal from a kept folder listing");
            }
        }
        catch ( const std::exception& e )
        {
            check(false, e.what());
        }
        std::filesystem::remove_all(caseRoot, ec);
        std::filesystem::remove_all(lowRoot, ec);
    }
}

} // namespace sed::tests
