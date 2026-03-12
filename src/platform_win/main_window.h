#pragma once

// clang-format off
#include <windows.h>
// clang-format on

#include <cstdint>
#include <shellapi.h>

#include "core/app_settings.h"
#include "core/app_state.h"
#include "feature_capture/capture_overlay.h"
#include "feature_pin/pin_manager.h"

namespace capturezy::platform_win
{
    class MainWindow final
    {
      public:
        MainWindow(HINSTANCE instance, core::AppState &app_state, core::AppSettings const &app_settings) noexcept;

        [[nodiscard]] bool Create(int show_command);
        [[nodiscard]] static int RunMessageLoop();

      private:
        enum class CaptureAction : std::uint8_t
        {
            CopyOnly,
            CopyAndPin,
            SaveToFile,
        };

        enum class CaptureScope : std::uint8_t
        {
            Region,
            FullScreen,
        };

        struct CaptureRequest final
        {
            constexpr CaptureRequest() noexcept = default;
            constexpr CaptureRequest(CaptureScope requested_scope, CaptureAction requested_action) noexcept
                : scope(requested_scope), action(requested_action)
            {
            }

            CaptureScope scope{CaptureScope::Region};
            CaptureAction action{CaptureAction::CopyAndPin};
        };

        [[nodiscard]] bool RegisterHotkeys() const noexcept;
        void UnregisterHotkeys() const noexcept;
        void UpdateWindowPresentation();
        void BeginCaptureEntry();
        void BeginCaptureEntry(CaptureRequest capture_request);
        [[nodiscard]] bool CreateTrayIcon();
        void RemoveTrayIcon() noexcept;
        void ShowWindowAndActivate() noexcept;
        void HideToTray() noexcept;
        void ShowTrayMenu() noexcept;
        void PaintWindow() const noexcept;
        void ExecutePendingCaptureRequest();
        void ProcessCaptureResult(feature_capture::CaptureResult capture_result);
        void HandleOverlayResult(feature_capture::OverlayResult result);
        [[nodiscard]] bool HandleCommand(WPARAM w_param);
        [[nodiscard]] bool HandleHotkey(WPARAM w_param);
        [[nodiscard]] bool HandleTrayMessage(LPARAM l_param);
        [[nodiscard]] ATOM RegisterWindowClass() const;
        [[nodiscard]] LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

        HINSTANCE instance_;
        core::AppSettings const *app_settings_;
        core::AppState *app_state_;
        HWND window_{};
        feature_capture::CaptureOverlay capture_overlay_;
        feature_pin::PinManager pin_manager_;
        NOTIFYICONDATAW tray_icon_{};
        CaptureRequest pending_capture_request_;
        bool tray_icon_added_{false};
        bool hotkeys_registered_{false};
        bool allow_close_{false};
    };
} // namespace capturezy::platform_win
