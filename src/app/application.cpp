#include "app/application.h"

#include <objbase.h>
#include <shellscalingapi.h>

#include "render_d2d/render_backend.h"

namespace
{
    bool EnablePerMonitorDpiAwareness() noexcept
    {
        using SetProcessDpiAwarenessContextFn = BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);
        using SetProcessDpiAwarenessFn = HRESULT(WINAPI *)(PROCESS_DPI_AWARENESS);

        HMODULE const user32 = GetModuleHandleW(L"user32.dll");
        if (user32 != nullptr)
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            auto const set_process_dpi_awareness_context = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
                GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
            if (set_process_dpi_awareness_context != nullptr)
            {
                if (set_process_dpi_awareness_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE)
                {
                    return true;
                }

                DWORD const last_error = GetLastError();
                if (last_error == ERROR_ACCESS_DENIED)
                {
                    return true;
                }
            }
        }

        HMODULE const shcore = LoadLibraryW(L"shcore.dll");
        if (shcore == nullptr)
        {
            return false;
        }

        auto const set_process_dpi_awareness =
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            reinterpret_cast<SetProcessDpiAwarenessFn>(GetProcAddress(shcore, "SetProcessDpiAwareness"));
        bool enabled = false;
        if (set_process_dpi_awareness != nullptr)
        {
            HRESULT const result = set_process_dpi_awareness(PROCESS_PER_MONITOR_DPI_AWARE);
            enabled = SUCCEEDED(result) || result == E_ACCESSDENIED;
        }

        FreeLibrary(shcore);
        return enabled;
    }
} // namespace

namespace capturezy::app
{
    Application::Application(HINSTANCE instance) : main_window_(instance, app_state_, app_settings_) {}

    Application::~Application() noexcept
    {
        UninitializeCom();
    }

    bool Application::InitializeCom() noexcept
    {
        HRESULT const result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (SUCCEEDED(result))
        {
            com_initialized_ = true;
            return true;
        }

        if (result == RPC_E_CHANGED_MODE)
        {
            return true;
        }

        return false;
    }

    void Application::UninitializeCom() noexcept
    {
        if (com_initialized_)
        {
            CoUninitialize();
            com_initialized_ = false;
        }
    }

    int Application::Run(int show_command)
    {
        (void)render_d2d::RenderBackend::DisplayName();
        (void)EnablePerMonitorDpiAwareness();

        if (!InitializeCom())
        {
            return -1;
        }

        app_settings_ = core::AppSettingsStore::Load();

        if (!main_window_.Create(show_command))
        {
            return -1;
        }

        return platform_win::MainWindow::RunMessageLoop();
    }
} // namespace capturezy::app
