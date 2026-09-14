#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace winrt::GmemMonitor::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();
        ValidateSettings();
    }

    int32_t MainWindow::MyProperty() { return myProperty_; }
    void MainWindow::MyProperty(int32_t value) { myProperty_ = value; }

    void MainWindow::Settings_TextChanged(IInspectable const&, Controls::TextChangedEventArgs const&)
    {
        ValidateSettings();
    }

    void MainWindow::ValidateSettings()
    {
        const auto isPositiveInteger = [](hstring const& value)
        {
            try { return std::stoll(value.c_str()) > 0; }
            catch (...) { return false; }
        };
        const auto minimum = gmemmonitor::core::MoneyUsd::Parse(to_string(MinimumBuyBox().Text()));
        const bool valid = isPositiveInteger(PollIntervalBox().Text()) && isPositiveInteger(WalletThresholdBox().Text()) &&
                           isPositiveInteger(WindowBox().Text()) && isPositiveInteger(CooldownBox().Text()) && minimum && minimum->micros > 0;
        ValidationText().Text(valid ? L"" : L"Enter positive whole-number intervals and a valid USD value with no more than six decimal places.");
    }
}
