#include "../agent_json.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

int main()
{
    int failures = 0;
    const auto refuse = [&](const std::string& input) {
        try { (void)sed_agent::parseJson(input); }
        catch (const std::exception&) { return; }
        ++failures;
        std::cerr << "FAIL: malformed or ambiguous request accepted: " << input << '\n';
    };
    for (const char* text : { "1e", "1e+", "1.2.3", "1-2", "01", "-.5", "1.",
            R"({"op":"setGravity","op":"addBox"})",
            R"({"x":1,"\u0078":2})", R"("\uD800")", R"("\uDC00")",
            R"("\uD800\u0041")", "\"raw\nnewline\"" })
        refuse(text);
    const auto text = sed_agent::parseJson(R"("\uD83D\uDE80")");
    if (text.str != "\xF0\x9F\x9A\x80")
    {
        ++failures;
        std::cerr << "FAIL: Unicode surrogate pair is not one UTF-8 scalar\n";
    }
    for (const char* valid : { "0", "-0", "1.25", "-1.25e+2", "1E-10",
            R"({"left":{"x":1},"right":{"x":2}})" })
    {
        try { (void)sed_agent::parseJson(valid); }
        catch (const std::exception&) { ++failures; std::cerr << "FAIL: valid JSON refused\n"; }
    }
    std::cout << "JSON contract failures=" << failures << '\n';
    return failures ? 1 : 0;
}
