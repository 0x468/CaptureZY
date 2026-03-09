#pragma once

#include <windows.h>

#include "platform_win/main_window.h"

namespace capturezy::app
{
    class Application final
    {
      public:
        explicit Application(HINSTANCE instance) noexcept;

        [[nodiscard]] int Run(int show_command);

      private:
        platform_win::MainWindow main_window_;
    };
} // namespace capturezy::app
