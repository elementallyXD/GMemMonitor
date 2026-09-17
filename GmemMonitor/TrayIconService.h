#pragma once

#include <functional>

namespace gmemmonitor::platform {

class TrayIconService final {
public:
    using Command = std::function<void()>;

    TrayIconService() = default;
    ~TrayIconService();
    TrayIconService(const TrayIconService&) = delete;
    TrayIconService& operator=(const TrayIconService&) = delete;

    [[nodiscard]] bool Initialize(HWND window, Command open, Command toggleMonitoring,
                                  Command exit, Command suspend, Command resume) noexcept;
    void SetMonitoring(bool monitoring) noexcept;
    void Shutdown() noexcept;

private:
    [[nodiscard]] bool AddIcon() noexcept;
    void ShowMenu() noexcept;
    [[nodiscard]] LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) noexcept;
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) noexcept;

    HWND window_{};
    WNDPROC previousProcedure_{};
    UINT taskbarCreatedMessage_{};
    bool monitoring_{};
    bool iconAdded_{};
    Command open_;
    Command toggleMonitoring_;
    Command exit_;
    Command suspend_;
    Command resume_;
};

} // namespace gmemmonitor::platform
