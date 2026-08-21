#ifndef RTS_ATOMIC_FILE_H
#define RTS_ATOMIC_FILE_H

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

// A single-file commit primitive for user-owned files. The temporary is kept
// beside the destination so a replace cannot cross volumes; an uncommitted
// transaction removes it in the destructor.
class AtomicFileTransaction
{
public:
    AtomicFileTransaction() = default;
    AtomicFileTransaction(const AtomicFileTransaction&) = delete;
    AtomicFileTransaction& operator=(const AtomicFileTransaction&) = delete;
    AtomicFileTransaction(AtomicFileTransaction&& other) noexcept;
    AtomicFileTransaction& operator=(AtomicFileTransaction&& other) noexcept;
    ~AtomicFileTransaction();

    static bool Begin(const std::filesystem::path& destination,
                      AtomicFileTransaction& transaction,
                      std::string* error = nullptr);

    std::ostream& Stream() { return stream_; }
    bool Commit(std::string* error = nullptr);
    const std::filesystem::path& TemporaryPath() const { return temporary_; }

    static bool Write(const std::filesystem::path& destination,
                      const std::function<bool(std::ostream&)>& writer,
                      std::string* error = nullptr);

private:
    void Cleanup() noexcept;

    std::filesystem::path destination_;
    std::filesystem::path temporary_;
    std::ofstream stream_;
    bool committed_{false};
};

#endif
