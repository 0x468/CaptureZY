#include "platform_win/settings_dialog.h"

// clang-format off
#include <commctrl.h>
// clang-format on

#include <shobjidl.h>
#include <string>
#include <string_view>
#include <utility>
#include <wrl/client.h>

#include "core/app_settings_store.h"

namespace capturezy::platform_win
{
    namespace
    {
        constexpr wchar_t const *kTextInputDialogClassName = L"CaptureZY.TextInputDialog";
        constexpr int kTextInputDialogWidth = 420;
        constexpr int kTextInputDialogHeight = 168;
        constexpr int kTextInputControlId = 2001;
        constexpr wchar_t const *kSettingsDialogClassName = L"CaptureZY.SettingsDialog";
        constexpr int kSettingsDialogWidth = 560;
        constexpr int kSettingsDialogHeight = 292;
        constexpr int kScopeComboId = 3001;
        constexpr int kActionComboId = 3002;
        constexpr int kHotkeyControlId = 3003;
        constexpr int kSaveDirectoryEditId = 3004;
        constexpr int kBrowseSaveDirectoryButtonId = 3005;
        constexpr int kWinModifierCheckboxId = 3006;
        constexpr int kSavePrefixEditId = 3007;
        constexpr int kResetDefaultsButtonId = 3008;
        constexpr int kDialogOkButtonId = IDOK;
        constexpr int kDialogCancelButtonId = IDCANCEL;

        using Microsoft::WRL::ComPtr;

        [[nodiscard]] HMENU ControlMenuHandle(int control_id) noexcept
        {
            return static_cast<HMENU>(LongToHandle(control_id));
        }

        [[nodiscard]] WPARAM FontMessageParam(HFONT font) noexcept
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
            return reinterpret_cast<WPARAM>(font);
        }

        template <typename T> void SetWindowUserData(HWND window, T *object) noexcept
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(object));
        }

        template <typename T> [[nodiscard]] T *GetWindowUserData(HWND window) noexcept
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
            return reinterpret_cast<T *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }

        void EnsureCommonControlsInitialized() noexcept
        {
            static bool const initialized = []() {
                INITCOMMONCONTROLSEX common_controls{};
                common_controls.dwSize = sizeof(common_controls);
                common_controls.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
                return InitCommonControlsEx(&common_controls) != FALSE;
            }();
            (void)initialized;
        }

        void CenterWindowToOwner(HWND window, HWND owner_window) noexcept
        {
            RECT dialog_rect{};
            GetWindowRect(window, &dialog_rect);

            RECT anchor_rect{};
            if (owner_window != nullptr)
            {
                GetWindowRect(owner_window, &anchor_rect);
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

            SetWindowPos(window, nullptr, left, top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }

        [[nodiscard]] std::wstring TrimWhitespace(std::wstring const &value)
        {
            std::size_t const first = value.find_first_not_of(L" \t\r\n");
            if (first == std::wstring::npos)
            {
                return {};
            }

            std::size_t const last = value.find_last_not_of(L" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        [[nodiscard]] std::wstring ReadWindowText(HWND window)
        {
            int const text_length = GetWindowTextLengthW(window);
            std::wstring text(static_cast<std::size_t>(text_length) + 1, L'\0');
            if (text_length > 0)
            {
                GetWindowTextW(window, text.data(), text_length + 1);
            }
            text.resize(static_cast<std::size_t>(text_length));
            return text;
        }

        [[nodiscard]] BYTE HotkeyFlagsFromModifiers(UINT modifiers) noexcept
        {
            BYTE flags = 0;
            if ((modifiers & MOD_ALT) != 0)
            {
                flags |= HOTKEYF_ALT;
            }
            if ((modifiers & MOD_CONTROL) != 0)
            {
                flags |= HOTKEYF_CONTROL;
            }
            if ((modifiers & MOD_SHIFT) != 0)
            {
                flags |= HOTKEYF_SHIFT;
            }

            return flags;
        }

        [[nodiscard]] UINT ModifiersFromHotkeyFlags(BYTE hotkey_flags) noexcept
        {
            UINT modifiers = MOD_NOREPEAT;
            if ((hotkey_flags & HOTKEYF_ALT) != 0)
            {
                modifiers |= MOD_ALT;
            }
            if ((hotkey_flags & HOTKEYF_CONTROL) != 0)
            {
                modifiers |= MOD_CONTROL;
            }
            if ((hotkey_flags & HOTKEYF_SHIFT) != 0)
            {
                modifiers |= MOD_SHIFT;
            }

            return modifiers;
        }

        [[nodiscard]] WORD HotkeyWordFromSetting(core::HotkeySetting const &hotkey) noexcept
        {
            return MAKEWORD(static_cast<BYTE>(hotkey.virtual_key), HotkeyFlagsFromModifiers(hotkey.modifiers));
        }

        [[nodiscard]] core::HotkeySetting HotkeySettingFromDialog(WORD hotkey_word, bool include_win_modifier) noexcept
        {
            core::HotkeySetting hotkey{};
            hotkey.virtual_key = LOBYTE(hotkey_word);
            hotkey.modifiers = ModifiersFromHotkeyFlags(HIBYTE(hotkey_word));
            if (include_win_modifier)
            {
                hotkey.modifiers |= MOD_WIN;
            }

            return hotkey;
        }

        [[nodiscard]] bool HasAnyHotkeyModifier(core::HotkeySetting const &hotkey) noexcept
        {
            return (hotkey.modifiers & (MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN)) != 0;
        }

        void SetCheckboxState(HWND checkbox, bool is_checked) noexcept
        {
            SendMessageW(checkbox, BM_SETCHECK, is_checked ? BST_CHECKED : BST_UNCHECKED, 0);
        }

        [[nodiscard]] bool IsCheckboxChecked(HWND checkbox) noexcept
        {
            return SendMessageW(checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
        }

        [[nodiscard]] int ScopeSelectionIndex(core::CaptureScopeSetting scope) noexcept
        {
            return scope == core::CaptureScopeSetting::FullScreen ? 1 : 0;
        }

        [[nodiscard]] core::CaptureScopeSetting ScopeSettingFromIndex(int selection_index) noexcept
        {
            return selection_index == 1 ? core::CaptureScopeSetting::FullScreen : core::CaptureScopeSetting::Region;
        }

        [[nodiscard]] int ActionSelectionIndex(core::CaptureActionSetting action) noexcept
        {
            switch (action)
            {
            case core::CaptureActionSetting::CopyOnly:
                return 0;

            case core::CaptureActionSetting::SaveToFile:
                return 2;

            case core::CaptureActionSetting::CopyAndPin:
            default:
                return 1;
            }
        }

        [[nodiscard]] core::CaptureActionSetting ActionSettingFromIndex(int selection_index) noexcept
        {
            switch (selection_index)
            {
            case 0:
                return core::CaptureActionSetting::CopyOnly;

            case 2:
                return core::CaptureActionSetting::SaveToFile;

            case 1:
            default:
                return core::CaptureActionSetting::CopyAndPin;
            }
        }

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
                if (RegisterWindowClass(instance_) == 0)
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

                CenterWindowToOwner(window_, owner_window_);
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
            [[nodiscard]] static ATOM RegisterWindowClass(HINSTANCE instance)
            {
                WNDCLASSEXW window_class{};
                window_class.cbSize = sizeof(window_class);
                window_class.lpfnWndProc = TextInputDialog::WindowProc;
                window_class.hInstance = instance;
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

            void Accept()
            {
                std::wstring text = TrimWhitespace(ReadWindowText(edit_control_));
                if (text.empty())
                {
                    MessageBoxW(window_, L"输入内容不能为空。", title_.c_str(), MB_OK | MB_ICONWARNING);
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
                    SetWindowUserData(window, dialog);
                    dialog->window_ = window;
                }

                if (auto *dialog = GetWindowUserData<TextInputDialog>(window); dialog != nullptr)
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

        class SettingsDialog final
        {
          public:
            SettingsDialog(HINSTANCE instance, HWND owner_window, core::AppSettings initial_settings)
                : instance_(instance), owner_window_(owner_window), initial_settings_(std::move(initial_settings))
            {
            }

            [[nodiscard]] std::optional<core::AppSettings> Show()
            {
                EnsureCommonControlsInitialized();
                if (RegisterWindowClass(instance_) == 0)
                {
                    return std::nullopt;
                }

                if (owner_window_ != nullptr)
                {
                    EnableWindow(owner_window_, FALSE);
                }

                window_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kSettingsDialogClassName, L"CaptureZY 设置",
                                          WS_CAPTION | WS_POPUP | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
                                          kSettingsDialogWidth, kSettingsDialogHeight, owner_window_, nullptr,
                                          instance_, this);
                if (window_ == nullptr)
                {
                    if (owner_window_ != nullptr)
                    {
                        EnableWindow(owner_window_, TRUE);
                    }
                    return std::nullopt;
                }

                CenterWindowToOwner(window_, owner_window_);
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

                return accepted_ ? std::optional<core::AppSettings>(result_settings_) : std::nullopt;
            }

          private:
            [[nodiscard]] static ATOM RegisterWindowClass(HINSTANCE instance)
            {
                WNDCLASSEXW window_class{};
                window_class.cbSize = sizeof(window_class);
                window_class.lpfnWndProc = SettingsDialog::WindowProc;
                window_class.hInstance = instance;
                window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
                window_class.lpszClassName = kSettingsDialogClassName;

                ATOM const result = RegisterClassExW(&window_class);
                if (result != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
                {
                    return 1;
                }

                return 0;
            }

            void InitializeControls()
            {
                auto *const font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                int constexpr kLabelX = 20;
                int constexpr kControlX = 146;
                int constexpr kLabelWidth = 110;
                int constexpr kControlWidth = 292;
                int constexpr kButtonWidth = 78;

                HWND scope_label = CreateWindowExW(0, L"STATIC", L"默认截图范围", WS_CHILD | WS_VISIBLE, kLabelX, 24,
                                                   kLabelWidth, 20, window_, nullptr, instance_, nullptr);
                SendMessageW(scope_label, WM_SETFONT, FontMessageParam(font), TRUE);

                scope_combo_ = CreateWindowExW(
                    0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                    kControlX, 20, kControlWidth, 120, window_, ControlMenuHandle(kScopeComboId), instance_, nullptr);
                SendMessageW(scope_combo_, WM_SETFONT, FontMessageParam(font), TRUE);
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
                SendMessageW(scope_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"区域截图"));
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
                SendMessageW(scope_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"全屏截图"));

                HWND action_label = CreateWindowExW(0, L"STATIC", L"默认截图动作", WS_CHILD | WS_VISIBLE, kLabelX, 60,
                                                    kLabelWidth, 20, window_, nullptr, instance_, nullptr);
                SendMessageW(action_label, WM_SETFONT, FontMessageParam(font), TRUE);

                action_combo_ = CreateWindowExW(
                    0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                    kControlX, 56, kControlWidth, 140, window_, ControlMenuHandle(kActionComboId), instance_, nullptr);
                SendMessageW(action_combo_, WM_SETFONT, FontMessageParam(font), TRUE);
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
                SendMessageW(action_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"仅复制"));
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
                SendMessageW(action_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"复制并贴图"));
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
                SendMessageW(action_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"直接保存"));

                HWND hotkey_label = CreateWindowExW(0, L"STATIC", L"截图热键", WS_CHILD | WS_VISIBLE, kLabelX, 96,
                                                    kLabelWidth, 20, window_, nullptr, instance_, nullptr);
                SendMessageW(hotkey_label, WM_SETFONT, FontMessageParam(font), TRUE);

                hotkey_control_ = CreateWindowExW(0, HOTKEY_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                                  kControlX, 92, 212, 24, window_, ControlMenuHandle(kHotkeyControlId),
                                                  instance_, nullptr);
                SendMessageW(hotkey_control_, WM_SETFONT, FontMessageParam(font), TRUE);

                win_modifier_checkbox_ = CreateWindowExW(
                    0, L"BUTTON", L"Win", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, kControlX + 224, 94, 64,
                    20, window_, ControlMenuHandle(kWinModifierCheckboxId), instance_, nullptr);
                SendMessageW(win_modifier_checkbox_, WM_SETFONT, FontMessageParam(font), TRUE);

                HWND hotkey_hint = CreateWindowExW(0, L"STATIC", L"至少包含一个修饰键。", WS_CHILD | WS_VISIBLE,
                                                   kControlX, 118, kControlWidth, 18, window_, nullptr, instance_,
                                                   nullptr);
                SendMessageW(hotkey_hint, WM_SETFONT, FontMessageParam(font), TRUE);

                HWND save_directory_label = CreateWindowExW(0, L"STATIC", L"默认保存目录", WS_CHILD | WS_VISIBLE,
                                                            kLabelX, 152, kLabelWidth, 20, window_, nullptr, instance_,
                                                            nullptr);
                SendMessageW(save_directory_label, WM_SETFONT, FontMessageParam(font), TRUE);

                save_directory_edit_ = CreateWindowExW(
                    WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, kControlX,
                    148, kControlWidth, 24, window_, ControlMenuHandle(kSaveDirectoryEditId), instance_, nullptr);
                SendMessageW(save_directory_edit_, WM_SETFONT, FontMessageParam(font), TRUE);

                HWND browse_button = CreateWindowExW(
                    0, L"BUTTON", L"浏览...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    kControlX + kControlWidth + 8, 147, kButtonWidth, 26, window_,
                    ControlMenuHandle(kBrowseSaveDirectoryButtonId), instance_, nullptr);
                SendMessageW(browse_button, WM_SETFONT, FontMessageParam(font), TRUE);

                HWND save_prefix_label = CreateWindowExW(0, L"STATIC", L"默认文件名前缀", WS_CHILD | WS_VISIBLE,
                                                         kLabelX, 188, kLabelWidth, 20, window_, nullptr, instance_,
                                                         nullptr);
                SendMessageW(save_prefix_label, WM_SETFONT, FontMessageParam(font), TRUE);

                save_prefix_edit_ = CreateWindowExW(
                    WS_EX_CLIENTEDGE, L"EDIT", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, kControlX,
                    184, kControlWidth, 24, window_, ControlMenuHandle(kSavePrefixEditId), instance_, nullptr);
                SendMessageW(save_prefix_edit_, WM_SETFONT, FontMessageParam(font), TRUE);

                HWND reset_button = CreateWindowExW(
                    0, L"BUTTON", L"恢复默认", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 210, 230, 88, 28,
                    window_, ControlMenuHandle(kResetDefaultsButtonId), instance_, nullptr);
                SendMessageW(reset_button, WM_SETFONT, FontMessageParam(font), TRUE);

                HWND ok_button = CreateWindowExW(0, L"BUTTON", L"确定",
                                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 310, 230, 88,
                                                 28, window_, ControlMenuHandle(kDialogOkButtonId), instance_, nullptr);
                SendMessageW(ok_button, WM_SETFONT, FontMessageParam(font), TRUE);

                HWND cancel_button = CreateWindowExW(
                    0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 410, 230, 88, 28,
                    window_, ControlMenuHandle(kDialogCancelButtonId), instance_, nullptr);
                SendMessageW(cancel_button, WM_SETFONT, FontMessageParam(font), TRUE);

                LoadSettingsIntoControls(initial_settings_);
                SetFocus(scope_combo_);
            }

            void LoadSettingsIntoControls(core::AppSettings const &settings)
            {
                SendMessageW(scope_combo_, CB_SETCURSEL, ScopeSelectionIndex(settings.default_capture_scope), 0);
                SendMessageW(action_combo_, CB_SETCURSEL, ActionSelectionIndex(settings.default_capture_action), 0);
                SendMessageW(hotkey_control_, HKM_SETHOTKEY, HotkeyWordFromSetting(settings.capture_hotkey), 0);
                SetCheckboxState(win_modifier_checkbox_, (settings.capture_hotkey.modifiers & MOD_WIN) != 0);
                SetWindowTextW(save_directory_edit_, settings.default_save_directory.c_str());
                SetWindowTextW(save_prefix_edit_, settings.default_save_file_prefix.c_str());
            }

            void BrowseForSaveDirectory()
            {
                if (auto selected_directory = PickDirectoryDialog(window_, L"选择默认保存目录",
                                                                  ReadWindowText(save_directory_edit_));
                    selected_directory.has_value())
                {
                    SetWindowTextW(save_directory_edit_, selected_directory->c_str());
                }
            }

            void ResetToDefaults()
            {
                LoadSettingsIntoControls(core::AppSettingsStore::LoadDefaults());
            }

            void Accept()
            {
                core::AppSettings settings = initial_settings_;
                settings.default_capture_scope = ScopeSettingFromIndex(
                    static_cast<int>(SendMessageW(scope_combo_, CB_GETCURSEL, 0, 0)));
                settings.default_capture_action = ActionSettingFromIndex(
                    static_cast<int>(SendMessageW(action_combo_, CB_GETCURSEL, 0, 0)));
                settings.default_save_directory = TrimWhitespace(ReadWindowText(save_directory_edit_));
                settings.default_save_file_prefix = TrimWhitespace(ReadWindowText(save_prefix_edit_));
                settings.capture_hotkey = HotkeySettingFromDialog(
                    static_cast<WORD>(SendMessageW(hotkey_control_, HKM_GETHOTKEY, 0, 0)),
                    IsCheckboxChecked(win_modifier_checkbox_));

                if (settings.capture_hotkey.virtual_key == 0)
                {
                    MessageBoxW(window_, L"截图热键不能为空。", L"CaptureZY 设置", MB_OK | MB_ICONWARNING);
                    SetFocus(hotkey_control_);
                    return;
                }

                if (!HasAnyHotkeyModifier(settings.capture_hotkey))
                {
                    MessageBoxW(window_, L"截图热键至少需要一个修饰键。", L"CaptureZY 设置", MB_OK | MB_ICONWARNING);
                    SetFocus(hotkey_control_);
                    return;
                }

                if (settings.default_save_directory.empty())
                {
                    MessageBoxW(window_, L"默认保存目录不能为空。", L"CaptureZY 设置", MB_OK | MB_ICONWARNING);
                    SetFocus(save_directory_edit_);
                    return;
                }

                if (settings.default_save_file_prefix.empty())
                {
                    MessageBoxW(window_, L"默认文件名前缀不能为空。", L"CaptureZY 设置", MB_OK | MB_ICONWARNING);
                    SetFocus(save_prefix_edit_);
                    return;
                }

                result_settings_ = std::move(settings);
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
                    case kBrowseSaveDirectoryButtonId:
                        BrowseForSaveDirectory();
                        return 0;

                    case kResetDefaultsButtonId:
                        ResetToDefaults();
                        return 0;

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
                    auto *dialog = static_cast<SettingsDialog *>(create_struct->lpCreateParams);
                    SetWindowUserData(window, dialog);
                    dialog->window_ = window;
                }

                if (auto *dialog = GetWindowUserData<SettingsDialog>(window); dialog != nullptr)
                {
                    return dialog->HandleMessage(message, w_param, l_param);
                }

                return DefWindowProcW(window, message, w_param, l_param);
            }

            HINSTANCE instance_{};
            HWND owner_window_{};
            HWND window_{};
            HWND scope_combo_{};
            HWND action_combo_{};
            HWND hotkey_control_{};
            HWND win_modifier_checkbox_{};
            HWND save_directory_edit_{};
            HWND save_prefix_edit_{};
            core::AppSettings initial_settings_{};
            core::AppSettings result_settings_{};
            bool accepted_{false};
        };
    } // namespace

    std::optional<std::wstring> PickDirectoryDialog(HWND owner_window, wchar_t const *title,
                                                    std::wstring_view initial_directory)
    {
        ComPtr<IFileOpenDialog> dialog;
        HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(dialog.GetAddressOf()));
        if (FAILED(result) || dialog == nullptr)
        {
            MessageBoxW(owner_window, L"无法创建文件夹选择对话框。", L"CaptureZY", MB_OK | MB_ICONERROR);
            return std::nullopt;
        }

        DWORD options = 0;
        result = dialog->GetOptions(&options);
        if (FAILED(result))
        {
            MessageBoxW(owner_window, L"无法初始化文件夹选择对话框。", L"CaptureZY", MB_OK | MB_ICONERROR);
            return std::nullopt;
        }

        result = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        if (FAILED(result))
        {
            MessageBoxW(owner_window, L"无法设置文件夹选择选项。", L"CaptureZY", MB_OK | MB_ICONERROR);
            return std::nullopt;
        }

        result = dialog->SetTitle(title != nullptr ? title : L"");
        if (FAILED(result))
        {
            MessageBoxW(owner_window, L"无法设置文件夹选择标题。", L"CaptureZY", MB_OK | MB_ICONERROR);
            return std::nullopt;
        }

        if (!initial_directory.empty())
        {
            ComPtr<IShellItem> current_directory;
            std::wstring const initial_directory_path(initial_directory);
            result = SHCreateItemFromParsingName(initial_directory_path.c_str(), nullptr,
                                                 IID_PPV_ARGS(current_directory.GetAddressOf()));
            if (SUCCEEDED(result) && current_directory != nullptr)
            {
                (void)dialog->SetFolder(current_directory.Get());
            }
        }

        result = dialog->Show(owner_window);
        if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        {
            return std::nullopt;
        }

        if (FAILED(result))
        {
            MessageBoxW(owner_window, L"无法打开文件夹选择对话框。", L"CaptureZY", MB_OK | MB_ICONERROR);
            return std::nullopt;
        }

        ComPtr<IShellItem> selected_item;
        result = dialog->GetResult(selected_item.GetAddressOf());
        if (FAILED(result) || selected_item == nullptr)
        {
            MessageBoxW(owner_window, L"无法读取选择的保存目录。", L"CaptureZY", MB_OK | MB_ICONERROR);
            return std::nullopt;
        }

        PWSTR selected_path = nullptr;
        result = selected_item->GetDisplayName(SIGDN_FILESYSPATH, &selected_path);
        if (FAILED(result) || selected_path == nullptr)
        {
            MessageBoxW(owner_window, L"无法解析选择的保存目录。", L"CaptureZY", MB_OK | MB_ICONERROR);
            return std::nullopt;
        }

        std::wstring directory_path = selected_path;
        CoTaskMemFree(selected_path);
        return directory_path;
    }

    std::optional<std::wstring> ShowTextInputDialog(HINSTANCE instance, HWND owner_window, std::wstring title,
                                                    std::wstring prompt, std::wstring initial_value)
    {
        TextInputDialog dialog(instance, owner_window, std::move(title), std::move(prompt), std::move(initial_value));
        return dialog.Show();
    }

    std::optional<core::AppSettings> ShowSettingsDialog(HINSTANCE instance, HWND owner_window,
                                                        core::AppSettings const &initial_settings)
    {
        SettingsDialog dialog(instance, owner_window, initial_settings);
        return dialog.Show();
    }
} // namespace capturezy::platform_win
