#pragma once

#include "gmemmonitor/core/Analysis.h"

namespace gmemmonitor::platform {

class WindowsNotificationService final : public gmemmonitor::core::INotificationService {
public:
    [[nodiscard]] bool Initialize() noexcept;
    [[nodiscard]] bool IsRegistered() const noexcept { return registered_; }
    void Shutdown() noexcept;
    [[nodiscard]] bool Show(const gmemmonitor::core::TokenAlert& alert) override;

private:
    static void OpenValidatedGmgnUrl(winrt::hstring const& url) noexcept;
    bool registered_{};
    winrt::event_token invokedToken_{};
};

// The application owns registration lifetime; this accessor gives the dashboard the
// same notification sink without exposing the Windows API to core code.
WindowsNotificationService& AppNotificationService() noexcept;
[[nodiscard]] bool AppNotificationServiceAvailable() noexcept;

} // namespace gmemmonitor::platform
