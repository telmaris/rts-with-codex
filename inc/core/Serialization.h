#ifndef SERIALIZATION_H
#define SERIALIZATION_H

#include <cstdint>
#include <string>
#include <sstream>
#include <vector>
#include <iomanip>
#include "core/SerializationVersions.h"

// Archive pattern: single serialize() function works in both write and read directions.
// All types use the same version numbering and format.

// Archive: bidirectional serialization (write to string, read from string)
// Usage:
//   Archive ar(SerializationVersion::GameCommandVersion);
//   ar << field1 << field2 << field3;
//   std::string payload = ar.GetString();
//
// Or for reading:
//   Archive ar(payload, expectedVersion);
//   ar >> field1 >> field2 >> field3;
class Archive
{
public:
    enum class Mode { Write, Read };

    // Write mode: construct empty, then operator<< to add fields
    explicit Archive(int version) : mode(Mode::Write), version(version)
    {
        stream << version; // First field is always the version
    }

    // Read mode: construct from payload
    Archive(const std::string& payload, int expectedVersion)
        : mode(Mode::Read), version(expectedVersion), stream(payload), isValid(true)
    {
        // Parse version from first token
        int readVersion = 0;
        if (!(stream >> readVersion))
        {
            isValid = false;
            return;
        }
        // Version mismatch should be handled by caller
        if (readVersion != expectedVersion)
        {
            isValid = false;
        }
    }

    // Write a field
    template<typename T>
    Archive& operator<<(const T& value)
    {
        if (mode == Mode::Write)
        {
            if (!stream.str().empty()) stream << ' ';
            SerializeValue(stream, value);
        }
        return *this;
    }

    // Read a field
    template<typename T>
    Archive& operator>>(T& value)
    {
        if (mode == Mode::Read && isValid)
        {
            if (!(stream >> value))
            {
                isValid = false;
            }
        }
        return *this;
    }

    // Specialization for std::string to handle quoted strings
    Archive& operator>>(std::string& value)
    {
        if (mode == Mode::Read && isValid)
        {
            if (!(stream >> std::quoted(value)))
            {
                isValid = false;
            }
        }
        return *this;
    }

    std::string GetString() const { return stream.str(); }
    bool IsValid() const { return isValid; }
    // A decoder must consume the complete payload. This rejects both trailing
    // fields and accidental concatenation of two messages.
    bool AtEnd()
    {
        if (mode != Mode::Read || !isValid)
            return false;
        stream >> std::ws;
        return stream.eof();
    }
    int GetVersion() const { return version; }
    Mode GetMode() const { return mode; }

private:
    void SerializeValue(std::stringstream& ss, int value) { ss << value; }
    void SerializeValue(std::stringstream& ss, uint64_t value) { ss << value; }
    void SerializeValue(std::stringstream& ss, double value) { ss << value; }
    void SerializeValue(std::stringstream& ss, bool value) { ss << (value ? 1 : 0); }
    void SerializeValue(std::stringstream& ss, const std::string& value)
    {
        // Quote strings with spaces/special chars
        ss << std::quoted(value);
    }

    Mode mode;
    int version;
    std::stringstream stream;
    bool isValid = true;
};

#endif
