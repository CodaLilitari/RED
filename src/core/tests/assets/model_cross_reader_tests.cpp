#include "sed/assets/3do.hpp"
#include "sed/assets/key.hpp"
#include "sed/assets/model_lighting.hpp"
#include "sed/assets/pup.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace sed::assets;

namespace
{
void require(bool valid, const char* message)
{
    if (!valid) throw std::runtime_error(message);
}

std::string read(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    require(input.is_open(), "Could not open a fixture file.");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::span<const std::byte> bytes(const std::string& text)
{
    return std::as_bytes(std::span(text.data(), text.size()));
}

void requireRefusal(const Model3do& model, std::string_view field)
{
    std::ostringstream output;
    try
    {
        saveTo3DO(model, output);
    }
    catch (const std::runtime_error& error)
    {
        require(std::string_view(error.what()).find(field) != std::string_view::npos,
                "Refusal must identify the unrepresentable source field.");
        require(output.str().empty(), "A refused export must write zero bytes.");
        return;
    }
    throw std::runtime_error("A generic export silently accepted unrepresentable data: " + std::string(field));
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        require(argc == 1 || (argc == 3 && std::string_view(argv[1]) == "--emit"),
                "Usage: sed_model_cross_reader_tests [--emit NEW_DIRECTORY]");
        const fs::path source =
            fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path().parent_path() / "tests/fixtures/relay-sentry";
        const auto singleSource = read(source / "relay_single.3do");
        const auto multiSource = read(source / "relay_sentry.3do");
        const auto single = loadFrom3DO(bytes(singleSource), {.applyHierarchyTransforms = false});
        const auto canonical = saveTo3DO(single, {.scalarVersion21Vertices = true});
        const auto reloaded = loadFrom3DO(bytes(canonical), {.applyHierarchyTransforms = false});
        require(reloaded.geosetCount == 1 && reloaded.nodes.size() == 2 && reloaded.nodes[1].name == "boom" &&
                    reloaded.nodes[1].parent == 0 && reloaded.nodes[1].pivot.x == 0.01 &&
                    reloaded.materials == std::vector<std::string>{"relay_skin.mat"},
                "Canonical 2.1 output changed a joint binding, pivot or material.");
        for (int lod = 0; lod < 2; ++lod)
        {
            const auto model = loadFrom3DO(bytes(multiSource), {.lod = lod, .applyHierarchyTransforms = false});
            require(model.geosetCount == 2 && model.meshes.size() == 2 && model.nodes.size() == 2 && model.nodes[1].mesh == 1 &&
                        model.meshes[0].faces.size() == (lod == 0 ? 2u : 1u) && model.meshes[1].faces.size() == (lod == 0 ? 2u : 1u),
                    "The source's distinct geosets must remain independently readable.");
            requireRefusal(model, "geoset");
        }
        const auto key = loadFromKEY(source / "relay_point.key");
        const auto serialized = saveToKEYText(key);
        std::istringstream next(serialized);
        const auto again = loadFromKEY(next);
        require(again.frames == 12 && again.fps == 12 && again.joints == 2 && again.nodes.at(0).nodeIndex == 1 &&
                    again.nodes[0].meshName == "boom" && again.markers.at(0).frame == 3 && again.markers.at(1).markerId == 6 &&
                    saveToKEYText(again) == serialized,
                "KEY round trip changed its time, markers, native joint map or deltas.");
        const auto puppet = loadFromPUP(source / "relay_motion.pup");
        require(puppet.keys.size() == 1 && puppet.keys[0] == "relay_point.key", "Puppet lost its KEY binding.");
        const std::string extension = "SECTION: DESIGNER_NOTE\nslot 5  0.333 # private metadata\nEND\n";
        auto litSource = multiSource + extension;
        auto expected = litSource;
        std::size_t count = 0, pos = 0;
        while ((pos = expected.find(" 1.000000", pos)) != std::string::npos)
        {
            expected.replace(pos, 9, " 0.25");
            ++count;
        }
        ModelLightingOptions options;
        options.ambient = {0.25, 0.25, 0.25, 1};
        const auto baked = bakeModelLighting(bytes(litSource), options);
        require(count == 14 && baked.changedVertices == count && baked.before.geosets == 2 && baked.text == expected,
                "A lighting-only transformation must preserve every byte outside all 14 vertex samples, including the extension.");
        requireRefusal(loadFrom3DO(bytes(singleSource + extension), {.applyHierarchyTransforms = false}), "DESIGNER_NOTE");
        if (argc == 3)
        {
            const fs::path out = argv[2];
            require(fs::create_directory(out), "Choose a new output directory.");
            const fs::path originals = out / "source";
            fs::create_directory(originals);
            for (const auto* leaf : {"relay_single.3do", "relay_point.key"})
                fs::copy_file(source / leaf, originals / leaf);
            fs::copy_file(source / "canonical-recipe.json", out / "canonical-recipe.json");
            saveTo3DO(single, out / "relay_single.3do", {.scalarVersion21Vertices = true});
            saveToKEY(key, out / "relay_point.key");
            for (const auto* leaf : {"relay_sentry.3do", "relay_skin.mat", "relay_motion.pup"})
                fs::copy_file(source / leaf, out / leaf);
            require(read(out / "relay_single.3do") == canonical && read(out / "relay_point.key") == serialized,
                    "Output readback differs.");
            require(read(originals / "relay_single.3do") == singleSource &&
                    read(originals / "relay_point.key") == read(source / "relay_point.key") &&
                    read(out / "canonical-recipe.json") == read(source / "canonical-recipe.json"),
                    "The transferable fixture lost a reversible source or transformation recipe.");
        }
        std::cout << "The RED cross-reader fixture passed.\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
