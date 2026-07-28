#ifndef TREE_SERIALIZER_H
#define TREE_SERIALIZER_H

// Writing side of the .rtsdata research format. The editor never parses — it
// only emits — and every save is verified by reading the file back through the
// game's real parser and diffing. If that round trip does not match, the save
// is reported as failed rather than silently producing a file the game reads
// differently than the editor shows.

#include "research/Technology.h"

#include <string>
#include <vector>

// Option lists for the inspector's dropdowns. These are exactly the names the
// game's parser accepts (ParseBalanceStat/ParseBuildingType/ParseResourceType/
// ParseResourceCategory in src/research/Technology.cpp) — offering anything else
// would let you build a modifier the game silently drops.
namespace RtsDataNames
{
    const std::vector<std::string>& BalanceStats();
    const std::vector<std::string>& BuildingTypes();
    const std::vector<std::string>& ResourceTypes();
    const std::vector<std::string>& ResourceCategories();
    const std::vector<std::string>& Categories();
    const std::vector<std::string>& Tags();

    std::string NameOf(BalanceStat stat);
    std::string NameOf(BuildingType type);
    std::string NameOf(ResourceType type);
    std::string NameOf(ResourceCategory category);

    BalanceStat ToBalanceStat(const std::string& name);
    BuildingType ToBuildingType(const std::string& name);
    ResourceType ToResourceType(const std::string& name);
    ResourceCategory ToResourceCategory(const std::string& name);
}

// Renders the whole tree as .rtsdata text. Regenerates from the model: comments
// and hand formatting in the previous file are not preserved (the editor is the
// source of truth, per the project decision).
std::string SerializeTree(const std::vector<TechnologyDefinition>& definitions);

// Formats one modifier as its `modifier ...` line, without the leading indent.
std::string SerializeModifier(const BalanceModifier& modifier);

struct SaveResult
{
    bool ok{false};
    std::string message;
};

// Writes the tree to `path` (creating a one-time `<path>.bak` of whatever was
// there first), then re-reads it with the game's parser and diffs. `isFocus`
// selects which loader the verification uses.
SaveResult SaveTree(const std::string& path,
                    const std::vector<TechnologyDefinition>& definitions,
                    bool isFocus);

#endif
