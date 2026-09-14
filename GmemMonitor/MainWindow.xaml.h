#pragma once

#include "MainWindow.g.h"

namespace winrt::GmemMonitor::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        int32_t MyProperty();
        void MyProperty(int32_t value);
        void Settings_TextChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::TextChangedEventArgs const&);
    private:
        void ValidateSettings();
        int32_t myProperty_{};
    };
}

namespace winrt::GmemMonitor::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
