#ifndef CORE_LOG_H
#define CORE_LOG_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <utility>

enum class LogLevel : std::uint8_t
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
};

// Bounded asynchronous logger. The simulation only formats and enqueues an
// already-built line; file I/O and flushing happen on the logger thread.
class Log
{
public:
    using Sink = std::function<void(const std::string&)>;

    static void Initialize();
    static void Shutdown();
    static void SetMinimumLevel(LogLevel level);
    static LogLevel GetMinimumLevel();
    static std::size_t DroppedLowPriorityCount();
    static constexpr std::size_t QueueCapacity() { return 1024; }

    // Test-only sink. It receives complete timestamped lines from the logger
    // worker and avoids making tests depend on the filesystem.
    static void SetTestSink(Sink sink);
    static void ClearTestSink();

    template <typename... Args>
    static void Msg(std::string tag, Args&&... args)
    {
        Write(LogLevel::Info, std::move(tag), std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Debug(std::string tag, Args&&... args)
    {
        Write(LogLevel::Debug, std::move(tag), std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Warning(std::string tag, Args&&... args)
    {
        Write(LogLevel::Warning, std::move(tag), std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Error(std::string tag, Args&&... args)
    {
        Write(LogLevel::Error, std::move(tag), std::forward<Args>(args)...);
    }

#ifndef NDEBUG
    template <typename... Args>
    static void Trace(std::string tag, Args&&... args)
    {
        Write(LogLevel::Trace, std::move(tag), std::forward<Args>(args)...);
    }
#else
    template <typename... Args>
    static void Trace(std::string, Args&&...)
    {
        // Trace call sites are compiled out in Release. In particular this
        // prevents an ostringstream from being created for per-resource logs.
    }
#endif

    template <typename... Args>
    static void Write(LogLevel level, std::string tag, Args&&... args)
    {
        if (!IsEnabled(level))
            return;

        std::ostringstream line;
        line << tag << " | ";
        (line << ... << std::forward<Args>(args));
        WriteLine(level, line.str());
    }

    static std::string CurrentTime();

private:
    static bool IsEnabled(LogLevel level);
    static void WriteLine(LogLevel level, std::string line);
};

#ifndef NDEBUG
#define TVORIN_LOG_TRACE(...) ::Log::Trace(__VA_ARGS__)
#else
#define TVORIN_LOG_TRACE(...) do { } while (false)
#endif

#endif
