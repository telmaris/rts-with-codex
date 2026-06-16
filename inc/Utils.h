#ifndef UTILS_H
#define UTILS_H

#include <iostream>
#include <vector>
#include <memory>
#include <chrono>
#include <string>
#include <sstream>
#include <map>
#include <iomanip>
#include <utility>
#include <algorithm>
#include <queue>
#include <array>
#include <functional>
#include <random> //zrobic klase random ktroa bedzie losowac liczby w danym zakresie
#include <type_traits>
#include <fstream>
#include <filesystem>


class Random
{
    public:
    static void normalized(int a, int b)
    {
        std::mt19937 generator (123);
        std::uniform_real_distribution<double> dis(a, b);
        double randomRealBetweenZeroAndOne = dis(generator);
    }
};
class Log
{
public:
    template <typename... Args>
    static void Msg(std::string tag, Args &&...args)
    {
        std::cout << tag << " | ";
        (std::cout << ... << args) << " | " << CurrentTime() << '\n';
        
    }

    static std::string CurrentTime()
    {
        using namespace std::chrono;

        auto now = system_clock::now();
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

        std::time_t t = system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&t);

        std::ostringstream oss;
        oss << std::setfill('0')
            << std::setw(2) << tm.tm_hour << ":"
            << std::setw(2) << tm.tm_min << ":"
            << std::setw(2) << tm.tm_sec << ":"
            << std::setw(3) << ms.count();

        return oss.str();
    }
};

template <typename T>
struct Vec2
{
    Vec2(T px, T py) : x(px), y(py) {}
    Vec2() = default;

    T x{0}, y{0};

    friend std::ostream& operator<<(std::ostream& os, const Vec2& rhs)
    {
        os << "[" << rhs.x << ", " << rhs.y << "]";
        return os;
    }

    bool operator==(const Vec2& rhs) const
    {
        return (x == rhs.x) && (y == rhs.y);
    }

    bool operator!=(const Vec2& rhs) const
    {
        return !(*this == rhs);
    }
};

using Vec2i = Vec2<int>;
using Vec2f = Vec2<float>;

template <typename T>
struct Vec4
{
    Vec4(T px, T py, T pz, T pw) : x(px), y(py), z(pz), w(pw) {}
    Vec4() = default;

    T x{0}, y{0}, z{0}, w{0};
};

using Vec4i = Vec4<int>;
#endif