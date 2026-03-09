#pragma once

#include <windows.h>

namespace capturezy::platform_win
{
    class MainWindow final
    {
      public:
        explicit MainWindow(HINSTANCE instance) noexcept;

        [[nodiscard]] bool Create(int show_command);
        [[nodiscard]] static int RunMessageLoop();

      private:
        [[nodiscard]] ATOM RegisterWindowClass() const;
        [[nodiscard]] LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

        HINSTANCE instance_;
        HWND window_{};
    };
} // namespace capturezy::platform_win
