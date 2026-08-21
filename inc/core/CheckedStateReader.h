#ifndef CHECKED_STATE_READER_H
#define CHECKED_STATE_READER_H

#include <cmath>
#include <iomanip>
#include <limits>
#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <utility>

struct StateReadError
{
    std::string section;
    std::string message;
};

template <typename T>
class StateReadResult
{
public:
    StateReadResult(T value) : value_(std::move(value)) {}
    StateReadResult(StateReadError error) : value_(std::move(error)) {}

    bool HasValue() const { return std::holds_alternative<T>(value_); }
    const T& Value() const { return std::get<T>(value_); }
    T& Value() { return std::get<T>(value_); }
    const StateReadError& Error() const { return std::get<StateReadError>(value_); }

private:
    std::variant<T, StateReadError> value_;
};

class CheckedStateReader
{
public:
    explicit CheckedStateReader(std::string_view payload, std::string section = "state")
        : stream_(std::string(payload)), section_(std::move(section)) {}

    StateReadResult<std::string> ReadTag(std::string_view expected);
    StateReadResult<std::int64_t> ReadInt(std::int64_t minimum, std::int64_t maximum);
    StateReadResult<std::size_t> ReadCount(std::size_t maximum);
    StateReadResult<double> ReadFiniteDouble(double minimum, double maximum);
    StateReadResult<std::string> ReadQuotedString(std::size_t maxBytes);

    template <typename Enum>
    StateReadResult<Enum> ReadEnum(const std::function<bool(int)>& isValid)
    {
        auto value = ReadInt(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        if (!value.HasValue())
            return value.Error();
        const int raw = static_cast<int>(value.Value());
        if (!isValid(raw))
            return MakeError("enum value out of domain");
        return static_cast<Enum>(raw);
    }

    bool AtEnd();
    const std::string& Section() const { return section_; }

private:
    StateReadError MakeError(std::string message) const;
    bool ReadToken(std::string& token);

    std::istringstream stream_;
    std::string section_;
};

#endif
