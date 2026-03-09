#include "feature_capture/capture_overlay.h"

namespace capturezy::feature_capture
{
    namespace
    {
        constexpr wchar_t const *kOverlayWindowClassName = L"CaptureZY.CaptureOverlay";
        constexpr COLORREF kOverlayColor = RGB(0, 0, 0);
        constexpr BYTE kOverlayAlpha = 96;
        constexpr wchar_t const *kOverlayInstruction = L"截图覆盖层原型已启动，左键占位完成，Esc 取消";

        void SetWindowUserData(HWND window, CaptureOverlay *overlay)
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(overlay));
        }

        CaptureOverlay *GetWindowUserData(HWND window)
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
            return reinterpret_cast<CaptureOverlay *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }
    } // namespace

    CaptureOverlay::CaptureOverlay(HINSTANCE instance) noexcept : instance_(instance) {}

    bool CaptureOverlay::Show(HWND owner_window)
    {
        owner_window_ = owner_window;
        if (overlay_window_ != nullptr)
        {
            ShowWindow(overlay_window_, SW_SHOW);
            SetForegroundWindow(overlay_window_);
            return true;
        }

        if (RegisterWindowClass() == 0)
        {
            return false;
        }

        int const left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int const top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int const width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int const height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        overlay_window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, kOverlayWindowClassName,
                                          L"CaptureZY Overlay", WS_POPUP, left, top, width, height, owner_window_,
                                          nullptr, instance_, this);

        if (overlay_window_ == nullptr)
        {
            return false;
        }

        SetLayeredWindowAttributes(overlay_window_, kOverlayColor, kOverlayAlpha, LWA_ALPHA);
        ShowWindow(overlay_window_, SW_SHOW);
        SetForegroundWindow(overlay_window_);
        SetFocus(overlay_window_);
        return true;
    }

    void CaptureOverlay::Close() noexcept
    {
        if (overlay_window_ == nullptr)
        {
            return;
        }

        DestroyWindow(overlay_window_);
        overlay_window_ = nullptr;
    }

    bool CaptureOverlay::IsVisible() const noexcept
    {
        return overlay_window_ != nullptr && IsWindowVisible(overlay_window_) != FALSE;
    }

    ATOM CaptureOverlay::RegisterWindowClass() const
    {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = CaptureOverlay::WindowProc;
        window_class.hInstance = instance_;
        window_class.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOWTEXT);
        window_class.lpszClassName = kOverlayWindowClassName;

        ATOM const result = RegisterClassExW(&window_class);
        if (result != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
        {
            return 1;
        }

        return 0;
    }

    LRESULT CaptureOverlay::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param)
    {
        switch (message)
        {
        case WM_ERASEBKGND:
            return 1;

        case WM_KEYDOWN:
            if (w_param == VK_ESCAPE)
            {
                Finish(OverlayResult::Cancelled);
                return 0;
            }
            break;

        case WM_LBUTTONDOWN:
            Finish(OverlayResult::PlaceholderCaptured);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC device_context = BeginPaint(overlay_window_, &paint);

            RECT client_rect{};
            GetClientRect(overlay_window_, &client_rect);
            FillRect(device_context, &client_rect, GetSysColorBrush(COLOR_WINDOWTEXT));
            SetBkMode(device_context, TRANSPARENT);
            SetTextColor(device_context, RGB(255, 255, 255));
            DrawTextW(device_context, kOverlayInstruction, -1, &client_rect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

            EndPaint(overlay_window_, &paint);
            return 0;
        }

        case WM_DESTROY:
            overlay_window_ = nullptr;
            return 0;

        default:
            break;
        }

        return DefWindowProcW(overlay_window_, message, w_param, l_param);
    }

    void CaptureOverlay::Finish(OverlayResult result) noexcept
    {
        if (owner_window_ != nullptr)
        {
            PostMessageW(owner_window_, ResultMessage(), static_cast<WPARAM>(result), 0);
        }

        Close();
    }

    LRESULT CALLBACK CaptureOverlay::WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
    {
        if (message == WM_NCCREATE)
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
            auto *create_struct = reinterpret_cast<CREATESTRUCTW *>(l_param);
            auto *overlay = static_cast<CaptureOverlay *>(create_struct->lpCreateParams);
            overlay->overlay_window_ = window;
            SetWindowUserData(window, overlay);
        }

        if (auto *overlay = GetWindowUserData(window); overlay != nullptr)
        {
            return overlay->HandleMessage(message, w_param, l_param);
        }

        return DefWindowProcW(window, message, w_param, l_param);
    }
} // namespace capturezy::feature_capture
