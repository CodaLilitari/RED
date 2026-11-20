#pragma once

#include <filesystem>
#include <istream>
#include <string>
#include <vector>

namespace sed::assets
{

struct PupBinding
{
    std::string action;
    std::string key;
};

struct PupFile
{
    std::vector<std::string> keys;
    std::vector<PupBinding> bindings;
};

PupFile loadFromPUP(std::istream& in);
PupFile loadFromPUP(const std::filesystem::path& path);

} // namespace sed::assets
