#include "pch.h"
#include "MainWindow.xaml.h"
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
    }

    MainWindow::~MainWindow() { StopMonitoring(); }

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
            *wallets > static_cast<std::int64_t>((std::numeric_limits<std::size_t>::max)()) ||
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
        if (session_) { StopMonitoring(); return; }
        const auto settings = ReadSettings();
        if (!settings) { ValidateSettings(); return; }
        wchar_t executable[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (length == 0 || length >= std::size(executable)) {
            StatusBar().Severity(Controls::InfoBarSeverity::Error); StatusBar().Title(L"Monitoring could not start"); StatusBar().Message(L"The application location could not be resolved."); return;
        }
        const auto root = std::filesystem::path(executable).parent_path();
        const gmemmonitor::core::GmgnRuntimePaths runtime{root / L"runtime" / L"node.exe", root / L"runtime" / L"gmgn-cli" / L"dist" / L"index.js"};
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
            [dispatcher, weak](const gmemmonitor::core::AnalysisUpdate&) {
                dispatcher.TryEnqueue([weak] { if (const auto self = weak.get()) self->LastTokenText().Text(L"Last analyzed token: enriched alert processed"); });
            });
        if (!session_->Start(*settings)) { session_.reset(); clock_.reset(); StatusBar().Severity(Controls::InfoBarSeverity::Error); StatusBar().Title(L"Monitoring could not start"); StatusBar().Message(L"The monitoring session could not be initialized."); return; }
        SetMonitoringControls(true);
        StatusBar().Severity(Controls::InfoBarSeverity::Informational); StatusBar().Title(L"Authenticating"); StatusBar().Message(L"Checking the external GMGN CLI configuration.");
    }

    void MainWindow::StopMonitoring() noexcept
    {
        if (session_) session_->Stop();
        session_.reset(); clock_.reset();
        SetMonitoringControls(false);
    }

    void MainWindow::SetMonitoringControls(const bool active)
    {
        PollIntervalBox().IsEnabled(!active); MinimumBuyBox().IsEnabled(!active); WalletThresholdBox().IsEnabled(!active); WindowBox().IsEnabled(!active); CooldownBox().IsEnabled(!active); SaveSettingsButton().IsEnabled(!active && ReadSettings().has_value());
        StartStopButton().Content(active ? box_value(L"Stop monitoring") : box_value(L"Start monitoring"));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(StartStopButton(), active ? L"Stop monitoring" : L"Start monitoring");
    }

    void MainWindow::ApplyMonitoringUpdate(const gmemmonitor::core::MonitoringUpdate& update)
    {
        const auto state = update.state == gmemmonitor::core::MonitoringState::Monitoring ? L"Monitoring" : update.state == gmemmonitor::core::MonitoringState::Retrying ? L"Retrying" : update.state == gmemmonitor::core::MonitoringState::AuthenticationRequired ? L"Authentication required" : L"Authenticating";
        StatusBar().Title(state); StatusBar().Message(update.diagnostic.empty() ? L"Monitoring current GMGN follows dynamically." : to_hstring(update.diagnostic));
        if (update.state == gmemmonitor::core::MonitoringState::Monitoring) LastPollText().Text(L"Last successful poll: just now");
        if (update.state == gmemmonitor::core::MonitoringState::AuthenticationRequired) { session_.reset(); clock_.reset(); SetMonitoringControls(false); }
    }

    void MainWindow::ValidateSettings()
    {
        const bool valid = ReadSettings().has_value();
        ValidationText().Text(valid ? L"" : L"Enter settings within the supported ranges and a USD value with no more than six decimal places.");
        SaveSettingsButton().IsEnabled(valid);
    }
}
