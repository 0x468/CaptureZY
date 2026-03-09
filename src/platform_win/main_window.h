#pragma once

// clang-format off
#include <windows.h>
// clang-format on

#include <shellapi.h>

namespace capturezy::platform_win
{
    class MainWindow final
    {
      public:
        explicit MainWindow(HINSTANCE instance) noexcept;

        [[nodiscard]] bool Create(int show_command);
        [[nodiscard]] static int RunMessageLoop();

      private:
        [[nodiscard]] bool CreateTrayIcon();
        void RemoveTrayIcon() noexcept;
        void ShowWindowAndActivate() noexcept;
        void HideToTray() noexcept;
        void ShowTrayMenu() noexcept;
        [[nodiscard]] ATOM RegisterWindowClass() const;
        [[nodiscard]] LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

        HINSTANCE instance_;
        HWND window_{};
        NOTIFYICONDATAW tray_icon_{};
        bool tray_icon_added_{false};
        bool allow_close_{false};
    };
} // namespace capturezy::platform_win
