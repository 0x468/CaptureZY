#include "platform_win/main_window.h"

#include <filesystem>
#include <string>
#include <utility>

#include "core/app_metadata.h"
#include "core/app_settings_store.h"
#include "feature_capture/capture_file_dialog.h"
#include "feature_capture/screen_capture.h"
#include "platform_win/settings_dialog.h"

namespace capturezy::platform_win
{
    namespace
    {
        constexpr UINT kTrayIconId = 1;
        constexpr UINT kTrayMessage = WM_APP + 1;
        constexpr UINT kExecutePendingCaptureMessage = WM_APP + 2;
        constexpr UINT_PTR kShowWindowCommandId = 1001;
        constexpr UINT_PTR kBeginCaptureCommandId = 1002;
        constexpr UINT_PTR kBeginCaptureAndSaveCommandId = 1003;
        constexpr UINT_PTR kBeginFullScreenCaptureCommandId = 1004;
        constexpr UINT_PTR kBeginFullScreenCaptureAndSaveCommandId = 1005;
        constexpr UINT_PTR kShowAllPinsCommandId = 1006;
        constexpr UINT_PTR kHideAllPinsCommandId = 1007;
        constexpr UINT_PTR kCloseAllPinsCommandId = 1008;
        constexpr UINT_PTR kExitApplicationCommandId = 1009;
        constexpr UINT_PTR kEditSettingsFileCommandId = 1010;
        constexpr UINT_PTR kOpenSettingsDirectoryCommandId = 1011;
        constexpr UINT_PTR kReloadSettingsCommandId = 1012;
        constexpr UINT_PTR kSetDefaultScopeRegionCommandId = 1013;
        constexpr UINT_PTR kSetDefaultScopeFullScreenCommandId = 1014;
        constexpr UINT_PTR kSetDefaultActionCopyOnlyCommandId = 1015;
        constexpr UINT_PTR kSetDefaultActionCopyAndPinCommandId = 1016;
        constexpr UINT_PTR kSetDefaultActionSaveToFileCommandId = 1017;
        constexpr UINT_PTR kOpenDefaultSaveDirectoryCommandId = 1018;
        constexpr UINT_PTR kOpenSettingsDialogCommandId = 1022;
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

        struct BatchPinMenuLabelParts final
        {
            wchar_t const *action_prefix;
            wchar_t const *item_label;
        };

        [[nodiscard]] std::wstring BatchPinMenuLabel(BatchPinMenuLabelParts parts, std::size_t count)
        {
            if (count == 0)
            {
                return parts.action_prefix;
            }

            std::wstring label = parts.action_prefix;
            label += L" ";
            label += std::to_wstring(count);
            label += L" ";
            label += parts.item_label;
            return label;
        }

        [[nodiscard]] std::wstring PinInventorySummaryLabel(std::size_t visible_pin_count, std::size_t hidden_pin_count)
        {
            if (visible_pin_count == 0 && hidden_pin_count == 0)
            {
                return L"贴图：当前无贴图";
            }

            std::wstring label = L"贴图：显示 ";
            label += std::to_wstring(visible_pin_count);
            label += L" 个";
            if (hidden_pin_count != 0)
            {
                label += L"，隐藏 ";
                label += std::to_wstring(hidden_pin_count);
                label += L" 个";
            }
            return label;
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
            return false;
        }

        core::Size const size = core::AppMetadata::MainWindowSize();

        window_ = CreateWindowExW(0, core::AppMetadata::MainWindowClassName(), core::AppMetadata::ProductName(),
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, size.width, size.height, nullptr,
                                  nullptr, instance_, this);

        if (window_ == nullptr)
        {
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
            DestroyWindow(window_);
            return false;
        }

        hotkeys_registered_ = RegisterHotkeys();

        ShowWindow(window_, show_command);
        UpdateWindowPresentation();
        UpdateWindow(window_);
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
        std::wstring title = core::AppMetadata::ProductName();
        title += app_state_->WindowTitleSuffix();
        SetWindowTextW(window_, title.c_str());
        InvalidateRect(window_, nullptr, TRUE);
    }

    std::wstring MainWindow::CurrentStatusText()
    {
        std::wstring status_text = app_state_->StatusText();

        std::size_t const open_pin_count = pin_manager_.OpenPinCount();
        if (open_pin_count == 0)
        {
            return status_text;
        }

        std::size_t const visible_pin_count = pin_manager_.VisiblePinCount();
        std::size_t const hidden_pin_count = open_pin_count >= visible_pin_count ? open_pin_count - visible_pin_count
                                                                                 : 0;
        if (hidden_pin_count == 0)
        {
            return status_text;
        }

        status_text += L" 当前有 ";
        status_text += std::to_wstring(hidden_pin_count);
        status_text += L" 个隐藏贴图";
        if (visible_pin_count != 0)
        {
            status_text += L"，另有 ";
            status_text += std::to_wstring(visible_pin_count);
            status_text += L" 个仍在桌面显示";
        }
        status_text += L"，可在托盘菜单中恢复。";
        return status_text;
    }

    void MainWindow::BeginCaptureEntry()
    {
        BeginCaptureEntry(CaptureRequest{DefaultCaptureScope(), DefaultCaptureAction()});
    }

    void MainWindow::BeginCaptureEntry(CaptureRequest capture_request)
    {
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
        if (!tray_icon_added_)
        {
            return false;
        }

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        tray_icon_.uVersion = NOTIFYICON_VERSION;
        Shell_NotifyIconW(NIM_SETVERSION, &tray_icon_);
        return true;
    }

    bool MainWindow::ApplySettings(core::AppSettings new_settings, bool persist, wchar_t const *hotkey_error_message,
                                   wchar_t const *save_error_message)
    {
        core::AppSettings const previous_settings = *app_settings_;
        bool const previous_hotkeys_registered = hotkeys_registered_;

        UnregisterHotkeys();
        *app_settings_ = std::move(new_settings);
        hotkeys_registered_ = RegisterHotkeys();
        if (!hotkeys_registered_)
        {
            *app_settings_ = previous_settings;
            hotkeys_registered_ = previous_hotkeys_registered;
            if (hotkeys_registered_)
            {
                hotkeys_registered_ = RegisterHotkeys();
            }

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

        ShowMessageDialog(L"CaptureZY", save_error_message, MB_ICONERROR);
        return false;
    }

    void MainWindow::OpenSettingsDialog()
    {
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
                ShowMessageDialog(L"CaptureZY", L"设置已更新。", MB_ICONINFORMATION);
            }

            return;
        }

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
            ShowMessageDialog(L"CaptureZY", L"配置已重载。", MB_ICONINFORMATION);
            return true;
        }

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

    void MainWindow::ShowWindowAndActivate() noexcept
    {
        ShowWindow(window_, SW_SHOWNORMAL);
        SetForegroundWindow(window_);
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
        HMENU menu = CreatePopupMenu();
        if (menu == nullptr)
        {
            return;
        }

        HMENU default_scope_menu = CreatePopupMenu();
        HMENU default_action_menu = CreatePopupMenu();
        if (default_scope_menu == nullptr || default_action_menu == nullptr)
        {
            if (default_action_menu != nullptr)
            {
                DestroyMenu(default_action_menu);
            }
            if (default_scope_menu != nullptr)
            {
                DestroyMenu(default_scope_menu);
            }
            DestroyMenu(menu);
            return;
        }

        std::size_t const open_pin_count = pin_manager_.OpenPinCount();
        std::size_t const visible_pin_count = pin_manager_.VisiblePinCount();
        std::size_t const hidden_pin_count = pin_manager_.HiddenPinCount();

        UINT show_all_pins_flags = MF_STRING;
        if (hidden_pin_count == 0)
        {
            show_all_pins_flags |= MF_GRAYED;
        }

        UINT hide_all_pins_flags = MF_STRING;
        if (visible_pin_count == 0)
        {
            hide_all_pins_flags |= MF_GRAYED;
        }

        UINT close_all_pins_flags = MF_STRING;
        if (open_pin_count == 0)
        {
            close_all_pins_flags |= MF_GRAYED;
        }

        AppendMenuW(menu, MF_STRING, kShowWindowCommandId, L"显示窗口");
        AppendMenuW(menu, MF_STRING, kBeginCaptureCommandId, L"开始截图");
        AppendMenuW(menu, MF_STRING, kBeginCaptureAndSaveCommandId, L"开始截图并保存");
        AppendMenuW(menu, MF_STRING, kBeginFullScreenCaptureCommandId, L"全屏截图");
        AppendMenuW(menu, MF_STRING, kBeginFullScreenCaptureAndSaveCommandId, L"全屏截图并保存");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kOpenSettingsDialogCommandId, L"设置...");
        AppendMenuW(default_scope_menu,
                    app_settings_->default_capture_scope == core::CaptureScopeSetting::Region ? MF_STRING | MF_CHECKED
                                                                                              : MF_STRING,
                    kSetDefaultScopeRegionCommandId, L"区域截图");
        AppendMenuW(default_scope_menu,
                    app_settings_->default_capture_scope == core::CaptureScopeSetting::FullScreen
                        ? MF_STRING | MF_CHECKED
                        : MF_STRING,
                    kSetDefaultScopeFullScreenCommandId, L"全屏截图");
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
        auto const default_scope_menu_handle = reinterpret_cast<UINT_PTR>(default_scope_menu);
        AppendMenuW(menu, MF_POPUP, default_scope_menu_handle, L"默认截图范围");
        AppendMenuW(default_action_menu,
                    app_settings_->default_capture_action == core::CaptureActionSetting::CopyOnly
                        ? MF_STRING | MF_CHECKED
                        : MF_STRING,
                    kSetDefaultActionCopyOnlyCommandId, L"仅复制");
        AppendMenuW(default_action_menu,
                    app_settings_->default_capture_action == core::CaptureActionSetting::CopyAndPin
                        ? MF_STRING | MF_CHECKED
                        : MF_STRING,
                    kSetDefaultActionCopyAndPinCommandId, L"复制并贴图");
        AppendMenuW(default_action_menu,
                    app_settings_->default_capture_action == core::CaptureActionSetting::SaveToFile
                        ? MF_STRING | MF_CHECKED
                        : MF_STRING,
                    kSetDefaultActionSaveToFileCommandId, L"直接保存");
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
        auto const default_action_menu_handle = reinterpret_cast<UINT_PTR>(default_action_menu);
        AppendMenuW(menu, MF_POPUP, default_action_menu_handle, L"默认截图动作");
        AppendMenuW(menu, MF_STRING, kEditSettingsFileCommandId, L"编辑配置文件");
        AppendMenuW(menu, MF_STRING, kOpenSettingsDirectoryCommandId, L"打开配置目录");
        AppendMenuW(menu, MF_STRING, kOpenDefaultSaveDirectoryCommandId, L"打开默认保存目录");
        AppendMenuW(menu, MF_STRING, kReloadSettingsCommandId, L"重新加载配置");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        std::wstring const show_all_pins_label = hidden_pin_count == 0
                                                     ? L"恢复全部贴图"
                                                     : BatchPinMenuLabel(
                                                           {.action_prefix = L"恢复", .item_label = L"个隐藏贴图"},
                                                           hidden_pin_count);
        std::wstring const hide_all_pins_label = visible_pin_count == 0 ? L"隐藏全部贴图"
                                                                        : BatchPinMenuLabel({.action_prefix = L"隐藏",
                                                                                             .item_label = L"个贴图"},
                                                                                            visible_pin_count);
        std::wstring const close_all_pins_label = open_pin_count == 0 ? L"关闭全部贴图"
                                                                      : BatchPinMenuLabel({.action_prefix = L"关闭",
                                                                                           .item_label = L"个贴图"},
                                                                                          open_pin_count);

        AppendMenuW(menu, MF_STRING | MF_DISABLED, 0,
                    PinInventorySummaryLabel(visible_pin_count, hidden_pin_count).c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, show_all_pins_flags, kShowAllPinsCommandId, show_all_pins_label.c_str());
        AppendMenuW(menu, hide_all_pins_flags, kHideAllPinsCommandId, hide_all_pins_label.c_str());
        AppendMenuW(menu, close_all_pins_flags, kCloseAllPinsCommandId, close_all_pins_label.c_str());
        AppendMenuW(menu, MF_STRING, kExitApplicationCommandId, L"退出");

        POINT cursor_position{};
        GetCursorPos(&cursor_position);
        SetForegroundWindow(window_);
        TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, cursor_position.x, cursor_position.y, 0,
                       window_, nullptr);
        PostMessageW(window_, WM_NULL, 0, 0);

        DestroyMenu(menu);
    }

    void MainWindow::PaintWindow() noexcept
    {
        PAINTSTRUCT paint{};
        HDC device_context = BeginPaint(window_, &paint);
        RECT client_rect{};
        GetClientRect(window_, &client_rect);
        try
        {
            std::wstring const status_text = CurrentStatusText();
            DrawTextW(device_context, status_text.c_str(), -1, &client_rect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
        catch (...)
        {
            DrawTextW(device_context, app_state_->StatusText(), -1, &client_rect,
                      DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
        EndPaint(window_, &paint);
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
            ShowWindowAndActivate();
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

        ShowWindowAndActivate();
        UpdateWindowPresentation();
    }

    void MainWindow::HandleOverlayResult(feature_capture::OverlayResult result)
    {
        if (result == feature_capture::OverlayResult::PlaceholderCaptured)
        {
            ProcessCaptureResult(feature_capture::ScreenCapture::CaptureRegion(capture_overlay_.LastSelectionRect()));
            return;
        }

        app_state_->ReturnToIdle();
        ShowWindowAndActivate();
        UpdateWindowPresentation();
    }

    bool MainWindow::HandleCommand(WPARAM w_param)
    {
        switch (LOWORD(w_param))
        {
        case kShowWindowCommandId:
            ShowWindowAndActivate();
            return true;

        case kBeginCaptureCommandId:
            BeginCaptureEntry();
            return true;

        case kBeginCaptureAndSaveCommandId:
            BeginCaptureEntry(CaptureRequest{CaptureScope::Region, CaptureAction::SaveToFile});
            return true;

        case kBeginFullScreenCaptureCommandId:
            BeginCaptureEntry(CaptureRequest{CaptureScope::FullScreen, CaptureAction::CopyOnly});
            return true;

        case kBeginFullScreenCaptureAndSaveCommandId:
            BeginCaptureEntry(CaptureRequest{CaptureScope::FullScreen, CaptureAction::SaveToFile});
            return true;

        case kOpenSettingsDialogCommandId:
            OpenSettingsDialog();
            return true;

        case kSetDefaultScopeRegionCommandId: {
            core::AppSettings updated_settings = *app_settings_;
            updated_settings.default_capture_scope = core::CaptureScopeSetting::Region;
            return ApplySettings(std::move(updated_settings), true, L"应用新配置失败，已保留当前配置。",
                                 L"配置保存失败。");
        }

        case kSetDefaultScopeFullScreenCommandId: {
            core::AppSettings updated_settings = *app_settings_;
            updated_settings.default_capture_scope = core::CaptureScopeSetting::FullScreen;
            return ApplySettings(std::move(updated_settings), true, L"应用新配置失败，已保留当前配置。",
                                 L"配置保存失败。");
        }

        case kSetDefaultActionCopyOnlyCommandId: {
            core::AppSettings updated_settings = *app_settings_;
            updated_settings.default_capture_action = core::CaptureActionSetting::CopyOnly;
            return ApplySettings(std::move(updated_settings), true, L"应用新配置失败，已保留当前配置。",
                                 L"配置保存失败。");
        }

        case kSetDefaultActionCopyAndPinCommandId: {
            core::AppSettings updated_settings = *app_settings_;
            updated_settings.default_capture_action = core::CaptureActionSetting::CopyAndPin;
            return ApplySettings(std::move(updated_settings), true, L"应用新配置失败，已保留当前配置。",
                                 L"配置保存失败。");
        }

        case kSetDefaultActionSaveToFileCommandId: {
            core::AppSettings updated_settings = *app_settings_;
            updated_settings.default_capture_action = core::CaptureActionSetting::SaveToFile;
            return ApplySettings(std::move(updated_settings), true, L"应用新配置失败，已保留当前配置。",
                                 L"配置保存失败。");
        }

        case kEditSettingsFileCommandId:
            if (!OpenSettingsFileForEditing())
            {
                ShowMessageDialog(L"CaptureZY", L"无法打开配置文件。", MB_ICONERROR);
            }
            return true;

        case kOpenSettingsDirectoryCommandId:
            if (!OpenSettingsDirectory())
            {
                ShowMessageDialog(L"CaptureZY", L"无法打开配置目录。", MB_ICONERROR);
            }
            return true;

        case kOpenDefaultSaveDirectoryCommandId:
            if (!OpenDefaultSaveDirectory())
            {
                ShowMessageDialog(L"CaptureZY", L"无法打开默认保存目录。", MB_ICONERROR);
            }
            return true;

        case kReloadSettingsCommandId:
            (void)ReloadSettings();
            return true;

        case kShowAllPinsCommandId:
            pin_manager_.ShowAll();
            return true;

        case kHideAllPinsCommandId:
            pin_manager_.HideAll();
            return true;

        case kCloseAllPinsCommandId:
            pin_manager_.CloseAll();
            return true;

        case kExitApplicationCommandId:
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
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
            ShowTrayMenu();
            return true;

        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            ShowWindowAndActivate();
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

        case WM_PAINT:
            PaintWindow();
            return 0;

        case kExecutePendingCaptureMessage:
            ExecutePendingCaptureRequest();
            return 0;

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
