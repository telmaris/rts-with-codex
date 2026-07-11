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
        // supplyUse is a per-TICK reference: ConsumeDivisionSupply computes
        //   baseUse = supplyUse * (strength/maxStrength) * conservationMul * dt
        // and multiplies it by the posture rate. At 100 Hz (dt=0.01) a full-
        // strength Swordsman (supplyUse=1.2) therefore drains 1.2*0.01*60 = ~72
        // food/min holding the field (kFoodFieldMul=1.0) and ~144/min fighting
        // (kFoodCombatMul=2.0). With foodSupplyCapacity ≈ manpowerScale (~200)
        // that empties in ~1.5–3 min, so logistics visibly bite. (An earlier
        // rework divided these by 60, dropping the drain to ~1/min — supply then
        // never visibly moved and cohesion, which is ceilinged by supply, never
        // dropped either; reverted.)
        // maxCohesion (organization) is a deliberate role knob, not a flat
        // scale-up with strength: defensive/line units (Spearman, Militia) hold
        // formation longest; shock/offensive units (Cavalry) break fastest in
        // exchange for their high `shock` stat breaking OTHERS faster (see
        // ResolveOneSidedDamage) — a glass cannon, not a brick with a lance.
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
            stats.maxStrength   = 120.0f; stats.maxCohesion  = 26.0f; stats.morale = 64.0f;
            stats.speed         = 12.0f; stats.supplyUse     = 1.0f;  stats.fatigueRate = 1.1f;
            stats.armoredShare  = 0.10f;
            break;
        case MilitaryUnitType::Spearman:
            stats.lightAttack   = 10.0f; stats.armoredAttack = 12.0f; stats.shock = 6.0f;
            stats.armor         = 5.0f;  stats.piercing      = 8.0f;  stats.defense = 14.0f;
            stats.maxStrength   = 180.0f; stats.maxCohesion  = 50.0f; stats.morale = 66.0f;
            stats.speed         = 11.0f; stats.supplyUse     = 1.1f;  stats.fatigueRate = 1.0f;
            stats.armoredShare  = 0.25f;
            break;
        case MilitaryUnitType::Cavalry:
            stats.lightAttack   = 18.0f; stats.armoredAttack = 12.0f; stats.shock = 20.0f;
            stats.armor         = 5.0f;  stats.piercing      = 4.0f;  stats.defense = 9.0f;
            stats.maxStrength   = 80.0f; stats.maxCohesion  = 22.0f; stats.morale = 70.0f;
            stats.speed         = 18.0f; stats.supplyUse     = 1.6f;  stats.fatigueRate = 1.4f;
            stats.armoredShare  = 0.30f;
            break;
        default: // Militia
            stats.lightAttack   = 8.0f;  stats.armoredAttack = 4.0f;  stats.shock = 5.0f;
            stats.armor         = 2.0f;  stats.piercing      = 1.0f;  stats.defense = 8.0f;
            stats.maxStrength   = 100.0f; stats.maxCohesion  = 30.0f; stats.morale = 55.0f;
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

float ResolveEffectiveDivisionMaxCohesion(const SoldierDivision& division, const BalanceModifierSet* mods)
{
    return ResolveDivisionMaxCohesion(division, mods) * DivisionSupplyEfficiency(division);
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
    // Weapon supply: at 0% ammo/serviceable gear a division fights at only 10%
    // (fists + improvised), scaling linearly to 100% when fully armed. This is
    // the dominant term — "no equipment = no cohesion" — so it also gates
    // ResolveEffectiveMaxCohesion and RegenerateDivisionCohesion, not just duel
    // damage output.
    const float weaponRatio = division.weaponSupplyCapacity > 0
        ? std::clamp(static_cast<float>(division.weaponSupply) /
                     static_cast<float>(division.weaponSupplyCapacity), 0.0f, 1.0f)
        : 1.0f;
    const float weaponFactor = 0.10f + 0.90f * weaponRatio;

    // Food: a starving division (no rations at all) fights at 35%.
    const float foodFactor = division.foodSupply > 0 ? 1.0f : 0.35f;

    // Floor lowered from 0.1 to 0.05: a division with neither weapons nor food
    // is nearly combat-ineffective, not just "weakened".
    return std::clamp(weaponFactor * foodFactor, 0.05f, 1.0f);
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
    out.isDefending         = division.currentOrder != MilitaryOrderType::Attack;

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
    // kOrgDamageBase scales the STATISTICAL term (firepower/armor/piercing/shock
    // interaction below) so it — not the constant floor — dominates org loss.
    // Calibrated on the harness (CombatTrace.*) so an even Swordsman-vs-Swordsman
    // duel breaks in ~15-20s; see docs note at ResolveOneSidedDamage for why the
    // stat term must dominate.
    constexpr float kOrgDamageBase        = 6.0f;
    constexpr float kMinDamageFraction    = 0.15f;  // floor so 0 defense stats can't fully no-sell
    constexpr float kCombatSecondsPerHour = 60.0f;  // 1 "combat hour" = 60s of sim time (tunable)
    // How much of `piercing / armor` translates into full hard-attack effectiveness.
    // Never lets armor fully no-sell a hard hit (kPierceFloor), and never lets
    // piercing over-penetrate beyond 100% (clamped to 1).
    constexpr float kPierceFloor = 0.2f;
    // Shock is a flat bonus added to firepower BEFORE defense mitigation, applied
    // only when the receiving division is defending (DivisionCombatStats::
    // isDefending) — a shock unit (cavalry) excels at breaking a line that is
    // trying to hold, not at trading blows with another attacker. See
    // ResolveOneSidedDamage.
    constexpr float kShockCoeff = 1.5f;

    // Small constant per-combat-hour organization floor (BUG 4 fix), so even a
    // division with 0 statistical damage output (fully mitigated) still grinds
    // the fight toward a conclusion instead of stalemating forever. Deliberately
    // small — the stat term (kOrgDamageBase above) must be the dominant driver of
    // org loss so lightAttack/armoredAttack/armor/piercing/defense/shock actually
    // matter, instead of being drowned out by a flat constant (the old value of
    // 100-150 made the stat term ~1% of total damage — effectively decorative).
    constexpr float kConstantOrgFloor = 10.0f;

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
    // Cohesion is the single damage channel, built from the FULL stat block
    // instead of a near-flat constant:
    //   1. firepower splits the defender's manpower into a soft/hard-cover mix
    //      (armoredShare): the attacker's lightAttack lands on the soft share,
    //      armoredAttack on the hard share — but armoredAttack is itself scaled
    //      by pierceRatio (attacker.piercing / defender.armor, clamped), so a
    //      unit whose piercing can't match the target's armor mostly bounces off
    //      the armored portion instead of ignoring armor entirely.
    //   2. shock is a flat bonus added to firepower BEFORE mitigation, but ONLY
    //      when the defender is itself defending (not pressing its own Attack
    //      order) — shock represents breaking a line trying to hold, not winning
    //      a mutual slugging match between two attackers.
    //   3. defense mitigates the combined total (never fully no-selling it, via
    //      kMinDamageFraction).
    //   4. the mitigated total (times kOrgDamageBase, so the stat term DOMINATES
    //      org loss) plus a small constant floor (guarantees a finite fight even
    //      at 0 stat damage) is scaled by the ATTACKER's supply efficiency (an
    //      unsupplied attacker hits far softer) and lands entirely on the
    //      defender's organization.
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
        const float softShare = std::clamp(1.0f - defender.armoredShare, 0.0f, 1.0f);
        const float hardShare = std::clamp(defender.armoredShare, 0.0f, 1.0f);
        const float pierceRatio = std::clamp(attacker.piercing / std::max(defender.armor, 1.0f),
                                             kPierceFloor, 1.0f);
        const float firepower = attacker.lightAttack * softShare
                               + attacker.armoredAttack * hardShare * pierceRatio;

        const float shockTerm = defender.isDefending ? attacker.shock * kShockCoeff : 0.0f;

        const float mitigated = std::max((firepower + shockTerm) * kMinDamageFraction,
                                         (firepower + shockTerm) - defender.defense);

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
    // logistics pressure within a couple of minutes (food first). `engaged` is
    // set by GameWorld::UpdateBattles (ETAP 11.2) and switches the pools onto
    // the combat rates below.
    //
    // Food (rations) — EVERY division eats, every tick, wherever it is. A garrison
    // sitting on its supply source eats least; a column on the march eats most.
    // Combat multiplier is 2x field (not 3x) — combined with the rescaled
    // supplyUse bases in MakeDefaultUnitStats, this lands a full-strength
    // Swordsman at ~1 food/min in the field and ~2/min while fighting, instead
    // of the old ~200/min combat drain that made supply irrelevant noise.
    constexpr float kFoodGarrisonMul = 0.7f;   // in-building, near the depot
    constexpr float kFoodFieldMul    = 1.0f;   // deployed, holding position
    constexpr float kFoodMarchMul    = 1.5f;   // deployed and marching (fatigue)
    constexpr float kFoodCombatMul   = 2.0f;   // fighting
    // Materiel (equipment maintenance/repairs) — trickles in the field, negligible
    // in garrison, heavy in combat. March wear cut to near-zero: a division should
    // arrive at the front with its gear, not have marched it away (see weapon
    // march rate below for the same reasoning).
    constexpr float kMaterielGarrisonMul = 0.1f;
    constexpr float kMaterielFieldMul    = 0.5f;
    constexpr float kMaterielMarchMul    = 0.1f;
    constexpr float kMaterielCombatMul   = 2.0f;
    // Weapons/ammunition — spent ONLY in actual battle. A column marching to the
    // front (not yet engaged) must arrive fully armed; march wear is 0 so armies
    // reach the front at full ammo. Engagement gating: weaponRate only applies
    // once `engaged` is true (see GameWorld.Battles.cpp).
    // The combat rate is deliberately LOW relative to the (restored, visible) food
    // rate: the weapon pool is small (~40 = establishment), so at full base
    // supplyUse a single duel would otherwise exhaust ammo in ~15-20 s, collapse
    // supply efficiency mid-fight, and turn a clean cohesion duel into a bloody
    // slog. 0.1 keeps a division armed through several engagements (a few
    // weapons/min of fighting) so ammo is a campaign-scale concern, not a
    // per-duel cliff — food is the fast, visible upkeep drain instead.
    constexpr float kWeaponMarchMul  = 0.0f;
    constexpr float kWeaponCombatMul = 0.1f;

    // Fraction of current strength lost per second while completely out of food.
    constexpr float kStarvationRate = 0.02f;
    // Fraction of current strength lost per second while completely out of
    // weapons/ammo — desertion and disorganized attrition, distinct from and
    // additive with starvation. A division with neither food nor weapons dies
    // fastest (both rates combine); one that's merely unarmed but fed still
    // eventually bleeds out and disappears, not just fights weaker forever.
    constexpr float kEquipmentAttritionRate = 0.015f;

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

    // Attrition: no food (starvation) and/or no weapons (desertion/disorganized
    // losses) each independently bleed strength — a division cut off from BOTH
    // dies fastest, but "well-fed and unarmed" or "armed and starving" both
    // eventually kill the division too, not just weaken it in combat.
    float attritionRate = 0.0f;
    if (division.foodSupply <= 0)   attritionRate += kStarvationRate;
    if (division.weaponSupply <= 0) attritionRate += kEquipmentAttritionRate;
    if (attritionRate > 0.0f && division.strength > 0)
    {
        division.strengthAttritionBuffer += attritionRate * static_cast<float>(division.strength) * static_cast<float>(dt);
        int whole = static_cast<int>(division.strengthAttritionBuffer);
        if (whole > 0)
        {
            division.strength = std::max(0, division.strength - whole);
            division.strengthAttritionBuffer -= static_cast<float>(whole);
            SyncDerivedHealth(division);
        }
    }
}

float EstimateDivisionFoodPerMinute(const SoldierDivision& division, bool deployed)
{
    const float maxStrength = std::max(1.0f, division.stats.maxStrength.GetBase());
    const float strengthRatio = std::clamp(static_cast<float>(division.strength) / maxStrength, 0.0f, 1.0f);
    const float baseUsePerMinute = division.stats.supplyUse.GetBase() * strengthRatio * 60.0f;
    return baseUsePerMinute * (deployed ? kFoodFieldMul : kFoodGarrisonMul);
}

double PlayerSupplyConservation(const Player& player)
{
    const double raw = player.ModifyBalance(BalanceStat::SupplyConservation, 0.0);
    return std::clamp(raw, 0.0, kMaxSupplyConservation);
}

void RegenerateDivisionCohesion(SoldierDivision& division, double dt, bool inOwnTerritory,
                                bool engaged, const BalanceModifierSet* mods)
{
    if (dt <= 0.0)
        return;

    // The ceiling itself is supply-scaled ("no equipment = no cohesion") — an
    // unarmed, starving division cannot sustain anywhere near its full
    // organization regardless of morale/territory/posture.
    const float effectiveMax = ResolveEffectiveDivisionMaxCohesion(division, mods);

    if (division.cohesion > effectiveMax)
    {
        // Erosion: supply collapsed faster than cohesion could react (or combat
        // is grinding weapon supply down mid-fight) — organization crumbles
        // toward the new, lower ceiling. Deliberately fast and NOT gated on
        // `engaged`: losing your ammo mid-battle should hurt immediately, not
        // wait for the fight to end.
        constexpr float kCohesionErosionPerSecond = 6.0f;
        division.cohesion = std::max(effectiveMax,
            division.cohesion - kCohesionErosionPerSecond * static_cast<float>(dt));
        return;
    }
    if (division.cohesion >= effectiveMax)
        return;

    // Regeneration only happens out of combat (HoI4-style, ETAP 11.2 / commit
    // 3ad94c2) — regenerating while `engaged` fought the Battle system's drain
    // in the same tick and made fights take ~3x longer than intended.
    if (engaged)
        return;

    constexpr float kBaseRegenSeconds = 30.0f;  // full bar in ~30s under ideal conditions
    // Materiel fuels REPAIRS specifically (distinct from the weapon/food curve
    // that already sets the ceiling above) — low materiel slows the CLIMB, not
    // just the destination.
    const float materielMul = division.materielSupply > 0 ? 1.0f : 0.35f;
    const float moraleMul   = 0.5f + 0.5f * std::clamp(static_cast<float>(division.morale) / 100.0f, 0.0f, 1.0f);
    const float territoryMul = inOwnTerritory ? 1.0f : 0.4f;
    // A division holding ground (not currently pressing an Attack order)
    // rallies its organization somewhat faster than one still on the offensive
    // — defense regenerates cohesion a bit quicker than attack.
    constexpr float kDefensivePostureBonus = 1.15f;
    const float postureMul = (division.currentOrder == MilitaryOrderType::Attack) ? 1.0f : kDefensivePostureBonus;

    const float rate = (effectiveMax / kBaseRegenSeconds) * materielMul * moraleMul * territoryMul * postureMul;
    division.cohesion = std::min(effectiveMax, division.cohesion + rate * static_cast<float>(dt));
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
