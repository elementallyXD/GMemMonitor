#pragma once

#include "MainWindow.g.h"

namespace winrt::GmemMonitor::implementation
{
    class DashboardClock;
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        ~MainWindow();
        int32_t MyProperty();
        void MyProperty(int32_t value);
        void Settings_TextChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
        void SaveSettings_Click(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
        void StartStop_Click(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    private:
        [[nodiscard]] std::optional<gmemmonitor::core::AppSettings> ReadSettings();
        void LoadSettings();
        void ValidateSettings();
        void SetMonitoringControls(bool active);
        void ApplyMonitoringUpdate(gmemmonitor::core::MonitoringUpdate const& update);
        void StopMonitoring() noexcept;
        int32_t myProperty_{};
        std::unique_ptr<DashboardClock> clock_;
        std::unique_ptr<gmemmonitor::core::MonitoringSession> session_;
    };
}

namespace winrt::GmemMonitor::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
