#ifndef AI_DIFFICULTY_H
#define AI_DIFFICULTY_H

#include "data/Resource.h"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

// Difficulty changes the AI's starting position, not the quality of its
// decisions. Every level runs the same deterministic utility model; stronger
// levels receive increasingly broad economic and military reserves.
enum class AIDifficulty : int
{
    Primitive = 0,
    Easy = 1,
    Normal = 2,
    Hard = 3
};

struct AIStartingResourceGrant
{
    ResourceType resource{ResourceType::Null};
    int amount{0};
};

struct AIDifficultyProfile
{
    std::string_view name;
    std::span<const AIStartingResourceGrant> startingResources;
    double manpowerCapFraction{0.0};
};

namespace AIDifficultyProfiles
{
    // Primitive intentionally matches the former Hard starting advantage.
    // This is now the minimum viable AI pace; higher levels add ready-to-use
    // military supplies without changing decision quality or simulation rules.
    inline constexpr std::array PrimitiveResources{
        AIStartingResourceGrant{ResourceType::WOOD, 30},
        AIStartingResourceGrant{ResourceType::STONE, 50},
        AIStartingResourceGrant{ResourceType::PLANKS, 40},
        AIStartingResourceGrant{ResourceType::FOOD_PROVISIONS, 30},
        AIStartingResourceGrant{ResourceType::IRON, 30},
        AIStartingResourceGrant{ResourceType::TOOLS, 10},
    };

    inline constexpr std::array EasyResources{
        AIStartingResourceGrant{ResourceType::WOOD, 80},
        AIStartingResourceGrant{ResourceType::STONE, 100},
        AIStartingResourceGrant{ResourceType::PLANKS, 80},
        AIStartingResourceGrant{ResourceType::FOOD_PROVISIONS, 80},
        AIStartingResourceGrant{ResourceType::IRON, 60},
        AIStartingResourceGrant{ResourceType::TOOLS, 20},
        AIStartingResourceGrant{ResourceType::IRON_SWORD, 20},
        AIStartingResourceGrant{ResourceType::LEATHER_ARMOR, 12},
        AIStartingResourceGrant{ResourceType::BATTERING_RAM, 5},
    };

    inline constexpr std::array NormalResources{
        AIStartingResourceGrant{ResourceType::WOOD, 140},
        AIStartingResourceGrant{ResourceType::STONE, 170},
        AIStartingResourceGrant{ResourceType::PLANKS, 140},
        AIStartingResourceGrant{ResourceType::FOOD_PROVISIONS, 150},
        AIStartingResourceGrant{ResourceType::IRON, 100},
        AIStartingResourceGrant{ResourceType::TOOLS, 35},
        AIStartingResourceGrant{ResourceType::IRON_SWORD, 40},
        AIStartingResourceGrant{ResourceType::LEATHER_ARMOR, 25},
        AIStartingResourceGrant{ResourceType::HEAVY_ARMOR, 12},
        AIStartingResourceGrant{ResourceType::SPEAR, 10},
        AIStartingResourceGrant{ResourceType::BOW, 10},
        AIStartingResourceGrant{ResourceType::ARROWS, 60},
        AIStartingResourceGrant{ResourceType::HORSE, 10},
        AIStartingResourceGrant{ResourceType::BATTERING_RAM, 10},
        AIStartingResourceGrant{ResourceType::BALLISTA, 5},
    };

    inline constexpr std::array HardResources{
        AIStartingResourceGrant{ResourceType::WOOD, 220},
        AIStartingResourceGrant{ResourceType::STONE, 260},
        AIStartingResourceGrant{ResourceType::PLANKS, 220},
        AIStartingResourceGrant{ResourceType::FOOD_PROVISIONS, 250},
        AIStartingResourceGrant{ResourceType::IRON, 160},
        AIStartingResourceGrant{ResourceType::TOOLS, 50},
        AIStartingResourceGrant{ResourceType::IRON_SWORD, 80},
        AIStartingResourceGrant{ResourceType::LEATHER_ARMOR, 45},
        AIStartingResourceGrant{ResourceType::HEAVY_ARMOR, 30},
        AIStartingResourceGrant{ResourceType::SPEAR, 25},
        AIStartingResourceGrant{ResourceType::BOW, 20},
        AIStartingResourceGrant{ResourceType::ARROWS, 120},
        AIStartingResourceGrant{ResourceType::HORSE, 20},
        AIStartingResourceGrant{ResourceType::BATTERING_RAM, 15},
        AIStartingResourceGrant{ResourceType::BALLISTA, 10},
    };

    inline constexpr std::array Profiles{
        AIDifficultyProfile{"Primitive", PrimitiveResources, 0.50},
        AIDifficultyProfile{"Easy", EasyResources, 0.70},
        AIDifficultyProfile{"Normal", NormalResources, 0.85},
        AIDifficultyProfile{"Hard", HardResources, 1.00},
    };
}

inline constexpr const AIDifficultyProfile& GetAIDifficultyProfile(AIDifficulty difficulty)
{
    return AIDifficultyProfiles::Profiles[static_cast<int>(difficulty)];
}

inline constexpr const AIDifficultyProfile& GetAIDifficultyProfile(int difficulty)
{
    if (difficulty < static_cast<int>(AIDifficulty::Primitive))
        difficulty = static_cast<int>(AIDifficulty::Primitive);
    if (difficulty > static_cast<int>(AIDifficulty::Hard))
        difficulty = static_cast<int>(AIDifficulty::Hard);
    return AIDifficultyProfiles::Profiles[static_cast<std::size_t>(difficulty)];
}

#endif
