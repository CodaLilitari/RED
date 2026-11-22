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
    const auto checkInteger = [&](const std::string& token, bool expected)
    {
        if ( sed_agent::parseJson(token).isInteger() != expected )
        {
            ++failures;
            std::cerr << "FAIL: incorrect decimal integrality for " << token << '\n';
        }
    };
    for ( const char* integral : { "1.0", "1e0", "120e-1", "-0.0020e3",
            "4503599627370496", "0e-999999999999999999999" } )
    {
        checkInteger(integral, true);
    }
    for ( const char* fractional : { "4503599627370496.25", "1.0000000000000001",
            "1.00000000000000000000000000000000000000000000001", "120001e-3" } )
    {
        checkInteger(fractional, false);
    }
    checkInteger("1" + std::string(200, '0') + "e-200", true);
    checkInteger("1" + std::string(199, '0') + "1e-200", false);
    if ( !sed_agent::Json::makeNumber(12.0).isInteger() || sed_agent::Json::makeNumber(12.5).isInteger()
        || sed_agent::toJsonString(sed_agent::parseJson("1.0000000000000001")) != "1" )
    {
        ++failures;
        std::cerr << "FAIL: programmatic integer classification or canonical numeric output changed\n";
    }
    std::cout << "JSON contract failures=" << failures << '\n';
    return failures ? 1 : 0;
}
