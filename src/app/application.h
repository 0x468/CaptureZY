#pragma once

#include <windows.h>

#include "core/app_state.h"
#include "platform_win/main_window.h"

namespace capturezy::app
{
    class Application final
    {
      public:
        explicit Application(HINSTANCE instance) noexcept;
        ~Application() noexcept;

        Application(Application const &) = delete;
        Application &operator=(Application const &) = delete;
        Application(Application &&) = delete;
        Application &operator=(Application &&) = delete;

        [[nodiscard]] int Run(int show_command);

      private:
        bool InitializeCom() noexcept;
        void UninitializeCom() noexcept;

        core::AppState app_state_{};
        platform_win::MainWindow main_window_;
        bool com_initialized_{false};
    };
} // namespace capturezy::app
