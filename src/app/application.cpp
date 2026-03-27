#include "app/application.h"

#include <memory>
#include <objbase.h>
#include <shellscalingapi.h>

#include "core/app_settings_store.h"
#include "core/log.h"
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
    namespace
    {
        [[nodiscard]] std::wstring RuntimeEnvironmentSummary(bool dpi_aware, int show_command)
        {
            std::wstring summary = L"Runtime environment: dpi_aware=";
            summary += dpi_aware ? L"true" : L"false";
            summary += L", show_command=";
            summary += std::to_wstring(show_command);
            summary += L", render_backend=";
            summary += render_d2d::RenderBackend::DisplayName();
            summary += L", settings_file=";
            summary += core::AppSettingsStore::SettingsFilePath();
            return summary;
        }
    } // namespace

    Application::Application(HINSTANCE instance) : instance_(instance) {}

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
        bool const dpi_aware = EnablePerMonitorDpiAwareness();
        CAPTUREZY_LOG_INFO(core::LogCategory::App, RuntimeEnvironmentSummary(dpi_aware, show_command));

        if (!InitializeCom())
        {
            CAPTUREZY_LOG_ERROR(core::LogCategory::App, L"COM initialization failed.");
            return -1;
        }
        CAPTUREZY_LOG_DEBUG(core::LogCategory::App, L"COM apartment initialized.");

        app_settings_ = core::AppSettingsStore::Load();
        CAPTUREZY_LOG_INFO(core::LogCategory::Settings,
                           std::wstring(L"Loaded settings: single=") +
                               std::to_wstring(static_cast<int>(app_settings_.tray_single_click_action)) +
                               L", double=" +
                               std::to_wstring(static_cast<int>(app_settings_.tray_double_click_action)) + L".");

        CAPTUREZY_LOG_DEBUG(core::LogCategory::App, L"Constructing main window controller.");
        main_window_ = std::make_unique<platform_win::MainWindow>(instance_, app_state_, app_settings_);
        CAPTUREZY_LOG_DEBUG(core::LogCategory::App, L"Main window controller constructed.");
        CAPTUREZY_LOG_DEBUG(core::LogCategory::App, L"Creating main window.");
        if (!main_window_->Create(show_command))
        {
            CAPTUREZY_LOG_ERROR(core::LogCategory::App, L"Main window creation failed.");
            main_window_.reset();
            return -1;
        }
        CAPTUREZY_LOG_INFO(core::LogCategory::App, L"Main window created successfully.");

        return platform_win::MainWindow::RunMessageLoop();
    }
} // namespace capturezy::app
