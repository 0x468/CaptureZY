#include "platform_win/main_window.h"

#include <array>
#include <chrono>
#include <commdlg.h>
#include <ctime>
#include <string>
#include <utility>

#include "core/app_metadata.h"
#include "feature_capture/screen_capture.h"

namespace capturezy::platform_win
{
    namespace
    {
        constexpr UINT kTrayIconId = 1;
        constexpr UINT kTrayMessage = WM_APP + 1;
        constexpr UINT_PTR kShowWindowCommandId = 1001;
        constexpr UINT_PTR kBeginCaptureCommandId = 1002;
        constexpr UINT_PTR kBeginCaptureAndSaveCommandId = 1003;
        constexpr UINT_PTR kShowAllPinsCommandId = 1004;
        constexpr UINT_PTR kHideAllPinsCommandId = 1005;
        constexpr UINT_PTR kCloseAllPinsCommandId = 1006;
        constexpr UINT_PTR kExitApplicationCommandId = 1007;
        constexpr int kCaptureHotkeyId = 1;
        constexpr UINT kCaptureHotkeyModifiers = MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT;
        constexpr UINT kCaptureHotkeyVirtualKey = 0x41;
        constexpr auto kSaveDialogFilter = std::to_array(L"PNG Files (*.png)\0*.png\0");

        [[nodiscard]] std::wstring BuildDefaultFileName(feature_capture::CaptureResult::Timestamp captured_at)
        {
            std::time_t const captured_time = std::chrono::system_clock::to_time_t(captured_at);
            std::tm local_time{};
            localtime_s(&local_time, &captured_time);

            std::array<wchar_t, 64> file_name{};
            wcsftime(file_name.data(), file_name.size(), L"CaptureZY_%Y%m%d_%H%M%S.png", &local_time);
            return file_name.data();
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
    } // namespace

    MainWindow::MainWindow(HINSTANCE instance, core::AppState &app_state) noexcept
        : instance_(instance), app_state_(&app_state), capture_overlay_(instance), pin_manager_(instance)
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
        return RegisterHotKey(window_, kCaptureHotkeyId, kCaptureHotkeyModifiers, kCaptureHotkeyVirtualKey) != FALSE;
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

    void MainWindow::BeginCaptureEntry(CaptureAction capture_action)
    {
        pending_capture_action_ = capture_action;
        app_state_->BeginCapture();
        UpdateWindowPresentation();
        HideToTray();

        if (!capture_overlay_.Show(window_))
        {
            app_state_->ReturnToIdle();
            ShowWindowAndActivate();
            UpdateWindowPresentation();
        }
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

    void MainWindow::HandleOverlayResult(feature_capture::OverlayResult result)
    {
        bool capture_completed = false;
        bool capture_saved = false;
        bool pin_created = false;

        if (result == feature_capture::OverlayResult::PlaceholderCaptured)
        {
            auto capture_result = feature_capture::ScreenCapture::CaptureRegion(capture_overlay_.LastSelectionRect());
            if (capture_result.IsValid())
            {
                switch (pending_capture_action_)
                {
                case CaptureAction::SaveToFile: {
                    std::array<wchar_t, 32768> file_path{};
                    std::wstring const default_file_name = BuildDefaultFileName(capture_result.CapturedAt());
                    wcsncpy_s(file_path.data(), file_path.size(), default_file_name.c_str(), _TRUNCATE);

                    OPENFILENAMEW dialog{};
                    dialog.lStructSize = sizeof(dialog);
                    dialog.hwndOwner = window_;
                    dialog.lpstrFilter = kSaveDialogFilter.data();
                    dialog.lpstrFile = file_path.data();
                    dialog.nMaxFile = static_cast<DWORD>(file_path.size());
                    dialog.lpstrDefExt = L"png";
                    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;

                    if (GetSaveFileNameW(&dialog) != FALSE &&
                        feature_capture::ScreenCapture::SaveBitmapToPng(capture_result, file_path.data()))
                    {
                        capture_completed = true;
                        capture_saved = true;
                    }
                    break;
                }

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
            BeginCaptureEntry(CaptureAction::SaveToFile);
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
