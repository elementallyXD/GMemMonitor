#include "gmemmonitor/core/Logging.h"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <system_error>

namespace gmemmonitor::core {
namespace {

[[nodiscard]] std::string LowerAscii(std::string_view value) {
    std::string lowered(value);
    for (char& character : lowered) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return lowered;
}

[[nodiscard]] const char* LevelText(const LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}

[[nodiscard]] std::string TimestampUtc() {
    SYSTEMTIME time{};
    GetSystemTime(&time);
    char value[32]{};
    static_cast<void>(std::snprintf(value, sizeof(value), "%04u-%02u-%02uT%02u:%02u:%02uZ",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond));
    return value;
}

[[nodiscard]] std::filesystem::path RotatedPath(const std::filesystem::path& path, const std::size_t index) {
    return std::filesystem::path(path.wstring() + L"." + std::to_wstring(index));
}

} // namespace

LoggingService::LoggingService(std::filesystem::path path, const std::uintmax_t maximumBytes,
                               const std::size_t retainedFiles)
    : path_(std::move(path)), maximumBytes_(maximumBytes), retainedFiles_((std::max)(retainedFiles, std::size_t{1})) {}

bool LoggingService::Initialize() noexcept {
    try {
        std::scoped_lock lock(mutex_);
        std::error_code error;
        std::filesystem::create_directories(path_.parent_path(), error);
        return !error && OpenAppend();
    } catch (...) {
        return false;
    }
}

std::string LoggingService::Redact(const std::string_view value) {
    constexpr std::size_t kMaximumDiagnosticBytes = 512;
    const std::string lowered = LowerAscii(value);
    constexpr std::string_view sensitiveMarkers[] = {
        "gmgn_api_key", "gmgn_private_key", "api-key", "api_key", "private key",
        "begin private", "x-apikey", "x-signature", "authorization:"
    };
    for (const auto marker : sensitiveMarkers) {
        if (lowered.find(marker) != std::string::npos) return "[redacted diagnostic]";
    }

    std::string result;
    result.reserve((std::min)(value.size(), kMaximumDiagnosticBytes));
    for (const unsigned char character : value) {
        if (result.size() == kMaximumDiagnosticBytes) break;
        if (character == '\r' || character == '\n' || character == '\t') result.push_back(' ');
        else if (character >= 0x20U && character != 0x7FU) result.push_back(static_cast<char>(character));
    }
    return result;
}

void LoggingService::Write(const LogLevel level, const std::string_view event,
                           const std::string_view diagnostic) noexcept {
    try {
        const std::string safeEvent = Redact(event);
        const std::string safeDiagnostic = Redact(diagnostic);
        std::string line = TimestampUtc() + " [" + LevelText(level) + "] " + safeEvent;
        if (!safeDiagnostic.empty()) line += ": " + safeDiagnostic;
        line.push_back('\n');

        std::scoped_lock lock(mutex_);
        if (!stream_.is_open() && !OpenAppend()) return;
        RotateIfNeeded(line.size());
        if (!stream_.is_open() && !OpenAppend()) return;
        stream_.write(line.data(), static_cast<std::streamsize>(line.size()));
        stream_.flush();
    } catch (...) {
        // Diagnostics must never destabilize monitoring or shutdown.
    }
}

void LoggingService::Flush() noexcept {
    try {
        std::scoped_lock lock(mutex_);
        if (stream_.is_open()) stream_.flush();
    } catch (...) {
    }
}

bool LoggingService::OpenAppend() {
    stream_.open(path_, std::ios::binary | std::ios::app);
    return stream_.is_open();
}

void LoggingService::RotateIfNeeded(const std::size_t incomingBytes) {
    std::error_code error;
    const auto existingBytes = std::filesystem::exists(path_, error) && !error
        ? std::filesystem::file_size(path_, error) : 0U;
    if (error || existingBytes + incomingBytes <= maximumBytes_) return;

    stream_.close();
    if (retainedFiles_ > 1) {
        std::filesystem::remove(RotatedPath(path_, retainedFiles_ - 1), error);
        error.clear();
        for (std::size_t index = retainedFiles_ - 1; index > 1; --index) {
            const auto source = RotatedPath(path_, index - 1);
            const auto destination = RotatedPath(path_, index);
            if (std::filesystem::exists(source, error) && !error) {
                std::filesystem::rename(source, destination, error);
            }
            error.clear();
        }
        if (std::filesystem::exists(path_, error) && !error) {
            std::filesystem::rename(path_, RotatedPath(path_, 1), error);
        }
    } else {
        std::filesystem::remove(path_, error);
    }
    stream_.clear();
}

} // namespace gmemmonitor::core
