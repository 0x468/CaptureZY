#include "platform_win/main_window.h"

#include <string>

#include "core/app_metadata.h"

namespace capturezy::platform_win
{
    namespace
    {
        constexpr UINT kTrayIconId = 1;
        constexpr UINT kTrayMessage = WM_APP + 1;
        constexpr UINT_PTR kShowWindowCommandId = 1001;
        constexpr UINT_PTR kBeginCaptureCommandId = 1002;
        constexpr UINT_PTR kExitApplicationCommandId = 1003;
        constexpr int kCaptureHotkeyId = 1;
        constexpr UINT kCaptureHotkeyModifiers = MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT;
        constexpr UINT kCaptureHotkeyVirtualKey = 0x41;

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
        : instance_(instance), app_state_(&app_state), capture_overlay_(instance)
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

    void MainWindow::BeginCaptureEntry()
    {
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

        AppendMenuW(menu, MF_STRING, kShowWindowCommandId, L"显示窗口");
        AppendMenuW(menu, MF_STRING, kBeginCaptureCommandId, L"开始截图");
        AppendMenuW(menu, MF_STRING, kExitApplicationCommandId, L"退出");

        POINT cursor_position{};
        GetCursorPos(&cursor_position);
        SetForegroundWindow(window_);
        TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, cursor_position.x, cursor_position.y, 0,
                       window_, nullptr);
        PostMessageW(window_, WM_NULL, 0, 0);

        DestroyMenu(menu);
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
            UnregisterHotkeys();
            RemoveTrayIcon();
            PostQuitMessage(0);
            return 0;

        case WM_COMMAND:
            switch (LOWORD(w_param))
            {
            case kShowWindowCommandId:
                ShowWindowAndActivate();
                return 0;

            case kBeginCaptureCommandId:
                BeginCaptureEntry();
                return 0;

            case kExitApplicationCommandId:
                allow_close_ = true;
                DestroyWindow(window_);
                return 0;

            default:
                break;
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC device_context = BeginPaint(window_, &paint);
            RECT client_rect{};
            GetClientRect(window_, &client_rect);
            DrawTextW(device_context, app_state_->StatusText(), -1, &client_rect,
                      DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            EndPaint(window_, &paint);
            return 0;
        }

        case feature_capture::CaptureOverlay::ResultMessage():
            app_state_->ReturnToIdle();
            ShowWindowAndActivate();
            UpdateWindowPresentation();
            return 0;

        case WM_HOTKEY:
            if (static_cast<int>(w_param) == kCaptureHotkeyId)
            {
                BeginCaptureEntry();
                return 0;
            }
            break;

        case kTrayMessage:
            switch (static_cast<UINT>(l_param))
            {
            case WM_CONTEXTMENU:
            case WM_RBUTTONUP:
                ShowTrayMenu();
                return 0;

            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
                ShowWindowAndActivate();
                return 0;

            default:
                break;
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
