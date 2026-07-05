#include "../inc/UnitStats.h"
#include "../inc/Building.h"          // MilitaryUnitType, SoldierDivision, DivisionEquipment
#include "../inc/Equipment.h"         // equipment quality table
#include "../inc/BalanceModifiers.h"  // BalanceModifierSet / context
#include "../inc/Player.h"            // Player::ModifyBalance (PlayerSupplyConservation)

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
    // Deterministic adaptation of HoI4's combat algorithm: expected values in
    // place of dice rolls (lockstep MP cannot tolerate RNG). See
    // docs/war_system_phase2_design.md, Phase C, for the full derivation and the
    // HoI4 wiki example these constants are calibrated against.
    constexpr float kHitChanceVsDefense = 0.10f;
    constexpr float kHitChanceNoDefense = 0.40f;
    constexpr float kHpDieAvg           = 1.5f;   // average of a d2 (HP die)
    constexpr float kOrgDieAvg          = 2.5f;   // average of a d4 (org die)
    constexpr float kOrgDieAvgArmored   = 3.5f;   // average of a d6 (armored, unpierced)
    constexpr float kHpDamageBase       = 0.06f;
    constexpr float kOrgDamageBase      = 0.053f;
    constexpr float kCombatSecondsPerHour = 60.0f;  // 1 "combat hour" = 60s of sim time (tunable)

    // BUG 4 fix — constant damage floor (HP-independent, per combat-hour).
    // Guarantees that even a near-dead division deals minimum damage so battles
    // always resolve in finite time. Calibrated so two equal swordsman divisions
    // (strength≈200, maxCohesion≈40) fight to a conclusion in roughly 30–60 s of
    // simulation time: kConstantOrgFloor drains org fast (drives retreat), then
    // kConstantHpFloor drains strength (kills). Both scale with `h` identically to
    // the HP-scaled term, so the ratio is preserved at any dt. Tunable here without
    // touching the rest of the formula.
    constexpr float kConstantOrgFloor = 80.0f;   // ≈ 1.33 org/s for a swordsman pair
    constexpr float kConstantHpFloor  = 200.0f;  // ≈ 3.33 strength/s for a swordsman pair

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
    // Fills the HP (strength) and organization (cohesion) loss inflicted on the
    // defender.
    void ResolveOneSidedDamage(const DivisionCombatStats& attacker, const DivisionCombatStats& defender,
                               float h, float varianceMul, float& hpLoss, float& orgLoss)
    {
        const float attackA = attacker.lightAttack + attacker.shock * 0.5f;
        const int attacks  = static_cast<int>(std::lround(attackA / 10.0f));
        const int defenses = static_cast<int>(std::lround(defender.defense / 10.0f));

        const float hits = static_cast<float>(std::min(attacks, defenses)) * kHitChanceVsDefense +
                           static_cast<float>(std::max(0, attacks - defenses)) * kHitChanceNoDefense;

        const bool armoredUnpierced = attacker.isArmored && attacker.armor > defender.piercing;
        const float orgDie = armoredUnpierced ? kOrgDieAvgArmored : kOrgDieAvg;

        const float hpScaling = HpScaling(attacker.strength, attacker.maxStrength);

        // Scaled term (from HoI4 formula) + HP-independent floor (BUG 4 fix).
        // The floor ensures even a near-dead division deals enough damage to
        // conclude the battle in finite time. The whole output — floor included —
        // is gated by the attacker's supplyEfficiency, so a division fighting on
        // empty ammo/rations hits far softer (fixes "unsupplied enemies hit full").
        hpLoss  = (hits * kHpDieAvg * kHpDamageBase  * hpScaling * attacker.hpDamageMultiplier
                   + kConstantHpFloor) * h * varianceMul * attacker.supplyEfficiency;
        orgLoss = (hits * orgDie    * kOrgDamageBase * hpScaling * attacker.orgDamageMultiplier
                   + kConstantOrgFloor) * h * varianceMul * attacker.supplyEfficiency;
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
    constexpr float kCombatSupplyMul  = 3.0f;   // consumption while engaged (reference max)
    constexpr float kIdleFoodFraction = 0.2f;   // food eaten while NOT fighting: 20% of combat
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
    (void)deployed;  // consumption now depends only on whether the division is fighting
    if (dt <= 0.0)
        return;

    const float conservationMul = 1.0f - static_cast<float>(std::clamp(supplyConservation, 0.0, kMaxSupplyConservation));

    const float maxStrength = std::max(1.0f, division.stats.maxStrength.GetBase());
    const float strengthRatio = std::clamp(static_cast<float>(division.strength) / maxStrength, 0.0f, 1.0f);
    // Reference consumption = what the division burns while engaged in combat.
    const float combatUse = division.stats.supplyUse.GetBase() * strengthRatio *
                            kCombatSupplyMul * conservationMul * static_cast<float>(dt);

    // Food is always eaten: full rate in combat, a small upkeep fraction (20%) idle.
    DrainPool(division.foodSupply, division.foodSupplyConsumeBuffer,
              engaged ? combatUse : kIdleFoodFraction * combatUse);

    // Weapons (ammunition/wear) and materiel (repairs) are only expended in battle.
    if (engaged)
    {
        DrainPool(division.weaponSupply, division.weaponSupplyConsumeBuffer, combatUse);
        DrainPool(division.materielSupply, division.materielSupplyConsumeBuffer, combatUse);
    }

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
