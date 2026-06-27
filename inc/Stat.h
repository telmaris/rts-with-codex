#ifndef STAT_H
#define STAT_H

#include "BalanceStats.h"

#include <istream>
#include <ostream>

// Base value holder for gameplay parameters that can be modified by balance systems.
template <typename T>
class Stat
{
public:
    Stat() = default;
    constexpr Stat(T value) : baseValue(value) {}
    constexpr Stat(BalanceStat stat, T value) : statId(stat), baseValue(value) {}

    // Returns the modifier stat id used by the balance resolver.
    BalanceStat GetStatId() const { return statId; }

    // Updates the modifier stat id for data-driven stat setup.
    void SetStatId(BalanceStat stat) { statId = stat; }

    // Returns the unmodified configured value.
    T GetBase() const { return baseValue; }

    // Replaces the unmodified configured value.
    void SetBase(T value) { baseValue = value; }

    // Assigns a new base value while keeping field-style syntax readable.
    Stat& operator=(T value)
    {
        SetBase(value);
        return *this;
    }

    // Exposes the base value for legacy arithmetic during incremental migration.
    operator T() const { return baseValue; }

private:
    BalanceStat statId{BalanceStat::BuildTime};
    T baseValue{};
};

template <typename T>
// Writes the base value to save files and debug streams.
std::ostream& operator<<(std::ostream& out, const Stat<T>& stat)
{
    out << stat.GetBase();
    return out;
}

template <typename T>
// Reads a base value from save files and data streams.
std::istream& operator>>(std::istream& in, Stat<T>& stat)
{
    T value{};
    in >> value;
    stat.SetBase(value);
    return in;
}

#endif
