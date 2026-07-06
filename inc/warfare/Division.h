#ifndef WARFARE_DIVISION_H
#define WARFARE_DIVISION_H

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "core/Utils.h"
#include "data/Resource.h"
#include "warfare/UnitStats.h"

// Moved out of economy/Building.h (ETAP 8.1 — warfare types don't belong in
// an economy file). Building.h forwards this include so every existing
// caller keeps working unchanged; new code should include this directly.

enum class MilitaryOrderType : int
{
    None = 0,
    Attack = 1,
    Support = 2,
    Defend = 3
};

enum class MilitaryUnitType : int
{
    Militia = 0,
    Swordsman = 1,
    Archer = 2,
    Spearman = 3,
    Cavalry = 4
};

const char* MilitaryUnitLabel(MilitaryUnitType type);

enum class EquipmentCategory : uint8_t;  // full definition in Equipment.h

double GetBaseRecruitmentTime(MilitaryUnitType type);
int GetBaseRecruitmentManpowerCost(MilitaryUnitType type);
// Plain (non-equipment) resource costs: food + generic supplies.
std::vector<std::pair<ResourceType, int>> GetBaseRecruitmentResourceCosts(MilitaryUnitType type);
// Equipment costs expressed as CATEGORIES (Sword, Bow, Ammo, …). Any material /
// quality of the right category satisfies the cost, so a swordsman can be armed
// with copper, bronze, iron or steel swords — whatever the network can supply.
std::vector<std::pair<EquipmentCategory, int>> GetBaseRecruitmentEquipmentCosts(MilitaryUnitType type);

struct DivisionEquipment
{
    ResourceType weapon{ResourceType::Null};
    ResourceType armor{ResourceType::Null};
    ResourceType rangedWeapon{ResourceType::Null};
    ResourceType ammo{ResourceType::Null};
};

// Abstract division. Concrete unit classes (militia, swordsmen, spearmen,
// archers, cavalry) derive from this and supply their own baseline gear/stats
// in the constructor and their combat identity through the virtual surface.
// Per-instance state (health, experience, equipment, morale, movement) lives on
// the base; only class-defining behaviour is virtual. Divisions are held by
// unique_ptr (polymorphic), so copies go through Clone().
class SoldierDivision
{
public:
    virtual ~SoldierDivision() = default;

    // --- Polymorphic identity: implemented by each concrete unit class ---
    // Deep copy that preserves the dynamic type.
    virtual std::unique_ptr<SoldierDivision> Clone() const = 0;
    // Baseline (unmodified) combat stat block for this unit class.
    virtual UnitStats BaseStats() const = 0;
    // Human-readable class name (also the default UI label).
    virtual const char* ClassName() const = 0;
    // Whether the class fights primarily at range (archers).
    virtual bool IsRanged() const { return false; }
    // Whether the class delivers a mounted shock charge (cavalry).
    virtual bool IsMounted() const { return false; }

    int id{0};
    // The division is owned by its Player (Player::forces), not by a building.
    // This records the building it is currently stationed at / homed to (the one it
    // was trained in, or the garrison it entered): >= 0 when in a building, and it
    // still functions as the movement source and supply anchor while deployed in
    // the field. -1 only if it has no home building.
    int garrisonBuildingId{-1};
    // Cached discriminant — always equals the concrete class. Kept as a field so
    // the simulation/UI can switch on it and serialization can reconstruct the
    // right subclass without RTTI.
    MilitaryUnitType type{MilitaryUnitType::Militia};
    int manpowerScale{10};
    int maxHealth{100};
    int health{100};
    int endurance{50};
    int strength{10};
    int morale{60};
    int experience{0};
    int foodSupply{0};
    int foodSupplyCapacity{0};
    int weaponSupply{0};
    int weaponSupplyCapacity{0};
    // Tools & raw materials (WOOD/PLANKS/TOOLS) pool — fuels cohesion regen /
    // repair / entrenchment (Phase C). See docs/war_system_phase2_design.md.
    int materielSupply{0};
    int materielSupplyCapacity{0};
    // Organization (HoI4-style) — the fast-depleting combat buffer, distinct from
    // `strength` (manpower/HP), which drains slowly. See docs/war_system_phase2_design.md.
    float cohesion{0.0f};
    double speedTilesPerMinute{12.0};
    MilitaryOrderType currentOrder{MilitaryOrderType::None};
    int orderTargetPositionId{-1};
    double orderCooldown{0.0};
    DivisionEquipment equipment;

    // Combat stat block (Stat<float>, modifiable by tech/focus/commander/buildings).
    // Bases are always the class defaults, so this is re-derived from `BaseStats()`
    // rather than serialized; see MakeDefaultUnitStats / ResolveUnitStat.
    UnitStats stats{};

    // Movement state — transient, not serialized; resets to home building on save/load.
    Vec2f worldPos{-1.0f, -1.0f};      // current world-space position; -1,-1 = home building
    Vec2i occupiedTile{-1, -1};        // map tile the division stands on / targets (one per tile)
    Vec2i sectorCell{-1, -1};          // 2x2 quadrant of occupiedTile (occupiedTile/2)
    bool inTransit{false};
    // Combat state — transient. A battle starts from an attack order and stays
    // "sticky" while the division remains adjacent to an enemy, even after the
    // order clears (HoI4-style). Two idle units touching never fight.
    bool engaged{false};
    float strengthBuffer{0.0f};  // accumulates sub-1 strength loss per tick
    float reinforcementBuffer{0.0f};  // accumulates sub-1 strength GAIN from reinforcement
    // Set while the division is executing a Phase-C retreat order to a rear
    // quadrant (cohesion broke and it is falling back); cleared on disengage.
    // Transient, not serialized.
    bool retreating{false};
    // Post-combat regroup window (seconds). Set when a division breaks off a fight
    // (cohesion lost, or the player ordered it to move out of contact). While > 0
    // the division cannot be dragged back into combat by proximity — it is free to
    // fall back / reposition / be re-ordered and rebuilds organization. Transient.
    float regroupTimer{0.0f};
    // Per-tick supply upkeep buffers (transient, not serialized) — see
    // ConsumeDivisionSupply / docs/war_system_phase2_design.md (Phase B).
    float foodSupplyConsumeBuffer{0.0f};
    float weaponSupplyConsumeBuffer{0.0f};
    float materielSupplyConsumeBuffer{0.0f};
    float strengthAttritionBuffer{0.0f};  // starvation (foodSupply == 0) manpower loss
    std::vector<int> travelPath;        // road tile ids (empty = direct march)
    int travelPathStep{0};
    double travelElapsed{0.0};
    double travelStepTime{0.0};         // sec/tile (road path) or total sec (direct march)
    std::vector<double> travelStepDurations; // per-hop seconds (mixed road/off-road); empty = uniform travelStepTime
    Vec2f travelFromPos{0.0f, 0.0f};
    Vec2f travelToPos{0.0f, 0.0f};

    float HealthRatio() const
    {
        return maxHealth > 0 ? std::clamp(health / static_cast<float>(maxHealth), 0.0f, 1.0f) : 0.0f;
    }

protected:
    explicit SoldierDivision(MilitaryUnitType t) : type(t) {}
    SoldierDivision(const SoldierDivision&) = default;
    SoldierDivision& operator=(const SoldierDivision&) = default;
};

using MilitaryDivision = SoldierDivision;

// ─── Concrete unit classes ────────────────────────────────────────────────────
// Each class seeds its baseline gear and per-instance stats in the constructor
// and exposes its combat identity through the virtual surface. Balance numbers
// for the stat block live centrally in MakeDefaultUnitStats().

class MilitiaDivision : public SoldierDivision
{
public:
    MilitiaDivision();
    std::unique_ptr<SoldierDivision> Clone() const override { return std::make_unique<MilitiaDivision>(*this); }
    UnitStats BaseStats() const override;
    const char* ClassName() const override { return "Militia"; }
};

class SwordsmanDivision : public SoldierDivision
{
public:
    SwordsmanDivision();
    std::unique_ptr<SoldierDivision> Clone() const override { return std::make_unique<SwordsmanDivision>(*this); }
    UnitStats BaseStats() const override;
    const char* ClassName() const override { return "Swordsman"; }
};

class ArcherDivision : public SoldierDivision
{
public:
    ArcherDivision();
    std::unique_ptr<SoldierDivision> Clone() const override { return std::make_unique<ArcherDivision>(*this); }
    UnitStats BaseStats() const override;
    const char* ClassName() const override { return "Archer"; }
    bool IsRanged() const override { return true; }
};

class SpearmanDivision : public SoldierDivision
{
public:
    SpearmanDivision();
    std::unique_ptr<SoldierDivision> Clone() const override { return std::make_unique<SpearmanDivision>(*this); }
    UnitStats BaseStats() const override;
    const char* ClassName() const override { return "Spearman"; }
};

class CavalryDivision : public SoldierDivision
{
public:
    CavalryDivision();
    std::unique_ptr<SoldierDivision> Clone() const override { return std::make_unique<CavalryDivision>(*this); }
    UnitStats BaseStats() const override;
    const char* ClassName() const override { return "Cavalry"; }
    bool IsMounted() const override { return true; }
};

// Factory: builds the concrete subclass for a unit type and assigns its id.
std::unique_ptr<SoldierDivision> CreateMilitaryDivision(MilitaryUnitType type, int id);

#endif
