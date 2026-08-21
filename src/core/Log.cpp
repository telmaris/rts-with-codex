#include "core/Log.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace
{
    struct LoggerState
    {
        std::mutex mutex;
        std::mutex outputMutex;
        std::condition_variable condition;
        std::deque<std::pair<LogLevel, std::string>> queue;
        std::jthread worker;
        std::ofstream file;
        Log::Sink testSink;
        LogLevel minimumLevel{LogLevel::Info};
        std::size_t droppedLowPriority{0};
        bool stopping{false};
        bool running{false};
    };

    LoggerState& GetState()
    {
        // The state intentionally lives until process exit. Explicit
        // Shutdown controls the worker/file lifetime without depending on
        // destruction order between unrelated static objects.
        static auto* state = new LoggerState();
        return *state;
    }

    bool IsAtLeast(LogLevel value, LogLevel minimum)
    {
        return static_cast<int>(value) >= static_cast<int>(minimum);
    }

    const char* LevelName(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::Trace: return "TRACE";
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Info: return "INFO";
            case LogLevel::Warning: return "WARNING";
            case LogLevel::Error: return "ERROR";
        }
        return "INFO";
    }

    void EmitLine(LoggerState& state, LogLevel level, const std::string& message)
    {
        Log::Sink sink;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            sink = state.testSink;
        }

        std::ostringstream line;
        line << Log::CurrentTime() << " | " << LevelName(level) << " | " << message;
        const std::string complete = line.str();

        if (sink)
            sink(complete);
        if (state.file.is_open())
            state.file << complete << '\n';
    }

    void RunWorker(LoggerState& state)
    {
        for (;;)
        {
            std::pair<LogLevel, std::string> entry;
            {
                std::unique_lock<std::mutex> lock(state.mutex);
                state.condition.wait(lock, [&]()
                {
                    return state.stopping || !state.queue.empty();
                });
                if (state.queue.empty() && state.stopping)
                    break;
                entry = std::move(state.queue.front());
                state.queue.pop_front();
            }

            std::lock_guard<std::mutex> lock(state.outputMutex);
            EmitLine(state, entry.first, entry.second);
        }

        std::lock_guard<std::mutex> lock(state.outputMutex);
        if (state.file.is_open())
            state.file.flush();
    }

    void StartWorker(LoggerState& state)
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.running)
            return;

        std::error_code error;
        std::filesystem::create_directories("logs", error);
        state.file.open("logs/tvorin.log", std::ios::out | std::ios::app);
        state.stopping = false;
        state.running = true;
        state.worker = std::jthread([&state]() { RunWorker(state); });
    }
}

void Log::Initialize()
{
    StartWorker(GetState());
}

void Log::Shutdown()
{
    LoggerState& state = GetState();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.running)
            return;
        state.stopping = true;
    }
    state.condition.notify_one();
    if (state.worker.joinable())
        state.worker.join();

    std::lock_guard<std::mutex> lock(state.outputMutex);
    if (state.file.is_open())
    {
        state.file.flush();
        state.file.close();
    }
    state.running = false;
    state.stopping = false;
}

void Log::SetMinimumLevel(LogLevel level)
{
    LoggerState& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.minimumLevel = level;
}

LogLevel Log::GetMinimumLevel()
{
    LoggerState& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.minimumLevel;
}

std::size_t Log::DroppedLowPriorityCount()
{
    LoggerState& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.droppedLowPriority;
}

void Log::SetTestSink(Sink sink)
{
    LoggerState& state = GetState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.testSink = std::move(sink);
}

void Log::ClearTestSink()
{
    SetTestSink({});
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

bool Log::IsEnabled(LogLevel level)
{
    LoggerState& state = GetState();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!IsAtLeast(level, state.minimumLevel))
            return false;
    }
    Initialize();
    return true;
}

void Log::WriteLine(LogLevel level, std::string line)
{
    LoggerState& state = GetState();
    Initialize();

    std::unique_lock<std::mutex> lock(state.mutex);
    if (state.queue.size() < QueueCapacity())
    {
        state.queue.emplace_back(level, std::move(line));
        lock.unlock();
        state.condition.notify_one();
        return;
    }

    if (level == LogLevel::Trace || level == LogLevel::Debug)
    {
        ++state.droppedLowPriority;
        return;
    }

    // Warnings and errors are rare and must not disappear when the queue is
    // full, so they use the bounded synchronous fallback sink.
    lock.unlock();
    std::lock_guard<std::mutex> outputLock(state.outputMutex);
    EmitLine(state, level, line);
}
