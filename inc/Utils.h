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
#include <random>
#include <type_traits>
#include <fstream>
#include <filesystem>
#include <limits>
#include <mutex>

constexpr int TILE_SIZE = 32;

// Simple stdout logger with timestamped messages.
class Log
{
public:
    // Writes one tagged log line with all arguments streamed in order.
    template <typename... Args>
    static void Msg(std::string tag, Args &&...args)
    {
        std::lock_guard<std::mutex> lock(GetMutex());
        std::ostringstream line;
        line << tag << " | ";
        (line << ... << args);
        line << " | " << CurrentTime();

        std::cout << line.str() << '\n';
        auto& file = GetFile();
        if (file.is_open())
        {
            file << line.str() << '\n';
            file.flush();
        }
    }

    // Returns local time formatted for log messages.
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

private:
    static std::ofstream& GetFile()
    {
        static std::ofstream file = []()
        {
            std::error_code error;
            std::filesystem::create_directories("logs", error);
            return std::ofstream("logs/rts.log", std::ios::out | std::ios::trunc);
        }();
        return file;
    }

    static std::mutex& GetMutex()
    {
        static std::mutex mutex;
        return mutex;
    }
};

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
