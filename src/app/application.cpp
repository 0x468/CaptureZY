#include "app/application.h"

#include <objbase.h>

#include "render_d2d/render_backend.h"

namespace capturezy::app
{
    Application::Application(HINSTANCE instance) noexcept : main_window_(instance, app_state_) {}

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

        if (!InitializeCom())
        {
            return -1;
        }

        if (!main_window_.Create(show_command))
        {
            return -1;
        }

        return platform_win::MainWindow::RunMessageLoop();
    }
} // namespace capturezy::app
