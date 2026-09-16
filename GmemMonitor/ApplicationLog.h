#pragma once

#include "gmemmonitor/core/Logging.h"

namespace gmemmonitor::platform {

[[nodiscard]] bool InitializeApplicationLog() noexcept;
void WriteApplicationLog(gmemmonitor::core::LogLevel level, std::string_view event,
                         std::string_view diagnostic = {}) noexcept;
void ShutdownApplicationLog() noexcept;

} // namespace gmemmonitor::platform
