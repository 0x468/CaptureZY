#include <string>

#include "core/app_metadata.h"
#include "core/log.h"
#include "platform_win/main_window.h"
#include "platform_win/tray_menu.h"

namespace capturezy::platform_win
{
    namespace
    {
        [[nodiscard]] wchar_t const *TrayClickActionName(core::TrayIconClickActionSetting action) noexcept
        {
            switch (action)
            {
            case core::TrayIconClickActionSetting::Disabled:
                return L"disabled";

            case core::TrayIconClickActionSetting::StartCapture:
                return L"start_capture";

            case core::TrayIconClickActionSetting::OpenMenu:
            default:
                return L"open_menu";
            }
        }
    } // namespace

    bool MainWindow::CreateTrayIcon()
    {
        tray_icon_.cbSize = sizeof(tray_icon_);
        tray_icon_.hWnd = window_;
        tray_icon_.uID = kTrayIconId;
        tray_icon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        tray_icon_.uCallbackMessage = kTrayMessage;
        tray_icon_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

        std::wstring const tray_tooltip = core::AppMetadata::ProductName();
        wcsncpy_s(tray_icon_.szTip, tray_tooltip.c_str(), _TRUNCATE);

        tray_icon_added_ = Shell_NotifyIconW(NIM_ADD, &tray_icon_) == TRUE;
        return tray_icon_added_;
    }

    void MainWindow::RemoveTrayIcon() noexcept
    {
        if (!tray_icon_added_)
        {
            return;
        }

        Shell_NotifyIconW(NIM_DELETE, &tray_icon_);
        tray_icon_added_ = false;
    }

    void MainWindow::ShowTrayMenu()
    {
        ShowMainTrayMenu(window_, *app_settings_, pin_manager_);
    }

    void MainWindow::ExecuteTrayClickAction(core::TrayIconClickActionSetting action)
    {
        CAPTUREZY_LOG_DEBUG(core::LogCategory::Tray,
                            std::wstring(L"Execute tray action: ") + TrayClickActionName(action) + L".");
        switch (action)
        {
        case core::TrayIconClickActionSetting::Disabled:
            return;

        case core::TrayIconClickActionSetting::StartCapture:
            BeginCaptureEntry();
            return;

        case core::TrayIconClickActionSetting::OpenMenu:
        default:
            ShowTrayMenu();
            return;
        }
    }

    bool MainWindow::ShouldDelaySingleTrayClickAction() const noexcept
    {
        return app_settings_->tray_double_click_action != core::TrayIconClickActionSetting::Disabled &&
               app_settings_->tray_double_click_action != app_settings_->tray_single_click_action;
    }

    void MainWindow::SchedulePendingSingleTrayClickAction()
    {
        if (window_ == nullptr)
        {
            return;
        }

        pending_single_tray_click_action_ = true;
        CAPTUREZY_LOG_TRACE(core::LogCategory::Tray, L"Scheduled pending single tray click action.");
        if (SetTimer(window_, kTrayLeftClickTimerId, GetDoubleClickTime(), nullptr) == 0)
        {
            pending_single_tray_click_action_ = false;
            ExecuteTrayClickAction(app_settings_->tray_single_click_action);
        }
    }

    void MainWindow::CancelPendingSingleTrayClickAction() noexcept
    {
        if (window_ != nullptr)
        {
            KillTimer(window_, kTrayLeftClickTimerId);
        }
        pending_single_tray_click_action_ = false;
        CAPTUREZY_LOG_TRACE(core::LogCategory::Tray, L"Cancelled pending single tray click action.");
    }

    bool MainWindow::HandleTrayMessage(LPARAM l_param)
    {
        switch (static_cast<UINT>(l_param))
        {
        case WM_LBUTTONDOWN:
            CAPTUREZY_LOG_TRACE(core::LogCategory::Tray, L"Tray message: WM_LBUTTONDOWN.");
            if (!ShouldDelaySingleTrayClickAction())
            {
                ignore_next_tray_left_button_up_ = true;
                ExecuteTrayClickAction(app_settings_->tray_single_click_action);
            }
            return true;

        case WM_LBUTTONUP:
            CAPTUREZY_LOG_TRACE(core::LogCategory::Tray, L"Tray message: WM_LBUTTONUP.");
            if (ignore_next_tray_left_button_up_)
            {
                ignore_next_tray_left_button_up_ = false;
                return true;
            }

            if (ShouldDelaySingleTrayClickAction())
            {
                SchedulePendingSingleTrayClickAction();
            }
            return true;

        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
            CAPTUREZY_LOG_TRACE(core::LogCategory::Tray, L"Tray message: context menu/right button up.");
            CancelPendingSingleTrayClickAction();
            ignore_next_tray_left_button_up_ = false;
            ShowTrayMenu();
            return true;

        case WM_LBUTTONDBLCLK:
            CAPTUREZY_LOG_TRACE(core::LogCategory::Tray, L"Tray message: WM_LBUTTONDBLCLK.");
            if (ShouldDelaySingleTrayClickAction())
            {
                CancelPendingSingleTrayClickAction();
                ignore_next_tray_left_button_up_ = true;
                ExecuteTrayClickAction(app_settings_->tray_double_click_action);
            }
            return true;

        default:
            return false;
        }
    }
} // namespace capturezy::platform_win
