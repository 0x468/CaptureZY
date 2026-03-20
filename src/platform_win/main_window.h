#pragma once

#include <memory>

// clang-format off
#include <windows.h>
// clang-format on

#include <cstdint>
#include <shellapi.h>
#include <string>

#include "core/app_settings.h"
#include "core/app_state.h"
#include "feature_capture/capture_overlay.h"
#include "feature_pin/pin_manager.h"

namespace capturezy::platform_win
{
    class MainWindow final
    {
      public:
        MainWindow(HINSTANCE instance, core::AppState &app_state, core::AppSettings &app_settings) noexcept;

        [[nodiscard]] bool Create(int show_command);
        [[nodiscard]] static int RunMessageLoop();

      private:
        static constexpr UINT kTrayIconId = 1;
        static constexpr UINT kTrayMessage = WM_APP + 1;
        static constexpr UINT_PTR kTrayLeftClickTimerId = 2;
        static constexpr UINT kExecutePendingCaptureMessage = WM_APP + 2;
        static constexpr int kCaptureHotkeyId = 1;

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

        [[nodiscard]] CaptureScope DefaultCaptureScope() const noexcept;
        [[nodiscard]] CaptureAction DefaultCaptureAction() const noexcept;
        [[nodiscard]] bool RegisterHotkeys() const noexcept;
        void UnregisterHotkeys() const noexcept;
        void UpdateWindowPresentation();
        void BeginCaptureEntry();
        void BeginCaptureEntry(CaptureRequest capture_request);
        [[nodiscard]] bool CreateTrayIcon();
        [[nodiscard]] bool ApplySettings(core::AppSettings new_settings, bool persist,
                                         wchar_t const *hotkey_error_message, wchar_t const *save_error_message);
        void OpenSettingsDialog();
        [[nodiscard]] bool OpenDefaultSaveDirectory() const;
        [[nodiscard]] bool OpenSettingsFileForEditing() const;
        [[nodiscard]] bool OpenSettingsDirectory() const;
        [[nodiscard]] bool ReloadSettings();
        [[nodiscard]] bool ConfirmApplicationExit();
        void RemoveTrayIcon() noexcept;
        void HideToTray() noexcept;
        void ShowMessageDialog(wchar_t const *title, wchar_t const *message, UINT icon_flags) const noexcept;
        void ShowTrayMenu();
        void ExecuteTrayClickAction(core::TrayIconClickActionSetting action);
        [[nodiscard]] bool ShouldDelaySingleTrayClickAction() const noexcept;
        void SchedulePendingSingleTrayClickAction();
        void CancelPendingSingleTrayClickAction() noexcept;
        void ExecutePendingCaptureRequest();
        void ProcessCaptureResult(feature_capture::CaptureResult capture_result);
        void ProcessCaptureResult(feature_capture::CaptureResult capture_result, CaptureAction action);
        void HandleOverlayResult(feature_capture::OverlayResult result);
        [[nodiscard]] bool HandleCommand(WPARAM w_param);
        [[nodiscard]] bool HandleHotkey(WPARAM w_param);
        [[nodiscard]] bool HandleTrayMessage(LPARAM l_param);
        [[nodiscard]] ATOM RegisterWindowClass() const;
        [[nodiscard]] LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

        HINSTANCE instance_;
        core::AppSettings *app_settings_;
        core::AppState *app_state_;
        HWND window_{};
        std::unique_ptr<feature_capture::CaptureOverlay> capture_overlay_;
        std::unique_ptr<feature_pin::PinManager> pin_manager_;
        NOTIFYICONDATAW tray_icon_{};
        CaptureRequest pending_capture_request_;
        bool tray_icon_added_{false};
        bool pending_single_tray_click_action_{false};
        bool ignore_next_tray_left_button_up_{false};
        bool hotkeys_registered_{false};
        bool allow_close_{false};
    };
} // namespace capturezy::platform_win
