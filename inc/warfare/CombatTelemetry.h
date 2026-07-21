#ifndef COMBAT_TELEMETRY_H
#define COMBAT_TELEMETRY_H

#include <map>
#include <string>
#include <utility>

// Stable, semantic origin of damage received by a marching unit. This is
// deliberately coarser than DamageType: the AI needs to know whether a wave
// was stopped by the defender's army, static towers, or the HQ itself.
enum class CombatDamageSource
{
    Unit,
    Tower,
    Headquarters
};

struct UnitDamageBreakdown
{
    double fromUnits{0.0};
    double fromTowers{0.0};
    double fromHeadquarters{0.0};
    int deathsToUnits{0};
    int deathsToTowers{0};
    int deathsToHeadquarters{0};

    double TotalDamage() const { return fromUnits + fromTowers + fromHeadquarters; }

    UnitDamageBreakdown& operator+=(const UnitDamageBreakdown& other)
    {
        fromUnits += other.fromUnits;
        fromTowers += other.fromTowers;
        fromHeadquarters += other.fromHeadquarters;
        deathsToUnits += other.deathsToUnits;
        deathsToTowers += other.deathsToTowers;
        deathsToHeadquarters += other.deathsToHeadquarters;
        return *this;
    }
};

inline UnitDamageBreakdown operator-(const UnitDamageBreakdown& current,
                                     const UnitDamageBreakdown& baseline)
{
    UnitDamageBreakdown result;
    result.fromUnits = current.fromUnits - baseline.fromUnits;
    result.fromTowers = current.fromTowers - baseline.fromTowers;
    result.fromHeadquarters = current.fromHeadquarters - baseline.fromHeadquarters;
    result.deathsToUnits = current.deathsToUnits - baseline.deathsToUnits;
    result.deathsToTowers = current.deathsToTowers - baseline.deathsToTowers;
    result.deathsToHeadquarters = current.deathsToHeadquarters - baseline.deathsToHeadquarters;
    return result;
}

// Cumulative combat counters. They are diagnostic/AI memory, not authoritative
// gameplay state: combat systems record already-resolved damage here and the AI
// compares snapshots around a wave. Like the rest of UtilityAIModel's internal
// state this is intentionally not serialized.
class CombatTelemetry
{
public:
    void RecordUnitDamage(int unitInstanceId, int ownerPlayerId, const std::string& unitDefId,
                          CombatDamageSource source, double damage, bool lethal)
    {
        UnitDamageBreakdown delta;
        switch (source)
        {
            case CombatDamageSource::Unit:
                delta.fromUnits = damage;
                delta.deathsToUnits = lethal ? 1 : 0;
                break;
            case CombatDamageSource::Tower:
                delta.fromTowers = damage;
                delta.deathsToTowers = lethal ? 1 : 0;
                break;
            case CombatDamageSource::Headquarters:
                delta.fromHeadquarters = damage;
                delta.deathsToHeadquarters = lethal ? 1 : 0;
                break;
        }
        byUnitInstance[unitInstanceId] += delta;
        byPlayerAndUnitType[{ownerPlayerId, unitDefId}] += delta;
    }

    void RecordHqDamage(int attackerPlayerId, int defenderPlayerId, double damage)
    {
        hqDamageByAttackerAndTarget[{attackerPlayerId, defenderPlayerId}] += damage;
    }

    UnitDamageBreakdown GetUnitDamage(int unitInstanceId) const
    {
        auto it = byUnitInstance.find(unitInstanceId);
        return it != byUnitInstance.end() ? it->second : UnitDamageBreakdown{};
    }

    UnitDamageBreakdown GetUnitTypeDamage(int ownerPlayerId, const std::string& unitDefId) const
    {
        auto it = byPlayerAndUnitType.find({ownerPlayerId, unitDefId});
        return it != byPlayerAndUnitType.end() ? it->second : UnitDamageBreakdown{};
    }

    double GetHqDamage(int attackerPlayerId, int defenderPlayerId) const
    {
        auto it = hqDamageByAttackerAndTarget.find({attackerPlayerId, defenderPlayerId});
        return it != hqDamageByAttackerAndTarget.end() ? it->second : 0.0;
    }

    void Clear()
    {
        byUnitInstance.clear();
        byPlayerAndUnitType.clear();
        hqDamageByAttackerAndTarget.clear();
    }

private:
    std::map<int, UnitDamageBreakdown> byUnitInstance;
    std::map<std::pair<int, std::string>, UnitDamageBreakdown> byPlayerAndUnitType;
    std::map<std::pair<int, int>, double> hqDamageByAttackerAndTarget;
};

#endif
