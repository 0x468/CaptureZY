#include "platform_win/main_window.h"

#include <memory>

#include "core/app_metadata.h"
#include "core/log.h"
#include "platform_win/tray_menu.h"

namespace capturezy::platform_win
{
    namespace
    {
        // Win32 约定上经常需要把对象指针塞进 GWLP_USERDATA，这里统一封装并局部抑制告警。
        void SetWindowUserData(HWND window, MainWindow *main_window)
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(main_window));
        }

        MainWindow *GetWindowUserData(HWND window)
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
            return reinterpret_cast<MainWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }

    } // namespace

    MainWindow::MainWindow(HINSTANCE instance, core::AppState &app_state, core::AppSettings &app_settings) noexcept
        : instance_(instance), app_settings_(&app_settings), app_state_(&app_state),
          capture_overlay_(std::make_unique<feature_capture::CaptureOverlay>(instance)),
          pin_manager_(std::make_unique<feature_pin::PinManager>(instance, app_settings))
    {
    }

    bool MainWindow::Create(int show_command)
    {
        if (RegisterWindowClass() == 0)
        {
            CAPTUREZY_LOG_ERROR(core::LogCategory::Platform, L"Main window class registration failed.");
            return false;
        }

        window_ = CreateWindowExW(0, core::AppMetadata::MainWindowClassName(), core::AppMetadata::ProductName(),
                                  WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, instance_, this);

        if (window_ == nullptr)
        {
            CAPTUREZY_LOG_ERROR(core::LogCategory::Platform, L"Main window creation failed.");
            return false;
        }

        pin_manager_->SetInventoryChangedCallback([this]() {
            if (window_ != nullptr)
            {
                UpdateWindowPresentation();
            }
        });

        if (!CreateTrayIcon())
        {
            CAPTUREZY_LOG_ERROR(core::LogCategory::Tray, L"Tray icon creation failed.");
            DestroyWindow(window_);
            return false;
        }

        hotkeys_registered_ = RegisterHotkeys();

        UpdateWindowPresentation();
        HideToTray();

        if (show_command == SW_SHOWNORMAL || show_command == SW_SHOWDEFAULT)
        {
            ShowWindow(window_, SW_HIDE);
        }
        return true;
    }

    int MainWindow::RunMessageLoop()
    {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        return static_cast<int>(message.wParam);
    }

    bool MainWindow::RegisterHotkeys() const noexcept
    {
        return RegisterHotKey(window_, kCaptureHotkeyId, app_settings_->capture_hotkey.modifiers,
                              app_settings_->capture_hotkey.virtual_key) != FALSE;
    }

    void MainWindow::UnregisterHotkeys() const noexcept
    {
        if (hotkeys_registered_)
        {
            UnregisterHotKey(window_, kCaptureHotkeyId);
        }
    }

    void MainWindow::UpdateWindowPresentation()
    {
        SetWindowTextW(window_, core::AppMetadata::ProductName());
    }

    void MainWindow::ShowMessageDialog(wchar_t const *title, wchar_t const *message, UINT icon_flags) const noexcept
    {
        MessageBoxW(window_, message, title, MB_OK | icon_flags);
    }

    void MainWindow::HideToTray() noexcept
    {
        ShowWindow(window_, SW_HIDE);
    }

    bool MainWindow::HandleCommand(WPARAM w_param)
    {
        switch (LOWORD(w_param))
        {
        case TrayMenuCommand::BeginCapture:
            BeginCaptureEntry();
            return true;

        case TrayMenuCommand::BeginCaptureAndSave:
            BeginCaptureEntry(CaptureRequest{CaptureScope::Region, CaptureAction::SaveToFile});
            return true;

        case TrayMenuCommand::BeginFullScreenCapture:
            BeginCaptureEntry(CaptureRequest{CaptureScope::FullScreen, CaptureAction::CopyOnly});
            return true;

        case TrayMenuCommand::BeginFullScreenCaptureAndSave:
            BeginCaptureEntry(CaptureRequest{CaptureScope::FullScreen, CaptureAction::SaveToFile});
            return true;

        case TrayMenuCommand::OpenSettingsDialog:
            OpenSettingsDialog();
            return true;

        case TrayMenuCommand::SetDefaultScopeRegion: {
            core::AppSettings updated_settings = *app_settings_;
            updated_settings.default_capture_scope = core::CaptureScopeSetting::Region;
            return ApplySettings(std::move(updated_settings), true, L"应用新配置失败，已保留当前配置。",
                                 L"配置保存失败。");
        }

        case TrayMenuCommand::SetDefaultScopeFullScreen: {
            core::AppSettings updated_settings = *app_settings_;
            updated_settings.default_capture_scope = core::CaptureScopeSetting::FullScreen;
            return ApplySettings(std::move(updated_settings), true, L"应用新配置失败，已保留当前配置。",
                                 L"配置保存失败。");
        }

        case TrayMenuCommand::SetDefaultActionCopyOnly: {
            core::AppSettings updated_settings = *app_settings_;
            updated_settings.default_capture_action = core::CaptureActionSetting::CopyOnly;
            return ApplySettings(std::move(updated_settings), true, L"应用新配置失败，已保留当前配置。",
                                 L"配置保存失败。");
        }

        case TrayMenuCommand::SetDefaultActionCopyAndPin: {
            core::AppSettings updated_settings = *app_settings_;
            updated_settings.default_capture_action = core::CaptureActionSetting::CopyAndPin;
            return ApplySettings(std::move(updated_settings), true, L"应用新配置失败，已保留当前配置。",
                                 L"配置保存失败。");
        }

        case TrayMenuCommand::SetDefaultActionSaveToFile: {
            core::AppSettings updated_settings = *app_settings_;
            updated_settings.default_capture_action = core::CaptureActionSetting::SaveToFile;
            return ApplySettings(std::move(updated_settings), true, L"应用新配置失败，已保留当前配置。",
                                 L"配置保存失败。");
        }

        case TrayMenuCommand::EditSettingsFile:
            if (!OpenSettingsFileForEditing())
            {
                ShowMessageDialog(L"CaptureZY", L"无法打开配置文件。", MB_ICONERROR);
            }
            return true;

        case TrayMenuCommand::OpenSettingsDirectory:
            if (!OpenSettingsDirectory())
            {
                ShowMessageDialog(L"CaptureZY", L"无法打开配置目录。", MB_ICONERROR);
            }
            return true;

        case TrayMenuCommand::OpenDefaultSaveDirectory:
            if (!OpenDefaultSaveDirectory())
            {
                ShowMessageDialog(L"CaptureZY", L"无法打开默认保存目录。", MB_ICONERROR);
            }
            return true;

        case TrayMenuCommand::ReloadSettings:
            (void)ReloadSettings();
            return true;

        case TrayMenuCommand::ShowAllPins:
            pin_manager_->ShowAll();
            return true;

        case TrayMenuCommand::HideAllPins:
            pin_manager_->HideAll();
            return true;

        case TrayMenuCommand::CloseAllPins:
            pin_manager_->CloseAll();
            return true;

        case TrayMenuCommand::ExitApplication:
            if (ConfirmApplicationExit())
            {
                allow_close_ = true;
                DestroyWindow(window_);
            }
            return true;

        default:
            return false;
        }
    }

    bool MainWindow::HandleHotkey(WPARAM w_param)
    {
        if (static_cast<int>(w_param) != kCaptureHotkeyId)
        {
            return false;
        }

        BeginCaptureEntry();
        return true;
    }

    ATOM MainWindow::RegisterWindowClass() const
    {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = MainWindow::WindowProc;
        window_class.hInstance = instance_;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        window_class.lpszClassName = core::AppMetadata::MainWindowClassName();

        ATOM const result = RegisterClassExW(&window_class);
        if (result != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
        {
            return 1;
        }

        return 0;
    }

    LRESULT MainWindow::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param)
    {
        switch (message)
        {
        case WM_CLOSE:
            if (!allow_close_)
            {
                HideToTray();
                return 0;
            }

            return DefWindowProcW(window_, message, w_param, l_param);

        case WM_DESTROY:
            CancelPendingSingleTrayClickAction();
            pin_manager_->CloseAll();
            UnregisterHotkeys();
            RemoveTrayIcon();
            PostQuitMessage(0);
            return 0;

        case WM_COMMAND:
            if (HandleCommand(w_param))
            {
                return 0;
            }
            break;

        case kExecutePendingCaptureMessage:
            ExecutePendingCaptureRequest();
            return 0;

        case WM_TIMER:
            if (w_param == kTrayLeftClickTimerId)
            {
                KillTimer(window_, kTrayLeftClickTimerId);
                if (pending_single_tray_click_action_)
                {
                    pending_single_tray_click_action_ = false;
                    ExecuteTrayClickAction(app_settings_->tray_single_click_action);
                }
                return 0;
            }
            break;

        case feature_capture::CaptureOverlay::ResultMessage():
            HandleOverlayResult(static_cast<feature_capture::OverlayResult>(w_param));
            return 0;

        case WM_HOTKEY:
            if (HandleHotkey(w_param))
            {
                return 0;
            }
            break;

        case kTrayMessage:
            if (HandleTrayMessage(l_param))
            {
                return 0;
            }
            break;

        default:
            break;
        }

        return DefWindowProcW(window_, message, w_param, l_param);
    }

    LRESULT CALLBACK MainWindow::WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
    {
        if (message == WM_NCCREATE)
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
            auto *create_struct = reinterpret_cast<CREATESTRUCTW *>(l_param);
            auto *main_window = static_cast<MainWindow *>(create_struct->lpCreateParams);
            main_window->window_ = window;
            SetWindowUserData(window, main_window);
        }

        if (auto *main_window = GetWindowUserData(window); main_window != nullptr)
        {
            return main_window->HandleMessage(message, w_param, l_param);
        }

        return DefWindowProcW(window, message, w_param, l_param);
    }
} // namespace capturezy::platform_win
