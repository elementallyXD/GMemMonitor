#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace gmemmonitor::core {

enum class LogLevel { Info, Warning, Error };

// Small bounded diagnostic logger. Callers provide event-level diagnostics only;
// raw GMGN payloads, command environments, wallets, and transaction activity are
// deliberately outside this interface.
class LoggingService final {
public:
    LoggingService(std::filesystem::path path,
                   std::uintmax_t maximumBytes = 2U * 1024U * 1024U,
                   std::size_t retainedFiles = 5);

    [[nodiscard]] bool Initialize() noexcept;
    void Write(LogLevel level, std::string_view event, std::string_view diagnostic = {}) noexcept;
    void Flush() noexcept;

    [[nodiscard]] static std::string Redact(std::string_view value);

private:
    void RotateIfNeeded(std::size_t incomingBytes);
    [[nodiscard]] bool OpenAppend();

    std::filesystem::path path_;
    std::uintmax_t maximumBytes_;
    std::size_t retainedFiles_;
    std::ofstream stream_;
    std::mutex mutex_;
};

} // namespace gmemmonitor::core
