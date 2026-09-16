#include "pch.h"
#include "TrayIconService.h"

#include <powrprof.h>

namespace gmemmonitor::platform {
namespace {
constexpr UINT kTrayMessage = WM_APP + 37;
constexpr UINT kOpenCommand = 1;
constexpr UINT kToggleCommand = 2;
constexpr UINT kExitCommand = 3;
constexpr wchar_t kServiceProperty[] = L"GMemMonitor.TrayIconService";
constexpr GUID kTrayGuid{0x65c51fe8, 0xcc7d, 0x431f, {0x8b, 0x19, 0x8a, 0x5a, 0x76, 0x18, 0x9d, 0x44}};

[[nodiscard]] NOTIFYICONDATAW IconData(const HWND window) noexcept {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = window;
    data.uFlags = NIF_GUID;
    data.guidItem = kTrayGuid;
    return data;
}
}

TrayIconService::~TrayIconService() { Shutdown(); }

bool TrayIconService::Initialize(const HWND window, Command open, Command toggleMonitoring,
                                 Command exit, Command suspend, Command resume) noexcept {
    if (!window || window_) return false;
    window_ = window;
    open_ = std::move(open);
    toggleMonitoring_ = std::move(toggleMonitoring);
    exit_ = std::move(exit);
    suspend_ = std::move(suspend);
    resume_ = std::move(resume);
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (!SetPropW(window_, kServiceProperty, this)) { window_ = nullptr; return false; }
    SetLastError(ERROR_SUCCESS);
    previousProcedure_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProcedure)));
    if (!previousProcedure_ && GetLastError() != ERROR_SUCCESS) {
        RemovePropW(window_, kServiceProperty);
        window_ = nullptr;
        return false;
    }
    if (!AddIcon()) {
        Shutdown();
        return false;
    }
    return true;
}

bool TrayIconService::AddIcon() noexcept {
    if (!window_) return false;
    auto data = IconData(window_);
    data.uFlags |= NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kTrayMessage;
    data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    static_cast<void>(wcscpy_s(data.szTip, L"GMemMonitor"));
    if (!Shell_NotifyIconW(NIM_ADD, &data)) return false;
    data.uVersion = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIconW(NIM_SETVERSION, &data)) {
        static_cast<void>(Shell_NotifyIconW(NIM_DELETE, &data));
        return false;
    }
    iconAdded_ = true;
    return true;
}

void TrayIconService::SetMonitoring(const bool monitoring) noexcept { monitoring_ = monitoring; }

void TrayIconService::ShowMenu() noexcept {
    if (!window_) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    static_cast<void>(AppendMenuW(menu, MF_STRING, kOpenCommand, L"Open"));
    static_cast<void>(AppendMenuW(menu, MF_STRING, kToggleCommand,
        monitoring_ ? L"Stop Monitoring" : L"Start Monitoring"));
    static_cast<void>(AppendMenuW(menu, MF_SEPARATOR, 0, nullptr));
    static_cast<void>(AppendMenuW(menu, MF_STRING, kExitCommand, L"Exit"));
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(window_);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
        point.x, point.y, 0, window_, nullptr);
    DestroyMenu(menu);
    if (command == kOpenCommand && open_) open_();
    else if (command == kToggleCommand && toggleMonitoring_) toggleMonitoring_();
    else if (command == kExitCommand && exit_) exit_();
}

LRESULT TrayIconService::HandleMessage(const UINT message, const WPARAM wParam, const LPARAM lParam) noexcept {
    if (taskbarCreatedMessage_ != 0 && message == taskbarCreatedMessage_) {
        iconAdded_ = false;
        static_cast<void>(AddIcon());
        return 0;
    }
    if (message == kTrayMessage) {
        const UINT event = LOWORD(lParam);
        if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP) ShowMenu();
        else if (event == WM_LBUTTONDBLCLK || event == NIN_SELECT || event == NIN_KEYSELECT) {
            if (open_) open_();
        }
        return 0;
    }
    if (message == WM_SIZE && wParam == SIZE_MINIMIZED) {
        const LRESULT result = CallWindowProcW(previousProcedure_, window_, message, wParam, lParam);
        ShowWindow(window_, SW_HIDE);
        return result;
    }
    if (message == WM_POWERBROADCAST) {
        if (wParam == PBT_APMSUSPEND && suspend_) suspend_();
        else if (wParam == PBT_APMRESUMEAUTOMATIC && resume_) resume_();
    }
    return CallWindowProcW(previousProcedure_, window_, message, wParam, lParam);
}

LRESULT CALLBACK TrayIconService::WindowProcedure(const HWND window, const UINT message,
                                                   const WPARAM wParam, const LPARAM lParam) noexcept {
    const auto service = static_cast<TrayIconService*>(GetPropW(window, kServiceProperty));
    if (!service || !service->previousProcedure_) return DefWindowProcW(window, message, wParam, lParam);
    return service->HandleMessage(message, wParam, lParam);
}

void TrayIconService::Shutdown() noexcept {
    if (!window_) return;
    if (iconAdded_) {
        auto data = IconData(window_);
        static_cast<void>(Shell_NotifyIconW(NIM_DELETE, &data));
        iconAdded_ = false;
    }
    if (previousProcedure_ && IsWindow(window_)) {
        SetWindowLongPtrW(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previousProcedure_));
        previousProcedure_ = nullptr;
    }
    if (IsWindow(window_)) RemovePropW(window_, kServiceProperty);
    window_ = nullptr;
}

} // namespace gmemmonitor::platform
