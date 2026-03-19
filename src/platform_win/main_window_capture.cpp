#include <string>
#include <utility>

#include "core/log.h"
#include "feature_capture/capture_file_dialog.h"
#include "feature_capture/screen_capture.h"
#include "platform_win/main_window.h"

namespace capturezy::platform_win
{
    namespace
    {
        [[nodiscard]] RECT VirtualScreenRect() noexcept
        {
            RECT screen_rect{};
            screen_rect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
            screen_rect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
            screen_rect.right = screen_rect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
            screen_rect.bottom = screen_rect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
            return screen_rect;
        }
    } // namespace

    MainWindow::CaptureScope MainWindow::DefaultCaptureScope() const noexcept
    {
        return app_settings_->default_capture_scope == core::CaptureScopeSetting::FullScreen ? CaptureScope::FullScreen
                                                                                             : CaptureScope::Region;
    }

    MainWindow::CaptureAction MainWindow::DefaultCaptureAction() const noexcept
    {
        switch (app_settings_->default_capture_action)
        {
        case core::CaptureActionSetting::CopyOnly:
            return CaptureAction::CopyOnly;

        case core::CaptureActionSetting::SaveToFile:
            return CaptureAction::SaveToFile;

        case core::CaptureActionSetting::CopyAndPin:
        default:
            return CaptureAction::CopyAndPin;
        }
    }

    void MainWindow::BeginCaptureEntry()
    {
        BeginCaptureEntry(CaptureRequest{DefaultCaptureScope(), DefaultCaptureAction()});
    }

    void MainWindow::BeginCaptureEntry(CaptureRequest capture_request)
    {
        if (app_state_->Mode() == core::AppMode::CapturePending)
        {
            CAPTUREZY_LOG_DEBUG(core::LogCategory::Capture,
                                L"Capture request ignored because capture is already pending.");
            return;
        }

        CAPTUREZY_LOG_INFO(core::LogCategory::Capture,
                           std::wstring(L"Begin capture request. scope=") +
                               std::to_wstring(static_cast<int>(capture_request.scope)) + L", action=" +
                               std::to_wstring(static_cast<int>(capture_request.action)) + L".");
        pending_capture_request_ = capture_request;
        app_state_->BeginCapture();
        UpdateWindowPresentation();
        HideToTray();
        PostMessageW(window_, kExecutePendingCaptureMessage, 0, 0);
    }

    void MainWindow::ExecutePendingCaptureRequest()
    {
        if (pending_capture_request_.scope == CaptureScope::FullScreen)
        {
            ProcessCaptureResult(feature_capture::ScreenCapture::CaptureRegion(VirtualScreenRect()));
            return;
        }

        if (!capture_overlay_.Show(window_))
        {
            app_state_->ReturnToIdle();
            UpdateWindowPresentation();
        }
    }

    void MainWindow::ProcessCaptureResult(feature_capture::CaptureResult capture_result)
    {
        bool capture_completed = false;
        bool capture_saved = false;
        bool pin_created = false;

        if (capture_result.IsValid())
        {
            switch (pending_capture_request_.action)
            {
            case CaptureAction::SaveToFile:
                if (feature_capture::SaveCaptureResultToDefaultPath(capture_result, *app_settings_) ||
                    feature_capture::SaveCaptureResultWithPngDialog(window_, capture_result, *app_settings_))
                {
                    capture_completed = true;
                    capture_saved = true;
                }
                break;

            case CaptureAction::CopyOnly:
                if (feature_capture::ScreenCapture::CopyBitmapToClipboard(window_, capture_result))
                {
                    capture_completed = true;
                }
                break;

            case CaptureAction::CopyAndPin:
            default:
                if (feature_capture::ScreenCapture::CopyBitmapToClipboard(window_, capture_result))
                {
                    capture_completed = true;
                    pin_created = pin_manager_.CreatePin(std::move(capture_result));
                }
                break;
            }
        }

        if (capture_completed)
        {
            if (capture_saved)
            {
                app_state_->CompleteCaptureSaved();
            }
            else if (pin_created)
            {
                app_state_->CompleteCaptureAndPin();
            }
            else
            {
                app_state_->CompleteCapture();
            }
        }
        else
        {
            app_state_->ReturnToIdle();
        }

        UpdateWindowPresentation();
    }

    void MainWindow::HandleOverlayResult(feature_capture::OverlayResult result)
    {
        if (result == feature_capture::OverlayResult::PlaceholderCaptured)
        {
            CAPTUREZY_LOG_INFO(core::LogCategory::Capture, L"Overlay reported capture completion.");
            ProcessCaptureResult(capture_overlay_.FrozenSelectionResult());
            return;
        }

        CAPTUREZY_LOG_INFO(core::LogCategory::Capture, L"Overlay reported capture cancellation.");
        app_state_->ReturnToIdle();
        UpdateWindowPresentation();
    }
} // namespace capturezy::platform_win
