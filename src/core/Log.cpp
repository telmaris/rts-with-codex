#include "core/Log.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>

namespace
{
    std::ofstream& GetLogFile()
    {
        static std::ofstream file = []()
        {
            std::error_code error;
            std::filesystem::create_directories("logs", error);
            return std::ofstream("logs/rts.log", std::ios::out | std::ios::trunc);
        }();
        return file;
    }

    std::mutex& GetLogMutex()
    {
        static std::mutex mutex;
        return mutex;
    }
}

std::string Log::CurrentTime()
{
    using namespace std::chrono;

    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t timestamp = system_clock::to_time_t(now);
    std::tm localTime{};

#ifdef _WIN32
    localtime_s(&localTime, &timestamp);
#else
    localtime_r(&timestamp, &localTime);
#endif

    std::ostringstream output;
    output << std::setfill('0')
           << std::setw(2) << localTime.tm_hour << ":"
           << std::setw(2) << localTime.tm_min << ":"
           << std::setw(2) << localTime.tm_sec << ":"
           << std::setw(3) << ms.count();
    return output.str();
}

void Log::WriteLine(std::string line)
{
    std::lock_guard<std::mutex> lock(GetLogMutex());
    line += " | " + CurrentTime();

    std::cout << line << '\n';
    auto& file = GetLogFile();
    if (file.is_open())
    {
        file << line << '\n';
        file.flush();
    }
}
