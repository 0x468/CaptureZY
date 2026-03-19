#include "platform_win/main_window.h"

#include <filesystem>
#include <string>
#include <utility>

#include "core/app_metadata.h"
#include "core/app_settings_store.h"
#include "core/log.h"
#include "feature_capture/capture_file_dialog.h"
#include "feature_capture/screen_capture.h"
#include "platform_win/settings_dialog.h"
#include "platform_win/tray_menu.h"

namespace capturezy::platform_win
{
    namespace
    {
        constexpr UINT kTrayIconId = 1;
        constexpr UINT kTrayMessage = WM_APP + 1;
        constexpr UINT kExecutePendingCaptureMessage = WM_APP + 2;
        constexpr UINT_PTR kTrayLeftClickTimerId = 2;
        constexpr int kCaptureHotkeyId = 1;
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

        [[nodiscard]] RECT VirtualScreenRect() noexcept
        {
            RECT screen_rect{};
            screen_rect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
            screen_rect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
            screen_rect.right = screen_rect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
            screen_rect.bottom = screen_rect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
            return screen_rect;
        }

        [[nodiscard]] bool ShellExecuteSucceeded(HINSTANCE result) noexcept
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
            return reinterpret_cast<INT_PTR>(result) > 32;
        }

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

    MainWindow::MainWindow(HINSTANCE instance, core::AppState &app_state, core::AppSettings &app_settings) noexcept
        : instance_(instance), app_settings_(&app_settings), app_state_(&app_state), capture_overlay_(instance),
          pin_manager_(instance, app_settings)
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

        pin_manager_.SetInventoryChangedCallback([this]() {
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

    bool MainWindow::ApplySettings(core::AppSettings new_settings, bool persist, wchar_t const *hotkey_error_message,
                                   wchar_t const *save_error_message)
    {
        core::AppSettings const previous_settings = *app_settings_;
        bool const previous_hotkeys_registered = hotkeys_registered_;

        CancelPendingSingleTrayClickAction();
        ignore_next_tray_left_button_up_ = false;

        UnregisterHotkeys();
        *app_settings_ = std::move(new_settings);
        CAPTUREZY_LOG_INFO(core::LogCategory::Settings,
                           std::wstring(L"Apply settings. single=") +
                               std::to_wstring(static_cast<int>(app_settings_->tray_single_click_action)) +
                               L", double=" +
                               std::to_wstring(static_cast<int>(app_settings_->tray_double_click_action)) + L".");
        hotkeys_registered_ = RegisterHotkeys();
        if (!hotkeys_registered_)
        {
            *app_settings_ = previous_settings;
            hotkeys_registered_ = previous_hotkeys_registered;
            if (hotkeys_registered_)
            {
                hotkeys_registered_ = RegisterHotkeys();
            }

            CAPTUREZY_LOG_WARNING(core::LogCategory::Settings, hotkey_error_message);
            ShowMessageDialog(L"CaptureZY", hotkey_error_message, MB_ICONWARNING);
            return false;
        }

        if (!persist || core::AppSettingsStore::Save(*app_settings_))
        {
            return true;
        }

        UnregisterHotkeys();
        *app_settings_ = previous_settings;
        hotkeys_registered_ = previous_hotkeys_registered;
        if (hotkeys_registered_)
        {
            hotkeys_registered_ = RegisterHotkeys();
        }

        CAPTUREZY_LOG_ERROR(core::LogCategory::Settings, save_error_message);
        ShowMessageDialog(L"CaptureZY", save_error_message, MB_ICONERROR);
        return false;
    }

    void MainWindow::OpenSettingsDialog()
    {
        CAPTUREZY_LOG_DEBUG(core::LogCategory::SettingsDialog, L"Open settings dialog.");
        bool const hotkeys_were_active = hotkeys_registered_;
        if (hotkeys_were_active)
        {
            UnregisterHotkeys();
        }

        if (auto updated_settings = ShowSettingsDialog(instance_, window_, *app_settings_);
            updated_settings.has_value())
        {
            if (ApplySettings(std::move(*updated_settings), true, L"新设置中的截图热键注册失败，已保留当前配置。",
                              L"设置写回配置文件失败，已恢复当前配置。"))
            {
                CAPTUREZY_LOG_INFO(core::LogCategory::SettingsDialog, L"Settings dialog accepted.");
                ShowMessageDialog(L"CaptureZY", L"设置已更新。", MB_ICONINFORMATION);
            }

            return;
        }

        CAPTUREZY_LOG_DEBUG(core::LogCategory::SettingsDialog, L"Settings dialog cancelled.");
        if (hotkeys_were_active)
        {
            hotkeys_registered_ = RegisterHotkeys();
        }
    }

    bool MainWindow::OpenSettingsFileForEditing() const
    {
        std::wstring const settings_file_path = core::AppSettingsStore::SettingsFilePath();
        if (settings_file_path.empty())
        {
            return false;
        }

        std::wstring const parameters = L"\"" + settings_file_path + L"\"";
        HINSTANCE const result = ShellExecuteW(window_, L"open", L"notepad.exe", parameters.c_str(), nullptr,
                                               SW_SHOWNORMAL);
        return ShellExecuteSucceeded(result);
    }

    bool MainWindow::OpenSettingsDirectory() const
    {
        std::filesystem::path const settings_file_path(core::AppSettingsStore::SettingsFilePath());
        std::filesystem::path const settings_directory = settings_file_path.parent_path();
        if (settings_directory.empty())
        {
            return false;
        }

        HINSTANCE const result = ShellExecuteW(window_, L"open", settings_directory.c_str(), nullptr, nullptr,
                                               SW_SHOWNORMAL);
        return ShellExecuteSucceeded(result);
    }

    bool MainWindow::OpenDefaultSaveDirectory() const
    {
        std::filesystem::path const save_directory(app_settings_->default_save_directory);
        if (save_directory.empty())
        {
            return false;
        }

        std::error_code error_code;
        std::filesystem::create_directories(save_directory, error_code);
        if (error_code)
        {
            return false;
        }

        HINSTANCE const result = ShellExecuteW(window_, L"open", save_directory.c_str(), nullptr, nullptr,
                                               SW_SHOWNORMAL);
        return ShellExecuteSucceeded(result);
    }

    bool MainWindow::ReloadSettings()
    {
        if (ApplySettings(core::AppSettingsStore::Load(), false, L"配置已读取，但新热键注册失败，已恢复旧热键。", L""))
        {
            CAPTUREZY_LOG_INFO(core::LogCategory::Settings, L"Settings reloaded from disk.");
            ShowMessageDialog(L"CaptureZY", L"配置已重载。", MB_ICONINFORMATION);
            return true;
        }

        CAPTUREZY_LOG_WARNING(core::LogCategory::Settings, L"Settings reload failed.");
        return false;
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

    void MainWindow::ShowMessageDialog(wchar_t const *title, wchar_t const *message, UINT icon_flags) const noexcept
    {
        MessageBoxW(window_, message, title, MB_OK | icon_flags);
    }

    void MainWindow::HideToTray() noexcept
    {
        ShowWindow(window_, SW_HIDE);
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
            pin_manager_.ShowAll();
            return true;

        case TrayMenuCommand::HideAllPins:
            pin_manager_.HideAll();
            return true;

        case TrayMenuCommand::CloseAllPins:
            pin_manager_.CloseAll();
            return true;

        case TrayMenuCommand::ExitApplication:
            allow_close_ = true;
            DestroyWindow(window_);
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
            pin_manager_.CloseAll();
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
