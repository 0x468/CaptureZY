#include "app/application.h"

#include "render_d2d/render_backend.h"

namespace capturezy::app
{
    Application::Application(HINSTANCE instance) noexcept : main_window_(instance) {}

    int Application::Run(int show_command)
    {
        (void)render_d2d::RenderBackend::DisplayName();

        if (!main_window_.Create(show_command))
        {
            return -1;
        }

        return platform_win::MainWindow::RunMessageLoop();
    }
} // namespace capturezy::app
