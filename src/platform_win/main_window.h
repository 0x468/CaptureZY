#pragma once

// clang-format off
#include <windows.h>
// clang-format on

#include <shellapi.h>

#include "core/app_state.h"
#include "feature_capture/capture_overlay.h"
#include "feature_pin/pin_manager.h"

namespace capturezy::platform_win
{
    class MainWindow final
    {
      public:
        MainWindow(HINSTANCE instance, core::AppState &app_state) noexcept;

        [[nodiscard]] bool Create(int show_command);
        [[nodiscard]] static int RunMessageLoop();

      private:
        [[nodiscard]] bool RegisterHotkeys() const noexcept;
        void UnregisterHotkeys() const noexcept;
        void UpdateWindowPresentation();
        void BeginCaptureEntry();
        [[nodiscard]] bool CreateTrayIcon();
        void RemoveTrayIcon() noexcept;
        void ShowWindowAndActivate() noexcept;
        void HideToTray() noexcept;
        void ShowTrayMenu() noexcept;
        void PaintWindow() const noexcept;
        void HandleOverlayResult(feature_capture::OverlayResult result);
        [[nodiscard]] bool HandleCommand(WPARAM w_param);
        [[nodiscard]] bool HandleHotkey(WPARAM w_param);
        [[nodiscard]] bool HandleTrayMessage(LPARAM l_param);
        [[nodiscard]] ATOM RegisterWindowClass() const;
        [[nodiscard]] LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

        HINSTANCE instance_;
        core::AppState *app_state_;
        HWND window_{};
        feature_capture::CaptureOverlay capture_overlay_;
        feature_pin::PinManager pin_manager_;
        NOTIFYICONDATAW tray_icon_{};
        bool tray_icon_added_{false};
        bool hotkeys_registered_{false};
        bool allow_close_{false};
    };
} // namespace capturezy::platform_win
