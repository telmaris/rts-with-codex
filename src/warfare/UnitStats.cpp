#include "warfare/UnitStats.h"
#include "economy/Building.h"          // MilitaryUnitType, SoldierDivision, DivisionEquipment
#include "warfare/Equipment.h"         // equipment quality table
#include "economy/BalanceModifiers.h"  // BalanceModifierSet / context
#include "economy/Player.h"            // Player::ModifyBalance (PlayerSupplyConservation)

#include <algorithm>
#include <cmath>

UnitStats MakeDefaultUnitStats(MilitaryUnitType type)
{
    UnitStats stats;
    switch (type)
    {
        case MilitaryUnitType::Swordsman:
            stats.lightAttack   = 14.0f; stats.armoredAttack = 10.0f; stats.shock = 8.0f;
            stats.armor         = 6.0f;  stats.piercing      = 3.0f;  stats.defense = 12.0f;
            stats.maxStrength   = 200.0f; stats.maxCohesion  = 40.0f; stats.morale = 68.0f;
            stats.speed         = 10.0f; stats.supplyUse     = 1.2f;  stats.fatigueRate = 1.0f;
            stats.armoredShare  = 0.35f;
            break;
        case MilitaryUnitType::Archer:
            stats.lightAttack   = 16.0f; stats.armoredAttack = 6.0f;  stats.shock = 4.0f;
            stats.armor         = 2.0f;  stats.piercing      = 6.0f;  stats.defense = 7.0f;
            stats.maxStrength   = 120.0f; stats.maxCohesion  = 30.0f; stats.morale = 64.0f;
            stats.speed         = 12.0f; stats.supplyUse     = 1.0f;  stats.fatigueRate = 1.1f;
            stats.armoredShare  = 0.10f;
            break;
        case MilitaryUnitType::Spearman:
            stats.lightAttack   = 10.0f; stats.armoredAttack = 12.0f; stats.shock = 6.0f;
            stats.armor         = 5.0f;  stats.piercing      = 8.0f;  stats.defense = 14.0f;
            stats.maxStrength   = 180.0f; stats.maxCohesion  = 38.0f; stats.morale = 66.0f;
            stats.speed         = 11.0f; stats.supplyUse     = 1.1f;  stats.fatigueRate = 1.0f;
            stats.armoredShare  = 0.25f;
            break;
        case MilitaryUnitType::Cavalry:
            stats.lightAttack   = 18.0f; stats.armoredAttack = 12.0f; stats.shock = 20.0f;
            stats.armor         = 5.0f;  stats.piercing      = 4.0f;  stats.defense = 9.0f;
            stats.maxStrength   = 80.0f; stats.maxCohesion  = 45.0f; stats.morale = 70.0f;
            stats.speed         = 18.0f; stats.supplyUse     = 1.6f;  stats.fatigueRate = 1.4f;
            stats.armoredShare  = 0.30f;
            break;
        default: // Militia
            stats.lightAttack   = 8.0f;  stats.armoredAttack = 4.0f;  stats.shock = 5.0f;
            stats.armor         = 2.0f;  stats.piercing      = 1.0f;  stats.defense = 8.0f;
            stats.maxStrength   = 100.0f; stats.maxCohesion  = 25.0f; stats.morale = 55.0f;
            stats.speed         = 14.0f; stats.supplyUse     = 0.8f;  stats.fatigueRate = 1.3f;
            stats.armoredShare  = 0.05f;
            break;
    }
    return stats;
}

float ResolveUnitStat(const Stat<float>& stat, MilitaryUnitType unitType,
                      const BalanceModifierSet* mods)
{
    const float base = stat.GetBase();
    if (mods == nullptr)
        return base;

    BalanceModifierContext context;
    context.stat = stat.GetStatId();
    context.unitType = unitType;
    return static_cast<float>(mods->ModifyDouble(static_cast<double>(base), context));
}

float ResolveDivisionMaxCohesion(const SoldierDivision& division, const BalanceModifierSet* mods)
{
    return ResolveUnitStat(division.stats.maxCohesion, division.type, mods);
}

float ResolveDivisionMaxStrength(const SoldierDivision& division, const BalanceModifierSet* mods)
{
    return ResolveUnitStat(division.stats.maxStrength, division.type, mods);
}

float ResolveDivisionMorale(const SoldierDivision& division, const BalanceModifierSet* mods)
{
    return ResolveUnitStat(division.stats.morale, division.type, mods);
}

float DivisionEquipmentQuality(const DivisionEquipment& equipment)
{
    const ResourceType slots[] = {
        equipment.weapon, equipment.armor, equipment.rangedWeapon, equipment.ammo};

    float total = 0.0f;
    int count = 0;
    for (ResourceType type : slots)
    {
        const EquipmentProfile* profile = FindEquipmentProfile(type);
        if (profile == nullptr)
            continue;
        total += profile->quality;
        count++;
    }

    // No recognised gear → makeshift equipment (e.g. militia with WEAPON_SUPPLY).
    return count > 0 ? total / static_cast<float>(count) : 0.5f;
}

float DivisionSupplyEfficiency(const SoldierDivision& division)
{
    // Weapon supply: at 0% ammo/serviceable gear a division still fights at 20%
    // (fists + improvised), scaling linearly to 100% when fully armed.
    const float weaponRatio = division.weaponSupplyCapacity > 0
        ? std::clamp(static_cast<float>(division.weaponSupply) /
                     static_cast<float>(division.weaponSupplyCapacity), 0.0f, 1.0f)
        : 1.0f;
    const float weaponFactor = 0.2f + 0.8f * weaponRatio;

    // Food: a starving division (no rations at all) fights at 40%.
    const float foodFactor = division.foodSupply > 0 ? 1.0f : 0.4f;

    return std::clamp(weaponFactor * foodFactor, 0.1f, 1.0f);
}

DivisionCombatStats ComputeDivisionCombatStats(const SoldierDivision& division,
                                               const BalanceModifierSet* mods)
{
    const UnitStats& s = division.stats;
    const MilitaryUnitType type = division.type;
    const float quality = DivisionEquipmentQuality(division.equipment);

    DivisionCombatStats out;
    out.equipmentQuality = quality;

    // Gear QUALITY scales how hard you hit and how well you shrug off blows (a steel
    // sword beats a copper one); morale, speed and the manpower pool come straight
    // from the (modified) unit stats. Supply availability is applied separately as
    // supplyEfficiency (below) so it scales the WHOLE damage output, not just this
    // term — see the note in DivisionCombatStats / ResolveOneSidedDamage.
    out.lightAttack   = ResolveUnitStat(s.lightAttack, type, mods) * quality;
    out.armoredAttack = ResolveUnitStat(s.armoredAttack, type, mods) * quality;
    out.shock         = ResolveUnitStat(s.shock, type, mods) * quality;
    out.piercing      = ResolveUnitStat(s.piercing, type, mods) * quality;
    out.armor         = ResolveUnitStat(s.armor, type, mods) * quality;
    out.defense       = ResolveUnitStat(s.defense, type, mods) * quality;
    out.morale        = ResolveUnitStat(s.morale, type, mods);
    out.speed         = ResolveUnitStat(s.speed, type, mods);
    out.maxStrength   = ResolveUnitStat(s.maxStrength, type, mods);
    out.maxCohesion   = ResolveUnitStat(s.maxCohesion, type, mods);

    out.strength           = static_cast<float>(division.strength);
    out.armoredShare       = ResolveUnitStat(s.armoredShare, type, mods);
    out.isArmored          = out.armoredShare > 0.3f || division.IsMounted();
    out.hpDamageMultiplier  = ResolveUnitStat(s.hpDamageMultiplier, type, mods);
    out.orgDamageMultiplier = ResolveUnitStat(s.orgDamageMultiplier, type, mods);

    // ── Supply efficiency ────────────────────────────────────────────────────
    // The dominant part of a duel is the constant damage floor; if it ignored
    // logistics an out-of-supply division would fight at full strength (the old
    // bug — enemies with no supply chain hit just as hard). Weapon supply and food
    // now gate the entire output: run dry on ammo and you fight at a fraction; go
    // hungry and you fight worse still. Never zero — troops always have fists — so
    // battles between two starved armies still resolve.
    out.supplyEfficiency = DivisionSupplyEfficiency(division);
    return out;
}

namespace
{
    // Cohesion-first combat (HoI4-style): every hit lands on organization first;
    // strength (manpower) is only lost as a consequence of the DEFENDER lacking
    // the supplies to absorb that hit without real casualties. A fully-supplied
    // division gets ground down and eventually breaks/retreats but keeps its
    // men; an under-supplied one bleeds strength on top. See the design note on
    // ResolveOneSidedDamage below and docs/war_system_phase2_design.md Phase C
    // for the historical (now superseded) two-track derivation.
    constexpr float kOrgDamageBase        = 0.6f;
    constexpr float kMinDamageFraction    = 0.15f;  // floor so 0 defense stats can't fully no-sell
    constexpr float kCombatSecondsPerHour = 60.0f;  // 1 "combat hour" = 60s of sim time (tunable)

    // Constant per-combat-hour organization floor (BUG 4 fix), so even a
    // near-dead division still grinds the fight toward a conclusion. Calibrated
    // so two equal swordsman divisions (maxCohesion≈40) break in roughly
    // 20–30 s of simulation time — see ResolveOneSidedDamage.
    constexpr float kConstantOrgFloor = 100.0f;

    // Small deterministic variance fraction (±7.5 %) that simulates the
    // random-but-averaged-out dice rolls HoI4 uses. Seeded from the simulation tick
    // and the two division IDs so all lockstep clients produce the same result.
    // Implementation: Wang-hash (cheap, bijective, well-distributed) on a composite
    // of the three integers, normalized to [0,1) then mapped to [1-v/2, 1+v/2].
    constexpr float kCombatVariance = 0.15f;   // ±7.5 % (0.925 … 1.075)

    uint32_t WangHash(uint32_t v)
    {
        v = (v ^ 61u) ^ (v >> 16u);
        v *= 9u;
        v ^= v >> 4u;
        v *= 0x27d4eb2du;
        v ^= v >> 15u;
        return v;
    }

    // Returns a variance multiplier in [1-kCombatVariance/2, 1+kCombatVariance/2].
    // Completely deterministic: same tick+idA+idB → same result on every client.
    float CombatVarianceMultiplier(uint64_t simTick, int idA, int idB)
    {
        uint32_t seed = WangHash(static_cast<uint32_t>(simTick & 0xFFFFFFFFu));
        seed ^= WangHash(static_cast<uint32_t>(idA));
        seed ^= WangHash(static_cast<uint32_t>(idB));
        seed = WangHash(seed);
        // Map to [0,1) then to [1-v/2, 1+v/2]
        constexpr float kInv32 = 1.0f / 4294967296.0f;
        const float r = static_cast<float>(seed) * kInv32;   // [0, 1)
        return 1.0f + kCombatVariance * (r - 0.5f);
    }

    // HP scaling steps in 10% increments of the attacker's strength ratio, floor
    // never below 0.1 (a division at 1 HP still fights, just barely).
    float HpScaling(float strength, float maxStrength)
    {
        float ratio = maxStrength > 0.0f ? std::clamp(strength / maxStrength, 0.0f, 1.0f) : 0.0f;
        return std::max(0.1f, std::floor(ratio * 10.0f + 1e-6f) / 10.0f);
    }

    // One-directional expected damage: `attacker` hitting `defender` over `h`
    // combat-hours, with a variance multiplier applied for non-zero randomness.
    //
    // Cohesion is the single damage channel: raw damage is the attacker's attack
    // (light or armored, whichever applies to the defender) plus piercing,
    // knocked down by the defender's defense (never below a small floor
    // fraction, so 0 defense can't fully no-sell a hit). That total — plus the
    // constant floor that guarantees a finite fight — is scaled by the
    // ATTACKER's supply efficiency (an unsupplied attacker hits far softer) and
    // lands entirely on the defender's organization.
    //
    // Strength (manpower) loss is NOT a second damage roll — it is the fraction
    // of that cohesion hit the DEFENDER couldn't absorb without real casualties,
    // driven by the defender's OWN supply shortage (1 - supplyEfficiency). A
    // fully-supplied defender (food + weapons in hand) takes zero permanent
    // losses from a duel: it gets pushed back, not killed. Losses come from
    // fighting undersupplied.
    void ResolveOneSidedDamage(const DivisionCombatStats& attacker, const DivisionCombatStats& defender,
                               float h, float varianceMul, float& hpLoss, float& orgLoss)
    {
        const float rawAttack = (defender.isArmored ? attacker.armoredAttack : attacker.lightAttack)
                                 + attacker.piercing;
        const float mitigated = std::max(rawAttack * kMinDamageFraction, rawAttack - defender.defense);

        const float hpScaling = HpScaling(attacker.strength, attacker.maxStrength);

        orgLoss = (mitigated * kOrgDamageBase * hpScaling * defender.orgDamageMultiplier + kConstantOrgFloor)
                  * h * varianceMul * attacker.supplyEfficiency;

        const float defenderShortage = 1.0f - defender.supplyEfficiency;  // 0 = fully supplied, no casualties
        hpLoss = orgLoss * defenderShortage * defender.hpDamageMultiplier;
    }
}

DivisionDuelResult ResolveDivisionDuel(const DivisionCombatStats& attacker,
                                       const DivisionCombatStats& defender, double dt,
                                       uint64_t simTick, int attackerDivId, int defenderDivId)
{
    const float h = static_cast<float>(dt) / kCombatSecondsPerHour;

    // Each side gets its own independent variance roll (attacker vs defender
    // direction — order of ids is swapped for the second call so the two sides
    // can diverge slightly, which makes battles feel more dynamic while staying
    // fully deterministic per tick).
    const float varA = CombatVarianceMultiplier(simTick, attackerDivId, defenderDivId);
    const float varD = CombatVarianceMultiplier(simTick, defenderDivId, attackerDivId);

    DivisionDuelResult result;
    ResolveOneSidedDamage(attacker, defender, h, varA, result.defenderStrengthLoss, result.defenderCohesionLoss);
    ResolveOneSidedDamage(defender, attacker, h, varD, result.attackerStrengthLoss, result.attackerCohesionLoss);
    return result;
}

namespace
{
    // ─── Supply upkeep rates ──────────────────────────────────────────────────
    // Continuous consumption multipliers over the per-tick reference
    //   baseUse = supplyUse * (strength/maxStrength) * conservationMul * dt
    // Each pool multiplies baseUse by the rate for the division's current posture.
    // Rates are tuned so an unsupplied full-strength division is under visible
    // logistics pressure within a couple of minutes (food first). Combat rates are
    // kept for when the combat system returns (ConsumeDivisionSupply(engaged=true)),
    // but nothing sets `engaged` while field combat is removed.
    //
    // Food (rations) — EVERY division eats, every tick, wherever it is. A garrison
    // sitting on its supply source eats least; a column on the march eats most.
    constexpr float kFoodGarrisonMul = 0.7f;   // in-building, near the depot
    constexpr float kFoodFieldMul    = 1.0f;   // deployed, holding position
    constexpr float kFoodMarchMul    = 1.5f;   // deployed and marching (fatigue)
    constexpr float kFoodCombatMul   = 3.0f;   // fighting (legacy combat reference)
    // Materiel (equipment maintenance/repairs) — trickles in the field, negligible
    // in garrison, heavy in combat.
    constexpr float kMaterielGarrisonMul = 0.1f;
    constexpr float kMaterielFieldMul    = 0.5f;
    constexpr float kMaterielMarchMul    = 0.8f;
    constexpr float kMaterielCombatMul   = 3.0f;
    // Weapons/ammunition — spent in battle; light wear while marching in the field
    // (a campaigning army needs re-arming even without fighting). Static otherwise.
    constexpr float kWeaponMarchMul  = 0.3f;
    constexpr float kWeaponCombatMul = 3.0f;

    // Fraction of current strength lost per second while completely out of food.
    constexpr float kStarvationRate = 0.02f;

    // Drains `use` from one pool, accumulating sub-1 amounts in `buffer` so slow
    // consumption is not lost to integer rounding (same pattern as strengthBuffer).
    void DrainPool(int& pool, float& buffer, float use)
    {
        if (use <= 0.0f || pool <= 0)
            return;
        buffer += use;
        int whole = static_cast<int>(buffer);
        if (whole > 0)
        {
            pool = std::max(0, pool - whole);
            buffer -= static_cast<float>(whole);
        }
    }
}

void ConsumeDivisionSupply(SoldierDivision& division, double dt, bool engaged, bool deployed,
                           double supplyConservation)
{
    if (dt <= 0.0)
        return;

    const float conservationMul = 1.0f - static_cast<float>(std::clamp(supplyConservation, 0.0, kMaxSupplyConservation));

    const float maxStrength = std::max(1.0f, division.stats.maxStrength.GetBase());
    const float strengthRatio = std::clamp(static_cast<float>(division.strength) / maxStrength, 0.0f, 1.0f);
    // Per-tick reference consumption. Every pool's drain is this scaled by the
    // posture-specific rate below, so supply drains continuously (upkeep), not only
    // during combat.
    const float baseUse = division.stats.supplyUse.GetBase() * strengthRatio *
                          conservationMul * static_cast<float>(dt);

    const bool marching = division.inTransit;

    // Food: always eaten. Combat > marching > holding in the field > garrison.
    float foodRate = kFoodGarrisonMul;
    if (engaged)       foodRate = kFoodCombatMul;
    else if (deployed) foodRate = marching ? kFoodMarchMul : kFoodFieldMul;
    DrainPool(division.foodSupply, division.foodSupplyConsumeBuffer, baseUse * foodRate);

    // Materiel: continuous maintenance, mostly a field/combat cost.
    float materielRate = kMaterielGarrisonMul;
    if (engaged)       materielRate = kMaterielCombatMul;
    else if (deployed) materielRate = marching ? kMaterielMarchMul : kMaterielFieldMul;
    DrainPool(division.materielSupply, division.materielSupplyConsumeBuffer, baseUse * materielRate);

    // Weapons/ammunition: spent in battle, light wear while marching in the field.
    float weaponRate = 0.0f;
    if (engaged)                       weaponRate = kWeaponCombatMul;
    else if (deployed && marching)     weaponRate = kWeaponMarchMul;
    if (weaponRate > 0.0f)
        DrainPool(division.weaponSupply, division.weaponSupplyConsumeBuffer, baseUse * weaponRate);

    // Starvation: no food at all slowly kills the division (manpower attrition).
    if (division.foodSupply <= 0 && division.strength > 0)
    {
        division.strengthAttritionBuffer += kStarvationRate * static_cast<float>(division.strength) * static_cast<float>(dt);
        int whole = static_cast<int>(division.strengthAttritionBuffer);
        if (whole > 0)
        {
            division.strength = std::max(0, division.strength - whole);
            division.strengthAttritionBuffer -= static_cast<float>(whole);
            SyncDerivedHealth(division);
        }
    }
}

double PlayerSupplyConservation(const Player& player)
{
    const double raw = player.ModifyBalance(BalanceStat::SupplyConservation, 0.0);
    return std::clamp(raw, 0.0, kMaxSupplyConservation);
}

void RegenerateDivisionCohesion(SoldierDivision& division, double dt, bool inOwnTerritory,
                                const BalanceModifierSet* mods)
{
    if (dt <= 0.0)
        return;

    const float maxCohesion = ResolveDivisionMaxCohesion(division, mods);
    if (division.cohesion >= maxCohesion)
    {
        division.cohesion = maxCohesion;
        return;
    }

    constexpr float kBaseRegenSeconds = 30.0f;  // full bar in ~30s under ideal conditions
    const float materielMul = division.materielSupply > 0 ? 1.0f : 0.35f;
    const float moraleMul   = 0.5f + 0.5f * std::clamp(static_cast<float>(division.morale) / 100.0f, 0.0f, 1.0f);
    const float territoryMul = inOwnTerritory ? 1.0f : 0.4f;

    const float rate = (maxCohesion / kBaseRegenSeconds) * materielMul * moraleMul * territoryMul;
    division.cohesion = std::min(maxCohesion, division.cohesion + rate * static_cast<float>(dt));
}

int ReinforceDivisionStrength(SoldierDivision& division, Player& owner, double dt,
                              const BalanceModifierSet* mods)
{
    if (dt <= 0.0)
        return 0;

    const float maxStrength = ResolveDivisionMaxStrength(division, mods);
    if (static_cast<float>(division.strength) >= maxStrength)
        return 0;
    // Must be fed and armed to absorb replacements — an unsupplied division just
    // stands (or keeps bleeding via starvation elsewhere).
    if (division.foodSupply <= 0 || division.weaponSupply <= 0)
        return 0;

    constexpr float kReinforceRatePerSecond = 4.0f;  // strength points/sec at full rate
    const float wanted = std::min(kReinforceRatePerSecond * static_cast<float>(dt),
                                  maxStrength - static_cast<float>(division.strength));
    if (wanted <= 0.0f)
        return 0;

    const double conservation = PlayerSupplyConservation(owner);
    const double manpowerCost = static_cast<double>(wanted) * (1.0 - conservation);
    if (!owner.strategicResources.Consume(StrategicResourceType::Manpower, manpowerCost))
        return 0;

    division.reinforcementBuffer += wanted;
    int whole = static_cast<int>(division.reinforcementBuffer);
    if (whole <= 0)
        return 0;

    division.strength += whole;
    division.reinforcementBuffer -= static_cast<float>(whole);
    SyncDerivedHealth(division);
    return whole;
}

void SyncDerivedHealth(SoldierDivision& division)
{
    const float maxStrength = std::max(1.0f, division.stats.maxStrength.GetBase());
    division.maxHealth = 100;
    division.health = static_cast<int>(std::lround(
        100.0f * std::clamp(static_cast<float>(division.strength) / maxStrength, 0.0f, 1.0f)));
}
