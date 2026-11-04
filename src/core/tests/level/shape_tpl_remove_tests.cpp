#include "test_common.hpp"

#include "sed/level/build.hpp"

#include <sstream>
#include <string>

namespace sed::tests
{
namespace
{

const std::string kTetra =
    "SEC Tetra\n"
    "dflt.cmp - 0 0 5 0 0 0 0 0\n"
    "VXS 4\n"
    "0 0 0\n"
    "1 0 0\n"
    "0 1 0\n"
    "0 0 1\n"
    "SURFS 4\n"
    "DFLT.MAT 5 4 4 3 1 0 3 -1 -1 7\n"
    " 0 0 0 5 1 0 0 5 2 0 0 5\n"
    "DFLT.MAT 5 4 4 3 1 0 3 -1 -1 7\n"
    " 0 0 0 5 3 0 0 5 1 0 0 5\n"
    "DFLT.MAT 5 4 4 3 1 0 3 -1 -1 7\n"
    " 0 0 0 5 2 0 0 5 3 0 0 5\n"
    "DFLT.MAT 5 4 4 3 1 0 3 -1 -1 7\n"
    " 1 0 0 5 3 0 0 5 2 0 0 5\n"
    "END\n";

std::string renamed(const std::string& block, const std::string& name)
{
    std::string result = block;
    result.replace(4, 5, name);
    return result;
}

std::vector<std::string> shapeNames(const std::string& text)
{
    std::istringstream input(text);
    std::vector<std::string> names{};
    for ( const level::SectorShape& shape : level::loadSectorShapesTpl(input) )
    {
        names.push_back(shape.name);
    }
    return names;
}

void testRemovesNamedBlockAndPreservesNeighbours()
{
    const std::string first = "# user shapes\n" + kTetra;
    const std::string middle = renamed(kTetra, "Sector Shape 1");
    const std::string last = renamed(kTetra, "Wedge");
    std::string text = first + "\n" + middle + "\n" + last;
    check(shapeNames(text) == std::vector<std::string>{ "Tetra", "Sector Shape 1", "Wedge" },
        "shapeTplRemove.fixture.parses");

    check(level::removeSectorShapeTplBlocks(text, "sector shape 1") == 1u,
        "shapeTplRemove.caseInsensitive.removedOne");
    check(text == first + "\n" + last, "shapeTplRemove.neighboursByteExact");
    check(shapeNames(text) == std::vector<std::string>{ "Tetra", "Wedge" },
        "shapeTplRemove.remainingShapesStillParse");
}

void testRemovesEveryDuplicateAndFirstOrLastBlocks()
{
    std::string text = kTetra + "\n" + renamed(kTetra, "Other") + "\n" + kTetra;
    check(level::removeSectorShapeTplBlocks(text, "TETRA") == 2u,
        "shapeTplRemove.duplicates.removedAll");
    check(shapeNames(text) == std::vector<std::string>{ "Other" },
        "shapeTplRemove.duplicates.onlyOtherRemains");

    check(level::removeSectorShapeTplBlocks(text, "Other") == 1u && shapeNames(text).empty(),
        "shapeTplRemove.lastBlock.removed");
}

void testMissingNameLeavesTextUntouched()
{
    std::string text = kTetra + "\r\n" + renamed(kTetra, "Wedge");
    const std::string before = text;
    check(level::removeSectorShapeTplBlocks(text, "Cube") == 0u && text == before,
        "shapeTplRemove.missing.untouched");
    check(level::removeSectorShapeTplBlocks(text, "") == 0u && text == before,
        "shapeTplRemove.emptyName.untouched");
    // A name that only prefixes a stored shape is not a match.
    check(level::removeSectorShapeTplBlocks(text, "Wed") == 0u && text == before,
        "shapeTplRemove.prefix.untouched");
}

void testUnterminatedBlockIsNotRemoved()
{
    // Without END the parser's extent is ambiguous; refuse rather than guess.
    std::string text = kTetra.substr(0, kTetra.size() - 4);
    const std::string before = text;
    check(level::removeSectorShapeTplBlocks(text, "Tetra") == 0u && text == before,
        "shapeTplRemove.unterminated.untouched");
}

} // namespace

void runShapeTplRemoveTests()
{
    testRemovesNamedBlockAndPreservesNeighbours();
    testRemovesEveryDuplicateAndFirstOrLastBlocks();
    testMissingNameLeavesTextUntouched();
    testUnterminatedBlockIsNotRemoved();
}

} // namespace sed::tests
