#include "pch.h"
#include "App.xaml.h"
#include "ApplicationLog.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace winrt::GmemMonitor::implementation
{
    /// <summary>
    /// Initializes the singleton application object.  This is the first line of authored code
    /// executed, and as such is the logical equivalent of main() or WinMain().
    /// </summary>
    App::App()
    {
        // Xaml objects should not call InitializeComponent during construction.
        // See https://github.com/microsoft/cppwinrt/tree/master/nuget#initializecomponent

#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
#endif
    }

    App::~App()
    {
        gmemmonitor::platform::WriteApplicationLog(gmemmonitor::core::LogLevel::Info, "application teardown started");
        // The application object is normally destroyed as a consequence of the
        // last window closing. Calling Close() again here re-enters WinUI's close
        // path and can fail-fast during process teardown. Releasing the already
        // closed window runs MainWindow's idempotent session/tray cleanup first.
        window = nullptr;
        gmemmonitor::platform::WriteApplicationLog(gmemmonitor::core::LogLevel::Info, "application window released");
        gmemmonitor::platform::AppNotificationService().Shutdown();
        gmemmonitor::platform::WriteApplicationLog(gmemmonitor::core::LogLevel::Info, "notification teardown completed");
        gmemmonitor::platform::ShutdownApplicationLog();
    }

    /// <summary>
    /// Invoked when the application is launched.
    /// </summary>
    /// <param name="e">Details about the launch request and process.</param>
    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e)
    {
        static_cast<void>(gmemmonitor::platform::InitializeApplicationLog());
        gmemmonitor::platform::WriteApplicationLog(gmemmonitor::core::LogLevel::Info, "startup");
        // MainWindow presents a persistent error and prevents monitoring if this fails.
        if (!gmemmonitor::platform::AppNotificationService().Initialize()) {
            gmemmonitor::platform::WriteApplicationLog(gmemmonitor::core::LogLevel::Error, "notification registration failed");
            OutputDebugStringW(L"GMemMonitor: Windows notification registration failed.\n");
        }
        window = make<MainWindow>();
        window.Activate();
    }
}
