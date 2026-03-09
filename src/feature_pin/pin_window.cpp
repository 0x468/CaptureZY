#include "feature_pin/pin_window.h"

#include <algorithm>
#include <string>
#include <utility>
#include <windowsx.h>

namespace capturezy::feature_pin
{
    namespace
    {
        constexpr wchar_t const *kPinWindowClassName = L"CaptureZY.PinWindow";
        constexpr wchar_t const *kScaleOverlayClassName = L"CaptureZY.PinWindow.ScaleOverlay";
        constexpr wchar_t const *kPinWindowTitle = L"CaptureZY 贴图";
        constexpr DWORD kPinWindowStyle = WS_POPUP;
        constexpr DWORD kPinWindowExStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
        constexpr std::int32_t kDefaultScalePercent = 100;
        constexpr std::int32_t kMinScalePercent = 20;
        constexpr std::int32_t kMaxScalePercent = 400;
        constexpr std::int32_t kScaleStepPercent = 5;
        constexpr UINT_PTR kScaleOverlayTimerId = 1;
        constexpr UINT kScaleOverlayDurationMs = 900;
        constexpr int kScaleOverlayWidth = 76;
        constexpr int kScaleOverlayHeight = 36;
        constexpr int kScaleOverlayMargin = 12;
        constexpr COLORREF kScaleOverlayBackgroundColor = RGB(32, 32, 32);
        constexpr COLORREF kScaleOverlayTextColor = RGB(255, 255, 255);

        void SetWindowUserData(HWND window, PinWindow *pin_window)
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pin_window));
        }

        PinWindow *GetWindowUserData(HWND window)
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
            return reinterpret_cast<PinWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }

        [[nodiscard]] std::int32_t ClampScalePercent(std::int32_t scale_percent) noexcept
        {
            return std::clamp(scale_percent, kMinScalePercent, kMaxScalePercent);
        }

        [[nodiscard]] RECT CurrentMonitorWorkArea(HWND window) noexcept
        {
            MONITORINFO monitor_info{};
            monitor_info.cbSize = sizeof(monitor_info);
            GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor_info);
            return monitor_info.rcWork;
        }
    } // namespace

    PinWindow::PinWindow(HINSTANCE instance) noexcept : instance_(instance) {}

    PinWindow::~PinWindow() noexcept
    {
        Close();
    }

    bool PinWindow::Create(feature_capture::CaptureResult capture_result)
    {
        if (!capture_result.IsValid() || RegisterWindowClass() == 0)
        {
            return false;
        }

        Close();
        capture_result_ = std::move(capture_result);
        scale_percent_ = kDefaultScalePercent;

        RECT const window_rect = CalculateWindowRect(capture_result_.ScreenRect(), CurrentClientSize());
        window_ = CreateWindowExW(kPinWindowExStyle, kPinWindowClassName, kPinWindowTitle, kPinWindowStyle,
                                  window_rect.left, window_rect.top, window_rect.right - window_rect.left,
                                  window_rect.bottom - window_rect.top, nullptr, nullptr, instance_, this);

        if (window_ == nullptr)
        {
            capture_result_ = {};
            return false;
        }

        ShowWindow(window_, SW_SHOWNORMAL);
        UpdateWindow(window_);
        return true;
    }

    void PinWindow::Close() noexcept
    {
        if (window_ != nullptr)
        {
            DestroyWindow(window_);
            window_ = nullptr;
        }
    }

    bool PinWindow::IsOpen() const noexcept
    {
        return window_ != nullptr;
    }

    SIZE PinWindow::CurrentClientSize() const noexcept
    {
        SIZE const bitmap_size = capture_result_.PixelSize();
        SIZE client_size{};
        client_size.cx = std::max(1, MulDiv(bitmap_size.cx, scale_percent_, kDefaultScalePercent));
        client_size.cy = std::max(1, MulDiv(bitmap_size.cy, scale_percent_, kDefaultScalePercent));
        return client_size;
    }

    RECT PinWindow::CalculateWindowRect(RECT anchor_rect, SIZE bitmap_size) noexcept
    {
        RECT window_rect{.left = 0, .top = 0, .right = bitmap_size.cx, .bottom = bitmap_size.cy};
        OffsetRect(&window_rect, anchor_rect.left, anchor_rect.top);
        return window_rect;
    }

    ATOM PinWindow::RegisterWindowClass() const
    {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = PinWindow::WindowProc;
        window_class.hInstance = instance_;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        window_class.lpszClassName = kPinWindowClassName;

        ATOM const result = RegisterClassExW(&window_class);
        if (result != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
        {
            return 1;
        }

        return 0;
    }

    ATOM PinWindow::RegisterScaleOverlayClass() const
    {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = PinWindow::ScaleOverlayProc;
        window_class.hInstance = instance_;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground = nullptr;
        window_class.lpszClassName = kScaleOverlayClassName;

        ATOM const result = RegisterClassExW(&window_class);
        if (result != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
        {
            return 1;
        }

        return 0;
    }

    bool PinWindow::UpdateScale(short wheel_delta, POINT anchor_screen_point) noexcept
    {
        int const wheel_steps = wheel_delta / WHEEL_DELTA;
        if (wheel_steps == 0)
        {
            return false;
        }

        std::int32_t const scale_delta = wheel_steps * kScaleStepPercent;
        std::int32_t const new_scale_percent = ClampScalePercent(scale_percent_ + scale_delta);
        if (new_scale_percent == scale_percent_)
        {
            return false;
        }

        RECT window_rect{};
        GetWindowRect(window_, &window_rect);

        int const old_width = std::max(1, static_cast<int>(window_rect.right - window_rect.left));
        int const old_height = std::max(1, static_cast<int>(window_rect.bottom - window_rect.top));
        int const relative_x = anchor_screen_point.x - window_rect.left;
        int const relative_y = anchor_screen_point.y - window_rect.top;

        scale_percent_ = new_scale_percent;

        SIZE const new_size = CurrentClientSize();
        int const new_left = anchor_screen_point.x - MulDiv(relative_x, new_size.cx, old_width);
        int const new_top = anchor_screen_point.y - MulDiv(relative_y, new_size.cy, old_height);

        SetWindowPos(window_, nullptr, new_left, new_top, new_size.cx, new_size.cy, SWP_NOACTIVATE | SWP_NOZORDER);
        RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
        ShowScaleOverlay();
        return true;
    }

    void PinWindow::PaintWindow() const noexcept
    {
        PAINTSTRUCT paint{};
        HDC device_context = BeginPaint(window_, &paint);

        RECT client_rect{};
        GetClientRect(window_, &client_rect);
        int const client_width = client_rect.right - client_rect.left;
        int const client_height = client_rect.bottom - client_rect.top;

        HDC canvas_device_context = CreateCompatibleDC(device_context);
        HBITMAP canvas_bitmap = nullptr;
        if (canvas_device_context != nullptr)
        {
            canvas_bitmap = CreateCompatibleBitmap(device_context, client_width, client_height);
        }

        if (canvas_device_context == nullptr || canvas_bitmap == nullptr)
        {
            if (canvas_bitmap != nullptr)
            {
                DeleteObject(canvas_bitmap);
            }
            if (canvas_device_context != nullptr)
            {
                DeleteDC(canvas_device_context);
            }
            EndPaint(window_, &paint);
            return;
        }

        HGDIOBJ previous_canvas_bitmap = SelectObject(canvas_device_context, canvas_bitmap);
        FillRect(canvas_device_context, &client_rect, GetSysColorBrush(COLOR_WINDOW));

        HDC image_device_context = CreateCompatibleDC(device_context);
        if (image_device_context != nullptr && capture_result_.IsValid())
        {
            HGDIOBJ previous_image_bitmap = SelectObject(image_device_context, capture_result_.Bitmap().Get());
            SIZE const size = capture_result_.PixelSize();
            SetStretchBltMode(canvas_device_context, HALFTONE);
            StretchBlt(canvas_device_context, 0, 0, client_width, client_height, image_device_context, 0, 0, size.cx,
                       size.cy, SRCCOPY);
            SelectObject(image_device_context, previous_image_bitmap);
        }
        if (image_device_context != nullptr)
        {
            DeleteDC(image_device_context);
        }

        FrameRect(canvas_device_context, &client_rect, GetSysColorBrush(COLOR_WINDOWFRAME));
        BitBlt(device_context, 0, 0, client_width, client_height, canvas_device_context, 0, 0, SRCCOPY);
        SelectObject(canvas_device_context, previous_canvas_bitmap);
        DeleteObject(canvas_bitmap);
        DeleteDC(canvas_device_context);
        EndPaint(window_, &paint);
    }

    void PinWindow::PaintScaleOverlay(HWND overlay_window) const
    {
        PAINTSTRUCT paint{};
        HDC device_context = BeginPaint(overlay_window, &paint);

        RECT client_rect{};
        GetClientRect(overlay_window, &client_rect);

        HBRUSH background_brush = CreateSolidBrush(kScaleOverlayBackgroundColor);
        FillRect(device_context, &client_rect, background_brush);
        DeleteObject(background_brush);

        SetBkMode(device_context, TRANSPARENT);
        SetTextColor(device_context, kScaleOverlayTextColor);

        std::wstring scale_text = std::to_wstring(scale_percent_);
        scale_text += L"%";
        DrawTextW(device_context, scale_text.data(), static_cast<int>(scale_text.size()), &client_rect,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        FrameRect(device_context, &client_rect, GetSysColorBrush(COLOR_WINDOWFRAME));
        EndPaint(overlay_window, &paint);
    }

    void PinWindow::ShowScaleOverlay() noexcept
    {
        if (RegisterScaleOverlayClass() == 0)
        {
            return;
        }

        if (scale_overlay_window_ == nullptr)
        {
            scale_overlay_window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
                                                    kScaleOverlayClassName, L"", WS_POPUP, 0, 0, kScaleOverlayWidth,
                                                    kScaleOverlayHeight, nullptr, nullptr, instance_, this);
            if (scale_overlay_window_ == nullptr)
            {
                return;
            }

            SetLayeredWindowAttributes(scale_overlay_window_, 0, 230, LWA_ALPHA);
        }

        UpdateScaleOverlayPosition();
        InvalidateRect(scale_overlay_window_, nullptr, TRUE);
        ShowWindow(scale_overlay_window_, SW_SHOWNOACTIVATE);
        SetTimer(window_, kScaleOverlayTimerId, kScaleOverlayDurationMs, nullptr);
    }

    void PinWindow::HideScaleOverlay() noexcept
    {
        if (scale_overlay_window_ != nullptr)
        {
            ShowWindow(scale_overlay_window_, SW_HIDE);
        }

        if (window_ != nullptr)
        {
            KillTimer(window_, kScaleOverlayTimerId);
        }
    }

    void PinWindow::UpdateScaleOverlayPosition() const noexcept
    {
        if (scale_overlay_window_ == nullptr)
        {
            return;
        }

        RECT pin_window_rect{};
        GetWindowRect(window_, &pin_window_rect);
        RECT const work_area = CurrentMonitorWorkArea(window_);

        int const min_left = work_area.left + kScaleOverlayMargin;
        int const max_left = work_area.right - kScaleOverlayWidth - kScaleOverlayMargin;
        int const min_top = work_area.top + kScaleOverlayMargin;
        int const max_top = work_area.bottom - kScaleOverlayHeight - kScaleOverlayMargin;

        int const desired_left = pin_window_rect.left + kScaleOverlayMargin;
        int const desired_top = pin_window_rect.top + kScaleOverlayMargin;
        int const overlay_left = std::clamp(desired_left, min_left, std::max(min_left, max_left));
        int const overlay_top = std::clamp(desired_top, min_top, std::max(min_top, max_top));

        SetWindowPos(scale_overlay_window_, HWND_TOPMOST, overlay_left, overlay_top, kScaleOverlayWidth,
                     kScaleOverlayHeight, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
    }

    LRESULT PinWindow::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param)
    {
        switch (message)
        {
        case WM_ERASEBKGND:
            return 1;

        case WM_NCHITTEST:
            return HTCAPTION;

        case WM_NCRBUTTONUP:
        case WM_NCLBUTTONDBLCLK:
        case WM_RBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_CLOSE:
            DestroyWindow(window_);
            return 0;

        case WM_MOUSEWHEEL: {
            POINT const anchor_screen_point{.x = GET_X_LPARAM(l_param), .y = GET_Y_LPARAM(l_param)};
            UpdateScale(GET_WHEEL_DELTA_WPARAM(w_param), anchor_screen_point);
            return 0;
        }

        case WM_TIMER:
            if (w_param == kScaleOverlayTimerId)
            {
                HideScaleOverlay();
                return 0;
            }
            break;

        case WM_PAINT:
            PaintWindow();
            return 0;

        case WM_WINDOWPOSCHANGED:
            if (scale_overlay_window_ != nullptr && IsWindowVisible(scale_overlay_window_) != FALSE)
            {
                UpdateScaleOverlayPosition();
            }
            break;

        case WM_DESTROY:
            HideScaleOverlay();
            if (scale_overlay_window_ != nullptr)
            {
                DestroyWindow(scale_overlay_window_);
                scale_overlay_window_ = nullptr;
            }
            capture_result_ = {};
            window_ = nullptr;
            return 0;

        default:
            break;
        }

        return DefWindowProcW(window_, message, w_param, l_param);
    }

    LRESULT CALLBACK PinWindow::WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
    {
        if (message == WM_NCCREATE)
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
            auto *create_struct = reinterpret_cast<CREATESTRUCTW *>(l_param);
            auto *pin_window = static_cast<PinWindow *>(create_struct->lpCreateParams);
            pin_window->window_ = window;
            SetWindowUserData(window, pin_window);
        }

        if (auto *pin_window = GetWindowUserData(window); pin_window != nullptr)
        {
            return pin_window->HandleMessage(message, w_param, l_param);
        }

        return DefWindowProcW(window, message, w_param, l_param);
    }

    LRESULT CALLBACK PinWindow::ScaleOverlayProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
    {
        (void)w_param;
        (void)l_param;

        if (message == WM_NCCREATE)
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
            auto *create_struct = reinterpret_cast<CREATESTRUCTW *>(l_param);
            auto *pin_window = static_cast<PinWindow *>(create_struct->lpCreateParams);
            SetWindowUserData(window, pin_window);
        }

        if (auto *pin_window = GetWindowUserData(window); pin_window != nullptr)
        {
            switch (message)
            {
            case WM_ERASEBKGND:
                return 1;

            case WM_PAINT:
                pin_window->PaintScaleOverlay(window);
                return 0;

            case WM_DESTROY:
                if (pin_window->scale_overlay_window_ == window)
                {
                    pin_window->scale_overlay_window_ = nullptr;
                }
                return 0;

            default:
                break;
            }
        }

        return DefWindowProcW(window, message, w_param, l_param);
    }
} // namespace capturezy::feature_pin
