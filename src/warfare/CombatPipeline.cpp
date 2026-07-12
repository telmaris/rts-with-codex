#include "warfare/CombatPipeline.h"

#include <algorithm>
#include <cmath>
#include <random>

bool CircleShape::Overlaps(Vec2f shapePos, Vec2f targetPos, float targetRadius) const
{
    float dx = shapePos.x - targetPos.x;
    float dy = shapePos.y - targetPos.y;
    float rSum = radius + targetRadius;
    return dx * dx + dy * dy <= rSum * rSum;
}

bool RectShape::Overlaps(Vec2f shapePos, Vec2f targetPos, float targetRadius) const
{
    float dx = std::abs(targetPos.x - shapePos.x);
    float dy = std::abs(targetPos.y - shapePos.y);
    return dx <= halfWidth + targetRadius && dy <= halfHeight + targetRadius;
}

namespace CombatResolver
{
    double ResolveDamage(double baseDamage, double targetArmor, DamageType damageType,
                         const std::map<DamageType, float>& targetResistances,
                         unsigned int worldSeed, std::uint64_t tick, int sourceUnitInstanceId)
    {
        double raw = std::max(1.0, baseDamage - targetArmor);

        float resistance = 0.0f;
        auto it = targetResistances.find(damageType);
        if (it != targetResistances.end())
            resistance = std::clamp(it->second, 0.0f, 1.0f);
        double resisted = raw * (1.0 - static_cast<double>(resistance));

        // Deterministic lockstep-safe RNG: no persistent RNG state, reseeded
        // from world/tick/source every call (CLAUDE.md's documented pattern).
        std::mt19937 rng(static_cast<unsigned int>(worldSeed) ^
                          static_cast<unsigned int>(tick) ^
                          static_cast<unsigned int>(sourceUnitInstanceId));
        std::uniform_real_distribution<double> varianceDist(0.9, 1.1);
        double variance = varianceDist(rng);

        return std::max(1.0, resisted * variance);
    }
}
