#include "platform_win/main_window.h"

#include <filesystem>
#include <shobjidl.h>
#include <string>
#include <string_view>
#include <utility>
#include <wrl/client.h>

#include "core/app_metadata.h"
#include "core/app_settings_store.h"
#include "feature_capture/capture_file_dialog.h"
#include "feature_capture/screen_capture.h"

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
        constexpr UINT_PTR kResetSettingsToDefaultsCommandId = 1019;
        constexpr UINT_PTR kChooseDefaultSaveDirectoryCommandId = 1020;
        constexpr UINT_PTR kEditDefaultSavePrefixCommandId = 1021;
        constexpr int kCaptureHotkeyId = 1;
        constexpr wchar_t const *kTextInputDialogClassName = L"CaptureZY.TextInputDialog";
        constexpr int kTextInputDialogWidth = 420;
        constexpr int kTextInputDialogHeight = 168;
        constexpr int kTextInputControlId = 2001;
        constexpr int kDialogOkButtonId = IDOK;
        constexpr int kDialogCancelButtonId = IDCANCEL;

        using Microsoft::WRL::ComPtr;

        class TextInputDialog;

        [[nodiscard]] HMENU ControlMenuHandle(int control_id) noexcept
        {
            return static_cast<HMENU>(LongToHandle(control_id));
        }

        [[nodiscard]] WPARAM FontMessageParam(HFONT font) noexcept
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
            return reinterpret_cast<WPARAM>(font);
        }

        void SetDialogUserData(HWND window, TextInputDialog *dialog) noexcept;
        TextInputDialog *GetDialogUserData(HWND window) noexcept;

        class TextInputDialog final
        {
          public:
            TextInputDialog(HINSTANCE instance, HWND owner_window, std::wstring title, std::wstring prompt,
                            std::wstring initial_value) noexcept
                : instance_(instance), owner_window_(owner_window), title_(std::move(title)),
                  prompt_(std::move(prompt)), initial_value_(std::move(initial_value))
            {
            }

            [[nodiscard]] std::optional<std::wstring> Show()
            {
                if (RegisterWindowClass() == 0)
                {
                    return std::nullopt;
                }

                if (owner_window_ != nullptr)
                {
                    EnableWindow(owner_window_, FALSE);
                }

                window_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kTextInputDialogClassName, title_.c_str(),
                                          WS_CAPTION | WS_POPUP | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
                                          kTextInputDialogWidth, kTextInputDialogHeight, owner_window_, nullptr,
                                          instance_, this);
                if (window_ == nullptr)
                {
                    if (owner_window_ != nullptr)
                    {
                        EnableWindow(owner_window_, TRUE);
                    }
                    return std::nullopt;
                }

                CenterToOwner();
                ShowWindow(window_, SW_SHOWNORMAL);
                UpdateWindow(window_);
                SetForegroundWindow(window_);

                MSG message{};
                while (window_ != nullptr && GetMessageW(&message, nullptr, 0, 0) > 0)
                {
                    if (IsDialogMessageW(window_, &message) == FALSE)
                    {
                        TranslateMessage(&message);
                        DispatchMessageW(&message);
                    }
                }

                if (owner_window_ != nullptr)
                {
                    EnableWindow(owner_window_, TRUE);
                    SetForegroundWindow(owner_window_);
                }

                return accepted_ ? std::optional<std::wstring>(value_) : std::nullopt;
            }

          private:
            [[nodiscard]] static ATOM RegisterWindowClass()
            {
                WNDCLASSEXW window_class{};
                window_class.cbSize = sizeof(window_class);
                window_class.lpfnWndProc = TextInputDialog::WindowProc;
                window_class.hInstance = GetModuleHandleW(nullptr);
                window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
                window_class.lpszClassName = kTextInputDialogClassName;

                ATOM const result = RegisterClassExW(&window_class);
                if (result != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
                {
                    return 1;
                }

                return 0;
            }

            void CenterToOwner() const noexcept
            {
                RECT dialog_rect{};
                GetWindowRect(window_, &dialog_rect);

                RECT anchor_rect{};
                if (owner_window_ != nullptr)
                {
                    GetWindowRect(owner_window_, &anchor_rect);
                }
                else
                {
                    anchor_rect.left = 0;
                    anchor_rect.top = 0;
                    anchor_rect.right = GetSystemMetrics(SM_CXSCREEN);
                    anchor_rect.bottom = GetSystemMetrics(SM_CYSCREEN);
                }

                int const dialog_width = dialog_rect.right - dialog_rect.left;
                int const dialog_height = dialog_rect.bottom - dialog_rect.top;
                int const anchor_width = anchor_rect.right - anchor_rect.left;
                int const anchor_height = anchor_rect.bottom - anchor_rect.top;
                int const left = anchor_rect.left + ((anchor_width - dialog_width) / 2);
                int const top = anchor_rect.top + ((anchor_height - dialog_height) / 2);

                SetWindowPos(window_, nullptr, left, top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }

            void InitializeControls()
            {
                auto *const font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

                HWND prompt_label = CreateWindowExW(0, L"STATIC", prompt_.c_str(), WS_CHILD | WS_VISIBLE, 16, 16, 372,
                                                    20, window_, nullptr, instance_, nullptr);
                SendMessageW(prompt_label, WM_SETFONT, FontMessageParam(font), TRUE);

                edit_control_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initial_value_.c_str(),
                                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 16, 44, 372, 24,
                                                window_, ControlMenuHandle(kTextInputControlId), instance_, nullptr);
                SendMessageW(edit_control_, WM_SETFONT, FontMessageParam(font), TRUE);

                HWND ok_button = CreateWindowExW(0, L"BUTTON", L"确定",
                                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 220, 92, 80, 28,
                                                 window_, ControlMenuHandle(kDialogOkButtonId), instance_, nullptr);
                SendMessageW(ok_button, WM_SETFONT, FontMessageParam(font), TRUE);

                HWND cancel_button = CreateWindowExW(
                    0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 308, 92, 80, 28, window_,
                    ControlMenuHandle(kDialogCancelButtonId), instance_, nullptr);
                SendMessageW(cancel_button, WM_SETFONT, FontMessageParam(font), TRUE);

                SetFocus(edit_control_);
                SendMessageW(edit_control_, EM_SETSEL, 0, -1);
            }

            [[nodiscard]] static std::wstring TrimWhitespace(std::wstring const &value)
            {
                std::size_t const first = value.find_first_not_of(L" \t\r\n");
                if (first == std::wstring::npos)
                {
                    return {};
                }

                std::size_t const last = value.find_last_not_of(L" \t\r\n");
                return value.substr(first, last - first + 1);
            }

            void Accept()
            {
                int const text_length = GetWindowTextLengthW(edit_control_);
                std::wstring text(static_cast<std::size_t>(text_length) + 1, L'\0');
                if (text_length > 0)
                {
                    GetWindowTextW(edit_control_, text.data(), text_length + 1);
                }
                text.resize(static_cast<std::size_t>(text_length));

                text = TrimWhitespace(text);
                if (text.empty())
                {
                    MessageBoxW(window_, L"文件名前缀不能为空。", title_.c_str(), MB_OK | MB_ICONWARNING);
                    SetFocus(edit_control_);
                    return;
                }

                value_ = std::move(text);
                accepted_ = true;
                DestroyWindow(window_);
            }

            void Cancel() noexcept
            {
                accepted_ = false;
                DestroyWindow(window_);
            }

            LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param)
            {
                switch (message)
                {
                case WM_CREATE:
                    InitializeControls();
                    return 0;

                case WM_COMMAND:
                    switch (LOWORD(w_param))
                    {
                    case kDialogOkButtonId:
                        Accept();
                        return 0;

                    case kDialogCancelButtonId:
                        Cancel();
                        return 0;

                    default:
                        break;
                    }
                    break;

                case WM_CLOSE:
                    Cancel();
                    return 0;

                case WM_DESTROY:
                    window_ = nullptr;
                    return 0;

                default:
                    break;
                }

                return DefWindowProcW(window_, message, w_param, l_param);
            }

            static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
            {
                if (message == WM_NCCREATE)
                {
                    // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
                    auto *create_struct = reinterpret_cast<CREATESTRUCTW *>(l_param);
                    auto *dialog = static_cast<TextInputDialog *>(create_struct->lpCreateParams);
                    SetDialogUserData(window, dialog);
                    dialog->window_ = window;
                }

                auto *dialog = GetDialogUserData(window);
                if (dialog != nullptr)
                {
                    return dialog->HandleMessage(message, w_param, l_param);
                }

                return DefWindowProcW(window, message, w_param, l_param);
            }

            HINSTANCE instance_{};
            HWND owner_window_{};
            HWND window_{};
            HWND edit_control_{};
            std::wstring title_;
            std::wstring prompt_;
            std::wstring initial_value_;
            std::wstring value_;
            bool accepted_{false};
        };

        void SetDialogUserData(HWND window, TextInputDialog *dialog) noexcept
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dialog));
        }

        TextInputDialog *GetDialogUserData(HWND window) noexcept
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
            return reinterpret_cast<TextInputDialog *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }

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

    bool MainWindow::SaveSettings(core::AppSettings previous_settings)
    {
        if (core::AppSettingsStore::Save(*app_settings_))
        {
            return true;
        }

        *app_settings_ = std::move(previous_settings);
        ShowMessageDialog(L"CaptureZY", L"配置保存失败。", MB_ICONERROR);
        return false;
    }

    std::optional<std::wstring> MainWindow::PromptForDefaultSavePrefix(std::wstring_view initial_value) const
    {
        TextInputDialog dialog(instance_, window_, L"设置默认文件名前缀", L"输入默认文件名前缀：",
                               std::wstring(initial_value));
        return dialog.Show();
    }

    std::optional<std::wstring> MainWindow::PickDefaultSaveDirectory() const
    {
        ComPtr<IFileOpenDialog> dialog;
        HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(dialog.GetAddressOf()));
        if (FAILED(result) || dialog == nullptr)
        {
            ShowMessageDialog(L"CaptureZY", L"无法创建文件夹选择对话框。", MB_ICONERROR);
            return std::nullopt;
        }

        DWORD options = 0;
        result = dialog->GetOptions(&options);
        if (FAILED(result))
        {
            ShowMessageDialog(L"CaptureZY", L"无法初始化文件夹选择对话框。", MB_ICONERROR);
            return std::nullopt;
        }

        result = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (FAILED(result))
        {
            ShowMessageDialog(L"CaptureZY", L"无法设置文件夹选择选项。", MB_ICONERROR);
            return std::nullopt;
        }

        result = dialog->SetTitle(L"选择默认保存目录");
        if (FAILED(result))
        {
            ShowMessageDialog(L"CaptureZY", L"无法设置文件夹选择标题。", MB_ICONERROR);
            return std::nullopt;
        }

        if (!app_settings_->default_save_directory.empty())
        {
            ComPtr<IShellItem> current_directory;
            result = SHCreateItemFromParsingName(app_settings_->default_save_directory.c_str(), nullptr,
                                                 IID_PPV_ARGS(current_directory.GetAddressOf()));
            if (SUCCEEDED(result) && current_directory != nullptr)
            {
                (void)dialog->SetFolder(current_directory.Get());
            }
        }

        result = dialog->Show(window_);
        if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        {
            return std::nullopt;
        }

        if (FAILED(result))
        {
            ShowMessageDialog(L"CaptureZY", L"无法打开文件夹选择对话框。", MB_ICONERROR);
            return std::nullopt;
        }

        ComPtr<IShellItem> selected_item;
        result = dialog->GetResult(selected_item.GetAddressOf());
        if (FAILED(result) || selected_item == nullptr)
        {
            ShowMessageDialog(L"CaptureZY", L"无法读取选择的保存目录。", MB_ICONERROR);
            return std::nullopt;
        }

        PWSTR selected_path = nullptr;
        result = selected_item->GetDisplayName(SIGDN_FILESYSPATH, &selected_path);
        if (FAILED(result) || selected_path == nullptr)
        {
            ShowMessageDialog(L"CaptureZY", L"无法解析选择的保存目录。", MB_ICONERROR);
            return std::nullopt;
        }

        std::wstring directory_path = selected_path;
        CoTaskMemFree(selected_path);
        return directory_path;
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

    bool MainWindow::ResetSettingsToDefaults()
    {
        core::AppSettings const previous_settings = *app_settings_;
        core::AppSettings const default_settings = core::AppSettingsStore::LoadDefaults();

        UnregisterHotkeys();
        *app_settings_ = default_settings;
        hotkeys_registered_ = RegisterHotkeys();
        if (!hotkeys_registered_)
        {
            *app_settings_ = previous_settings;
            hotkeys_registered_ = RegisterHotkeys();
            ShowMessageDialog(L"CaptureZY", L"恢复默认配置失败，已保留当前配置。", MB_ICONWARNING);
            return false;
        }

        if (core::AppSettingsStore::Save(*app_settings_))
        {
            ShowMessageDialog(L"CaptureZY", L"已恢复默认配置。", MB_ICONINFORMATION);
            return true;
        }

        UnregisterHotkeys();
        *app_settings_ = previous_settings;
        hotkeys_registered_ = RegisterHotkeys();
        ShowMessageDialog(L"CaptureZY", L"默认配置已生成，但写回文件失败，已恢复当前配置。", MB_ICONWARNING);
        return false;
    }

    bool MainWindow::ReloadSettings()
    {
        core::AppSettings const previous_settings = *app_settings_;
        core::AppSettings const reloaded_settings = core::AppSettingsStore::Load();

        UnregisterHotkeys();
        *app_settings_ = reloaded_settings;
        hotkeys_registered_ = RegisterHotkeys();
        if (hotkeys_registered_)
        {
            ShowMessageDialog(L"CaptureZY", L"配置已重载。", MB_ICONINFORMATION);
            return true;
        }

        *app_settings_ = previous_settings;
        hotkeys_registered_ = RegisterHotkeys();
        ShowMessageDialog(L"CaptureZY", L"配置已读取，但新热键注册失败，已恢复旧热键。", MB_ICONWARNING);
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

    void MainWindow::ShowTrayMenu() noexcept
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
        AppendMenuW(menu, MF_STRING, kChooseDefaultSaveDirectoryCommandId, L"选择默认保存目录");
        AppendMenuW(menu, MF_STRING, kOpenDefaultSaveDirectoryCommandId, L"打开默认保存目录");
        AppendMenuW(menu, MF_STRING, kEditDefaultSavePrefixCommandId, L"设置默认文件名前缀");
        AppendMenuW(menu, MF_STRING, kReloadSettingsCommandId, L"重新加载配置");
        AppendMenuW(menu, MF_STRING, kResetSettingsToDefaultsCommandId, L"恢复默认配置");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, show_all_pins_flags, kShowAllPinsCommandId, L"显示全部贴图");
        AppendMenuW(menu, hide_all_pins_flags, kHideAllPinsCommandId, L"隐藏全部贴图");
        AppendMenuW(menu, close_all_pins_flags, kCloseAllPinsCommandId, L"关闭全部贴图");
        AppendMenuW(menu, MF_STRING, kExitApplicationCommandId, L"退出");

        POINT cursor_position{};
        GetCursorPos(&cursor_position);
        SetForegroundWindow(window_);
        TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, cursor_position.x, cursor_position.y, 0,
                       window_, nullptr);
        PostMessageW(window_, WM_NULL, 0, 0);

        DestroyMenu(menu);
    }

    void MainWindow::PaintWindow() const noexcept
    {
        PAINTSTRUCT paint{};
        HDC device_context = BeginPaint(window_, &paint);
        RECT client_rect{};
        GetClientRect(window_, &client_rect);
        DrawTextW(device_context, app_state_->StatusText(), -1, &client_rect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
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

        case kSetDefaultScopeRegionCommandId: {
            core::AppSettings const previous_settings = *app_settings_;
            app_settings_->default_capture_scope = core::CaptureScopeSetting::Region;
            return SaveSettings(previous_settings);
        }

        case kSetDefaultScopeFullScreenCommandId: {
            core::AppSettings const previous_settings = *app_settings_;
            app_settings_->default_capture_scope = core::CaptureScopeSetting::FullScreen;
            return SaveSettings(previous_settings);
        }

        case kSetDefaultActionCopyOnlyCommandId: {
            core::AppSettings const previous_settings = *app_settings_;
            app_settings_->default_capture_action = core::CaptureActionSetting::CopyOnly;
            return SaveSettings(previous_settings);
        }

        case kSetDefaultActionCopyAndPinCommandId: {
            core::AppSettings const previous_settings = *app_settings_;
            app_settings_->default_capture_action = core::CaptureActionSetting::CopyAndPin;
            return SaveSettings(previous_settings);
        }

        case kSetDefaultActionSaveToFileCommandId: {
            core::AppSettings const previous_settings = *app_settings_;
            app_settings_->default_capture_action = core::CaptureActionSetting::SaveToFile;
            return SaveSettings(previous_settings);
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

        case kChooseDefaultSaveDirectoryCommandId:
            if (auto selected_directory = PickDefaultSaveDirectory(); selected_directory.has_value())
            {
                core::AppSettings const previous_settings = *app_settings_;
                app_settings_->default_save_directory = std::move(*selected_directory);
                if (SaveSettings(previous_settings))
                {
                    ShowMessageDialog(L"CaptureZY", L"默认保存目录已更新。", MB_ICONINFORMATION);
                }
            }
            return true;

        case kOpenDefaultSaveDirectoryCommandId:
            if (!OpenDefaultSaveDirectory())
            {
                ShowMessageDialog(L"CaptureZY", L"无法打开默认保存目录。", MB_ICONERROR);
            }
            return true;

        case kEditDefaultSavePrefixCommandId:
            if (auto new_prefix = PromptForDefaultSavePrefix(app_settings_->default_save_file_prefix);
                new_prefix.has_value())
            {
                core::AppSettings const previous_settings = *app_settings_;
                app_settings_->default_save_file_prefix = std::move(*new_prefix);
                if (SaveSettings(previous_settings))
                {
                    ShowMessageDialog(L"CaptureZY", L"默认文件名前缀已更新。", MB_ICONINFORMATION);
                }
            }
            return true;

        case kReloadSettingsCommandId:
            (void)ReloadSettings();
            return true;

        case kResetSettingsToDefaultsCommandId:
            (void)ResetSettingsToDefaults();
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
