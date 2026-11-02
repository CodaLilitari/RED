#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sed_agent
{

// Minimal JSON value used by the sed-agent protocol. The tool links
// sed_core only, so it carries its own bounded parser/serializer rather
// than a Qt or third-party JSON dependency.
struct Json
{
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type{ Type::Null };
    bool boolean{};
    double number{};
    std::string str{};
    std::vector<Json> array{};
    std::map<std::string, Json> object{};

    static Json makeBool(bool v) { Json j; j.type = Type::Bool; j.boolean = v; return j; }
    static Json makeNumber(double v) { Json j; j.type = Type::Number; j.number = v; return j; }
    static Json makeString(std::string v) { Json j; j.type = Type::String; j.str = std::move(v); return j; }
    static Json makeArray() { Json j; j.type = Type::Array; return j; }
    static Json makeObject() { Json j; j.type = Type::Object; return j; }

    [[nodiscard]] bool isObject() const noexcept { return type == Type::Object; }
    [[nodiscard]] const Json* find(std::string_view key) const
    {
        if (type != Type::Object) return nullptr;
        const auto it = object.find(std::string(key));
        return it == object.end() ? nullptr : &it->second;
    }
    void set(std::string key, Json value) { type = Type::Object; object[std::move(key)] = std::move(value); }
};

// Parses one JSON document. Throws std::runtime_error on malformed input.
// `depth` and node counts are bounded so hostile requests cannot drive
// unbounded allocation beyond the caller's byte cap.
Json parseJson(std::string_view text);
std::string toJsonString(const Json& value);

std::string jsonEscape(std::string_view text);
std::string sha256Hex(std::string_view bytes);
std::string sha256Hex(const std::vector<std::byte>& bytes);

} // namespace sed_agent
