#ifndef CORE_TYPES_H
#define CORE_TYPES_H

#include <ostream>

constexpr int TILE_SIZE = 32;

// Two-component value type used for integer and floating point coordinates.
template <typename T>
struct Vec2
{
    Vec2(T px, T py) : x(px), y(py) {}
    Vec2() = default;

    T x{0}, y{0};

    // Streams this vector in a compact debug format.
    friend std::ostream& operator<<(std::ostream& os, const Vec2& rhs)
    {
        os << "[" << rhs.x << ", " << rhs.y << "]";
        return os;
    }

    // Returns true when both coordinates match.
    bool operator==(const Vec2& rhs) const
    {
        return (x == rhs.x) && (y == rhs.y);
    }

    // Returns true when any coordinate differs.
    bool operator!=(const Vec2& rhs) const
    {
        return !(*this == rhs);
    }
};

using Vec2i = Vec2<int>;
using Vec2f = Vec2<float>;

// Four-component value type used for margins and rectangle-like data.
template <typename T>
struct Vec4
{
    Vec4(T px, T py, T pz, T pw) : x(px), y(py), z(pz), w(pw) {}
    Vec4() = default;

    T x{0}, y{0}, z{0}, w{0};
};

using Vec4i = Vec4<int>;

#endif
