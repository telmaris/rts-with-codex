#ifndef COMBAT_PIPELINE_H
#define COMBAT_PIPELINE_H

#include "core/Types.h"
#include "warfare/UnitDefinition.h"

#include <cstdint>
#include <map>
#include <memory>

class TileMap;

// TD(etap-5) — the shared attack pipeline decision: EVERY attack in the game
// (unit-vs-unit, unit-vs-HQ [ETAP 6], tower-vs-unit and HQ-thorns-vs-unit
// [ETAP 7]) goes through this same shape+emission+resolver pipeline. Adding
// a new attack shape (cone, AoE circle) or a multi-tick travelling
// projectile is a new ICollisionShape or a longer AttackEmission lifetime —
// never a change to CombatResolver, which stays the one place with the
// damage formula.

// Collision shape an AttackEmission tests targets against. v1: Circle
// (melee contact / a unit's own hitbox) and Rect (a straight segment/AoE
// placeholder for ETAP 7's projectiles). The interface leaves room for
// Cone/etc. later without touching the resolver.
class ICollisionShape
{
public:
    virtual ~ICollisionShape() = default;
    // Returns true when a circle of `targetRadius` centered at `targetPos`
    // overlaps this shape, itself positioned at `shapePos`.
    virtual bool Overlaps(Vec2f shapePos, Vec2f targetPos, float targetRadius) const = 0;
};

struct CircleShape : public ICollisionShape
{
    float radius{0.4f};

    CircleShape() = default;
    explicit CircleShape(float r) : radius(r) {}

    bool Overlaps(Vec2f shapePos, Vec2f targetPos, float targetRadius) const override;
};

struct RectShape : public ICollisionShape
{
    float halfWidth{0.5f};
    float halfHeight{0.5f};

    RectShape() = default;
    RectShape(float hw, float hh) : halfWidth(hw), halfHeight(hh) {}

    bool Overlaps(Vec2f shapePos, Vec2f targetPos, float targetRadius) const override;
};

// Who an emission is allowed to hit. v1 only ever uses EnemiesOnly; Everyone
// is reserved for a future friendly-fire-immune support ability or similar.
enum class AttackTargetFilter
{
    EnemiesOnly,
    Everyone
};

// A short-lived simulation object emitted by an attacker: a shape positioned
// in the world plus a damage payload. Melee/thorns (etap-5/6) resolve inline
// without ever instantiating one of these (contact is already guaranteed by
// the state machine, so there's nothing to track between ticks). A tower's
// projectile (etap-7) is the first real instantiation: it moves over many
// ticks by decrementing ticksRemaining each tick instead of expiring
// immediately, homing toward targetUnitInstanceId's current position each
// tick at `speed` tile-units/sec — a deliberate simplification over true
// ballistic lead-shooting (see docs/tech_debt.md), which still deterministically
// catches a moving target since motion is fully deterministic.
struct AttackEmission
{
    int sourcePlayerId{-1};
    int sourceUnitInstanceId{-1};
    Vec2f position{0.0f, 0.0f};
    double damage{0.0};
    DamageType damageType{DamageType::Physical};
    AttackTargetFilter filter{AttackTargetFilter::EnemiesOnly};
    std::shared_ptr<ICollisionShape> shape;
    int ticksRemaining{1};
    // Projectile-only fields (etap-7); unused (default) for inline melee/thorns.
    int targetUnitInstanceId{-1};
    double speed{0.0};
};

// Shared building-geometry helper (etap-6 HqCombatSystem's thorns AoE center
// and etap-7 TowerAttackSystem's range/targeting both need a building's
// world-space center point, not just its anchor tile).
Vec2f ComputeBuildingCenter(const TileMap& tilemap, const Building& building);

// The one place damage is computed. v1 formula: max(1, attack - target's
// armor), reduced by the target's resistance for the incoming damage type
// (0 for any type not present in `targetResistances` — i.e. no resistance),
// then scaled by a small deterministic variance factor so equal-strength
// fights aren't always a dead tie. The RNG is seeded from
// (worldSeed, tick, sourceUnitInstanceId) rather than carrying real RNG
// state in the save/snapshot — replays identically for the same seed.
namespace CombatResolver
{
    double ResolveDamage(double baseDamage, double targetArmor, DamageType damageType,
                         const std::map<DamageType, float>& targetResistances,
                         unsigned int worldSeed, std::uint64_t tick, int sourceUnitInstanceId);
}

#endif
