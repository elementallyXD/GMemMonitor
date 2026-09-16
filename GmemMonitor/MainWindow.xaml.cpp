#include "pch.h"
#include "MainWindow.xaml.h"
#include "ApplicationLog.h"
#include "WindowsNotificationService.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <charconv>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace {
[[nodiscard]] std::string UsdDecimal(const gmemmonitor::core::MoneyUsd value)
{
    const auto whole = value.micros / 1'000'000;
    const auto fraction = value.micros % 1'000'000;
    std::ostringstream text;
    text << whole << '.' << std::setw(6) << std::setfill('0') << fraction;
    std::string result = text.str();
    result.erase(result.find_last_not_of('0') + 1);
    if (!result.empty() && result.back() == '.') result.push_back('0');
    return result;
}

[[nodiscard]] std::optional<std::filesystem::path> LocalAppDataDirectory()
{
    const DWORD needed = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (needed == 0) return std::nullopt;
    std::wstring value(needed, L'\0');
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), needed) == 0) return std::nullopt;
    value.resize(needed - 1);
    return std::filesystem::path(value);
}
}

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace winrt::GmemMonitor::implementation
{
    class DashboardClock final : public gmemmonitor::core::IClock, public gmemmonitor::core::IAlertClock {
    public:
        std::chrono::system_clock::time_point UtcNow() const override { return std::chrono::system_clock::now(); }
        std::chrono::steady_clock::time_point SteadyNow() const override { return std::chrono::steady_clock::now(); }
    };

    MainWindow::MainWindow()
    {
        InitializeComponent();
        LoadSettings();
        ValidateSettings();
        try {
            HWND windowHandle{};
            auto windowNative = this->try_as<::IWindowNative>();
            if (windowNative && SUCCEEDED(windowNative->get_WindowHandle(&windowHandle))) {
                trayIcon_ = std::make_unique<gmemmonitor::platform::TrayIconService>();
                auto weak = get_weak();
                if (!trayIcon_->Initialize(windowHandle,
                    [weak] { if (const auto self = weak.get()) self->OpenFromTray(); },
                    [weak] { if (const auto self = weak.get()) self->ToggleMonitoring(); },
                    [weak] { if (const auto self = weak.get()) self->Close(); },
                    [weak] { if (const auto self = weak.get()) self->SuspendMonitoring(); },
                    [weak] { if (const auto self = weak.get()) self->ResumeStopped(); })) {
                    trayIcon_.reset();
                    gmemmonitor::platform::WriteApplicationLog(gmemmonitor::core::LogLevel::Error, "tray initialization failed");
                }
            }
        } catch (...) {
            trayIcon_.reset();
            gmemmonitor::platform::WriteApplicationLog(gmemmonitor::core::LogLevel::Error, "tray initialization failed");
        }
        auto weak = get_weak();
        Closed([weak](IInspectable const&, WindowEventArgs const&) {
            if (const auto self = weak.get()) {
                gmemmonitor::platform::WriteApplicationLog(gmemmonitor::core::LogLevel::Info, "window close cleanup started");
                self->StopMonitoring(false);
                gmemmonitor::platform::WriteApplicationLog(gmemmonitor::core::LogLevel::Info, "window monitoring cleanup completed");
                if (self->trayIcon_) self->trayIcon_->Shutdown();
                gmemmonitor::platform::WriteApplicationLog(gmemmonitor::core::LogLevel::Info, "window tray cleanup completed");
            }
        });
        if (!gmemmonitor::platform::AppNotificationServiceAvailable()) {
            StatusBar().Severity(Controls::InfoBarSeverity::Error);
            StatusBar().Title(L"Notifications unavailable");
            StatusBar().Message(L"Windows notification registration failed. Monitoring cannot start until notifications are available.");
        }
    }

    MainWindow::~MainWindow()
    {
        // XAML controls may already be torn down when the implementation object
        // is released after Closed. Do not touch presentation state here.
        StopMonitoring(false);
        if (trayIcon_) trayIcon_->Shutdown();
        trayIcon_.reset();
    }

    int32_t MainWindow::MyProperty() { return myProperty_; }
    void MainWindow::MyProperty(int32_t value) { myProperty_ = value; }

    void MainWindow::Settings_TextChanged(IInspectable const&, Controls::TextChangedEventArgs const&)
    {
        ValidateSettings();
    }

    std::optional<gmemmonitor::core::AppSettings> MainWindow::ReadSettings()
    {
        const auto parsePositive = [](hstring const& value) -> std::optional<std::int64_t>
        {
            const std::string text = to_string(value);
            std::int64_t parsed{};
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
            return error == std::errc{} && end == text.data() + text.size() && parsed > 0 ? std::optional{parsed} : std::nullopt;
        };
        const auto poll = parsePositive(PollIntervalBox().Text());
        const auto wallets = parsePositive(WalletThresholdBox().Text());
        const auto aggregationWindow = parsePositive(WindowBox().Text());
        const auto cooldownMinutes = parsePositive(CooldownBox().Text());
        const auto minimum = gmemmonitor::core::MoneyUsd::Parse(to_string(MinimumBuyBox().Text()));
        if (!poll || !wallets || !aggregationWindow || !cooldownMinutes || !minimum || minimum->micros <= 0 ||
            *cooldownMinutes > (std::numeric_limits<std::int64_t>::max)() / 60) return std::nullopt;
        gmemmonitor::core::AppSettings settings{
            std::chrono::seconds{*poll}, *minimum, static_cast<std::size_t>(*wallets),
            std::chrono::seconds{*aggregationWindow}, std::chrono::seconds{*cooldownMinutes * 60}};
        return gmemmonitor::core::ValidateSettings(settings) ? std::nullopt : std::optional{settings};
    }

    void MainWindow::LoadSettings()
    {
        const auto localAppData = LocalAppDataDirectory();
        if (!localAppData) {
            StatusBar().Severity(Controls::InfoBarSeverity::Warning);
            StatusBar().Title(L"Settings unavailable");
            StatusBar().Message(L"Windows did not provide a local application-data folder; defaults are in use.");
            return;
        }
        const gmemmonitor::core::SettingsStore store(*localAppData / L"GMemMonitor" / L"config.json");
        const auto loaded = store.Load();
        PollIntervalBox().Text(to_hstring(loaded.settings.pollInterval.count()));
        MinimumBuyBox().Text(to_hstring(UsdDecimal(loaded.settings.minimumBuyUsd)));
        WalletThresholdBox().Text(to_hstring(loaded.settings.distinctWalletThreshold));
        WindowBox().Text(to_hstring(loaded.settings.aggregationWindow.count()));
        CooldownBox().Text(to_hstring(loaded.settings.notificationCooldown.count() / 60));
        if (loaded.warning) {
            StatusBar().Severity(Controls::InfoBarSeverity::Warning);
            StatusBar().Title(L"Saved settings need attention");
            StatusBar().Message(to_hstring(*loaded.warning));
        }
    }

    void MainWindow::SaveSettings_Click(IInspectable const&, RoutedEventArgs const&)
    {
        const auto settings = ReadSettings();
        if (!settings) { ValidateSettings(); return; }
        const auto localAppData = LocalAppDataDirectory();
        if (!localAppData) {
            StatusBar().Severity(Controls::InfoBarSeverity::Error);
            StatusBar().Title(L"Settings were not saved");
            StatusBar().Message(L"Windows did not provide a local application-data folder.");
            return;
        }
        const gmemmonitor::core::SettingsStore store(*localAppData / L"GMemMonitor" / L"config.json");
        if (const auto error = store.Save(*settings)) {
            StatusBar().Severity(Controls::InfoBarSeverity::Error);
            StatusBar().Title(L"Settings were not saved");
            StatusBar().Message(to_hstring(*error));
            return;
        }
        StatusBar().Severity(Controls::InfoBarSeverity::Success);
        StatusBar().Title(L"Settings saved");
        StatusBar().Message(L"Monitoring remains OFF until it is started explicitly.");
    }

    void MainWindow::StartStop_Click(IInspectable const&, RoutedEventArgs const&)
    {
        ToggleMonitoring();
    }

    void MainWindow::ToggleMonitoring()
    {
        if (session_) { StopMonitoring(); return; }
        const auto settings = ReadSettings();
        if (!settings) { ValidateSettings(); return; }
        if (!gmemmonitor::platform::AppNotificationServiceAvailable()) {
            StatusBar().Severity(Controls::InfoBarSeverity::Error);
            StatusBar().Title(L"Monitoring could not start");
            StatusBar().Message(L"Windows notifications are unavailable. Resolve notification registration before starting monitoring.");
            return;
        }
        wchar_t executable[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (length == 0 || length >= std::size(executable)) {
            StatusBar().Severity(Controls::InfoBarSeverity::Error); StatusBar().Title(L"Monitoring could not start"); StatusBar().Message(L"The application location could not be resolved."); return;
        }
        const auto root = std::filesystem::path(executable).parent_path();
        const gmemmonitor::core::GmgnRuntimePaths runtime{root / L"runtime" / L"node.exe",
            root / L"runtime" / L"gmgn-cli" / L"node_modules" / L"gmgn-cli" / L"dist" / L"index.js"};
        if (!std::filesystem::is_regular_file(runtime.nodeExecutable) || !std::filesystem::is_regular_file(runtime.cliEntry)) {
            StatusBar().Severity(Controls::InfoBarSeverity::Warning); StatusBar().Title(L"Bundled GMGN runtime is missing"); StatusBar().Message(L"Install the pinned runtime beside GMemMonitor before starting monitoring."); return;
        }
        auto client = std::make_shared<gmemmonitor::core::GmgnCliClient>(runtime, std::make_shared<gmemmonitor::core::ProcessRunner>());
        clock_ = std::make_unique<DashboardClock>();
        const auto dispatcher = DispatcherQueue();
        auto weak = get_weak();
        session_ = std::make_unique<gmemmonitor::core::MonitoringSession>(client, gmemmonitor::platform::AppNotificationService(), *clock_, *clock_,
            [dispatcher, weak](const gmemmonitor::core::MonitoringUpdate& update) {
                dispatcher.TryEnqueue([weak, update] { if (const auto self = weak.get()) self->ApplyMonitoringUpdate(update); });
            },
            [dispatcher, weak](const gmemmonitor::core::AnalysisUpdate& update) {
                dispatcher.TryEnqueue([weak, update] { if (const auto self = weak.get()) self->ApplyAnalysisUpdate(update); });
            });
        if (!session_->Start(*settings)) { session_.reset(); clock_.reset(); StatusBar().Severity(Controls::InfoBarSeverity::Error); StatusBar().Title(L"Monitoring could not start"); StatusBar().Message(L"The monitoring session could not be initialized."); return; }
        SetMonitoringControls(true);
        gmemmonitor::platform::WriteApplicationLog(gmemmonitor::core::LogLevel::Info, "monitoring started");
        StatusBar().Severity(Controls::InfoBarSeverity::Informational); StatusBar().Title(L"Authenticating"); StatusBar().Message(L"Checking the external GMGN CLI configuration.");
    }

    void MainWindow::StopMonitoring(const bool updateControls) noexcept
    {
        const bool wasActive = static_cast<bool>(session_);
        if (session_) session_->Stop();
        session_.reset(); clock_.reset();
        if (updateControls) SetMonitoringControls(false);
        if (wasActive) gmemmonitor::platform::WriteApplicationLog(gmemmonitor::core::LogLevel::Info, "monitoring stopped");
    }

    void MainWindow::SetMonitoringControls(const bool active)
    {
        PollIntervalBox().IsEnabled(!active); MinimumBuyBox().IsEnabled(!active); WalletThresholdBox().IsEnabled(!active); WindowBox().IsEnabled(!active); CooldownBox().IsEnabled(!active); SaveSettingsButton().IsEnabled(!active && ReadSettings().has_value());
        StartStopButton().Content(active ? box_value(L"Stop monitoring") : box_value(L"Start monitoring"));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(StartStopButton(), active ? L"Stop monitoring" : L"Start monitoring");
        if (trayIcon_) trayIcon_->SetMonitoring(active);
    }

    void MainWindow::ApplyMonitoringUpdate(const gmemmonitor::core::MonitoringUpdate& update)
    {
        const auto state = update.state == gmemmonitor::core::MonitoringState::Monitoring ? L"Monitoring" : update.state == gmemmonitor::core::MonitoringState::Retrying ? L"Retrying" : update.state == gmemmonitor::core::MonitoringState::AuthenticationRequired ? L"Authentication required" : L"Authenticating";
        StatusBar().Title(state); StatusBar().Message(update.diagnostic.empty() ? L"Monitoring current GMGN follows dynamically." : to_hstring(update.diagnostic));
        if (update.state == gmemmonitor::core::MonitoringState::Monitoring) LastPollText().Text(L"Last successful poll: just now");
        if (!update.diagnostic.empty()) {
            const auto level = update.state == gmemmonitor::core::MonitoringState::AuthenticationRequired
                ? gmemmonitor::core::LogLevel::Error : gmemmonitor::core::LogLevel::Warning;
            gmemmonitor::platform::WriteApplicationLog(level, "monitoring status", update.diagnostic);
        }
        if (update.state == gmemmonitor::core::MonitoringState::AuthenticationRequired) { session_.reset(); clock_.reset(); SetMonitoringControls(false); }
    }

    void MainWindow::ApplyAnalysisUpdate(const gmemmonitor::core::AnalysisUpdate& update)
    {
        if (update.alert) LastTokenText().Text(L"Last analyzed token: " + to_hstring(update.alert->sanitizedSymbol));
        if (update.delivered) {
            gmemmonitor::platform::WriteApplicationLog(gmemmonitor::core::LogLevel::Info, "notification delivered");
        } else if (update.suppressedByCooldown) {
            gmemmonitor::platform::WriteApplicationLog(gmemmonitor::core::LogLevel::Info, "notification suppressed by cooldown");
        } else if (!update.diagnostic.empty()) {
            gmemmonitor::platform::WriteApplicationLog(gmemmonitor::core::LogLevel::Warning, "token analysis error", update.diagnostic);
        }
    }

    void MainWindow::OpenFromTray()
    {
        HWND windowHandle{};
        if (auto windowNative = this->try_as<::IWindowNative>(); windowNative && SUCCEEDED(windowNative->get_WindowHandle(&windowHandle))) {
            ShowWindow(windowHandle, SW_RESTORE);
            SetForegroundWindow(windowHandle);
        }
        Activate();
    }

    void MainWindow::SuspendMonitoring() noexcept
    {
        // WM_POWERBROADCAST is time-sensitive. Signal every worker and return;
        // ResumeStopped (or orderly shutdown) performs the bounded joins.
        if (session_) session_->RequestStop();
        SetMonitoringControls(false);
        StatusBar().Severity(Controls::InfoBarSeverity::Informational);
        StatusBar().Title(L"Monitoring is OFF");
        StatusBar().Message(L"Monitoring stopped for system suspend and will remain OFF after resume.");
        gmemmonitor::platform::WriteApplicationLog(gmemmonitor::core::LogLevel::Info, "system suspend");
    }

    void MainWindow::ResumeStopped() noexcept
    {
        StopMonitoring();
        StatusBar().Severity(Controls::InfoBarSeverity::Informational);
        StatusBar().Title(L"Monitoring is OFF");
        StatusBar().Message(L"Windows resumed. Start monitoring manually when ready.");
        gmemmonitor::platform::WriteApplicationLog(gmemmonitor::core::LogLevel::Info, "system resume; monitoring remains stopped");
    }

    void MainWindow::ValidateSettings()
    {
        const bool valid = ReadSettings().has_value();
        ValidationText().Text(valid ? L"" : L"Enter settings within the supported ranges and a USD value with no more than six decimal places.");
        SaveSettingsButton().IsEnabled(valid);
    }
}
