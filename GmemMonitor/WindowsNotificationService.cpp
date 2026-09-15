#include "pch.h"
#include "WindowsNotificationService.h"

namespace gmemmonitor::platform {

namespace {

[[nodiscard]] std::string NotificationRiskSummary(const std::vector<gmemmonitor::core::RiskFact>& risks)
{
    constexpr std::size_t maximumLength = 360;
    std::string result;
    for (const auto& risk : risks) {
        const auto label = gmemmonitor::core::SanitizeDisplayText(risk.label, 48);
        const auto value = gmemmonitor::core::SanitizeDisplayText(risk.value, 80);
        if (label.empty() || value.empty()) continue;
        const std::string item = label + ": " + value;
        const std::size_t separatorLength = result.empty() ? 0 : 2;
        if (item.size() + separatorLength > maximumLength - result.size()) break;
        if (!result.empty()) result += "; ";
        result += item;
    }
    return result;
}

} // namespace

WindowsNotificationService& AppNotificationService() noexcept
{
    static WindowsNotificationService service;
    return service;
}

bool AppNotificationServiceAvailable() noexcept
{
    return AppNotificationService().IsRegistered();
}

bool WindowsNotificationService::Initialize() noexcept
{
    if (registered_) return true;
    try {
        auto const manager = winrt::Microsoft::Windows::AppNotifications::AppNotificationManager::Default();
        invokedToken_ = manager.NotificationInvoked([this](auto const&, auto const& arguments) {
            const auto values = arguments.Arguments();
            const auto action = values.TryLookup(L"action");
            const auto url = values.TryLookup(L"url");
            if (action && url && *action == L"open_gmgn") OpenValidatedGmgnUrl(*url);
        });
        manager.Register();
        registered_ = true;
        return true;
    } catch (...) {
        return false;
    }
}

void WindowsNotificationService::Shutdown() noexcept
{
    if (!registered_) return;
    try {
        auto const manager = winrt::Microsoft::Windows::AppNotifications::AppNotificationManager::Default();
        manager.NotificationInvoked(invokedToken_);
        manager.Unregister();
    } catch (...) {
        // Shutdown must continue even when Windows has already removed registration.
    }
    registered_ = false;
}

void WindowsNotificationService::OpenValidatedGmgnUrl(winrt::hstring const& url) noexcept
{
    const auto validated = gmemmonitor::core::ValidateGmgnUrl(winrt::to_string(url));
    if (!validated) return;
    const std::wstring launchUrl(validated->begin(), validated->end());
    const auto result = ShellExecuteW(nullptr, L"open", launchUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    static_cast<void>(result);
}

bool WindowsNotificationService::Show(const gmemmonitor::core::TokenAlert& alert)
{
    if (!registered_) return false;
    try {
        using namespace winrt::Microsoft::Windows::AppNotifications;
        using namespace winrt::Microsoft::Windows::AppNotifications::Builder;
        auto builder = AppNotificationBuilder();
        builder.AddText(winrt::to_hstring(alert.title));
        builder.AddText(winrt::to_hstring(alert.body));
        if (const auto risks = NotificationRiskSummary(alert.risks); !risks.empty()) {
            builder.AddText(winrt::to_hstring(risks));
        }
        if (!alert.validatedGmgnUrl.empty()) {
            auto button = AppNotificationButton(L"Open in GMGN");
            button.AddArgument(L"action", L"open_gmgn");
            button.AddArgument(L"url", winrt::to_hstring(alert.validatedGmgnUrl));
            builder.AddButton(button);
        }
        AppNotificationManager::Default().Show(builder.BuildNotification());
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace gmemmonitor::platform
