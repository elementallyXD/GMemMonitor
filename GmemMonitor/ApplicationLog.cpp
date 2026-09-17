#include "pch.h"
#include "ApplicationLog.h"

#include <filesystem>
#include <mutex>

namespace gmemmonitor::platform {
namespace {
std::mutex g_mutex;
std::unique_ptr<gmemmonitor::core::LoggingService> g_logger;

[[nodiscard]] std::optional<std::filesystem::path> LocalAppDataPath() {
    const DWORD needed = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (needed == 0) return std::nullopt;
    std::wstring value(needed, L'\0');
    const DWORD written = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), needed);
    if (written == 0 || written >= needed) return std::nullopt;
    value.resize(written);
    return std::filesystem::path(value);
}
}

bool InitializeApplicationLog() noexcept {
    try {
        std::scoped_lock lock(g_mutex);
        if (g_logger) return true;
        const auto localAppData = LocalAppDataPath();
        if (!localAppData) return false;
        auto logger = std::make_unique<gmemmonitor::core::LoggingService>(
            *localAppData / L"GMemMonitor" / L"logs" / L"gmemmonitor.log");
        if (!logger->Initialize()) return false;
        g_logger = std::move(logger);
        return true;
    } catch (...) {
        return false;
    }
}

void WriteApplicationLog(const gmemmonitor::core::LogLevel level, const std::string_view event,
                         const std::string_view diagnostic) noexcept {
    std::scoped_lock lock(g_mutex);
    if (g_logger) g_logger->Write(level, event, diagnostic);
}

void ShutdownApplicationLog() noexcept {
    std::scoped_lock lock(g_mutex);
    if (g_logger) {
        g_logger->Write(gmemmonitor::core::LogLevel::Info, "shutdown");
        g_logger->Flush();
        g_logger.reset();
    }
}

} // namespace gmemmonitor::platform
