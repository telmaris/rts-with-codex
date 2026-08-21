#include "platform/AtomicFile.h"

#include <atomic>
#include <chrono>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace
{
    std::atomic<unsigned long long> sequence{0};

    std::filesystem::path MakeTemporaryPath(const std::filesystem::path& destination)
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto id = sequence.fetch_add(1, std::memory_order_relaxed);
        return destination.string() + ".tmp." + std::to_string(stamp) + "." + std::to_string(id);
    }

    void SetError(std::string* error, const std::string& message)
    {
        if (error != nullptr)
            *error = message;
    }
}

AtomicFileTransaction::AtomicFileTransaction(AtomicFileTransaction&& other) noexcept
    : destination_(std::move(other.destination_)),
      temporary_(std::move(other.temporary_)),
      stream_(std::move(other.stream_)),
      committed_(other.committed_)
{
    other.committed_ = true;
}

AtomicFileTransaction& AtomicFileTransaction::operator=(AtomicFileTransaction&& other) noexcept
{
    if (this == &other)
        return *this;
    Cleanup();
    destination_ = std::move(other.destination_);
    temporary_ = std::move(other.temporary_);
    stream_ = std::move(other.stream_);
    committed_ = other.committed_;
    other.committed_ = true;
    return *this;
}

AtomicFileTransaction::~AtomicFileTransaction()
{
    Cleanup();
}

bool AtomicFileTransaction::Begin(const std::filesystem::path& destination,
                                  AtomicFileTransaction& transaction,
                                  std::string* error)
{
    transaction.Cleanup();
    transaction.destination_ = destination;
    transaction.temporary_ = MakeTemporaryPath(destination);

    std::error_code fsError;
    if (!destination.parent_path().empty())
        std::filesystem::create_directories(destination.parent_path(), fsError);
    if (fsError)
    {
        SetError(error, "cannot create destination directory: " + fsError.message());
        transaction.Cleanup();
        return false;
    }

    transaction.stream_.open(transaction.temporary_, std::ios::binary | std::ios::trunc);
    if (!transaction.stream_.is_open())
    {
        SetError(error, "cannot open temporary file");
        transaction.Cleanup();
        return false;
    }
    transaction.committed_ = false;
    return true;
}

bool AtomicFileTransaction::Commit(std::string* error)
{
    if (committed_ || !stream_.is_open())
    {
        SetError(error, "transaction is not open");
        return false;
    }
    stream_.flush();
    if (!stream_.good())
    {
        SetError(error, "flush failed");
        return false;
    }
    stream_.close();

    std::error_code fsError;
#ifdef _WIN32
    const std::wstring destination = destination_.wstring();
    const std::wstring temporary = temporary_.wstring();
    if (std::filesystem::exists(destination_))
    {
        if (!ReplaceFileW(destination.c_str(), temporary.c_str(), nullptr,
                          REPLACEFILE_WRITE_THROUGH, nullptr, nullptr))
        {
            SetError(error, "ReplaceFileW failed");
            return false;
        }
    }
    else if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                          MOVEFILE_WRITE_THROUGH | MOVEFILE_REPLACE_EXISTING))
    {
        SetError(error, "MoveFileExW failed");
        return false;
    }
#else
    std::filesystem::rename(temporary_, destination_, fsError);
    if (fsError)
    {
        SetError(error, "atomic rename failed: " + fsError.message());
        return false;
    }
#endif
    committed_ = true;
    return true;
}

bool AtomicFileTransaction::Write(const std::filesystem::path& destination,
                                  const std::function<bool(std::ostream&)>& writer,
                                  std::string* error)
{
    AtomicFileTransaction transaction;
    if (!Begin(destination, transaction, error))
        return false;
    if (!writer(transaction.Stream()) || !transaction.Stream().good())
    {
        SetError(error, "write failed");
        return false;
    }
    return transaction.Commit(error);
}

void AtomicFileTransaction::Cleanup() noexcept
{
    if (stream_.is_open())
        stream_.close();
    if (!committed_ && !temporary_.empty())
    {
        std::error_code ignored;
        std::filesystem::remove(temporary_, ignored);
    }
    destination_.clear();
    temporary_.clear();
    committed_ = true;
}
