#include "feature_pin/pin_window.h"

#include <utility>

namespace capturezy::feature_pin
{
    namespace
    {
        constexpr wchar_t const *kPinWindowClassName = L"CaptureZY.PinWindow";
        constexpr wchar_t const *kPinWindowTitle = L"CaptureZY 贴图";
        constexpr DWORD kPinWindowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        constexpr DWORD kPinWindowExStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;

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
    } // namespace

    PinWindow::PinWindow(HINSTANCE instance) noexcept : instance_(instance) {}

    PinWindow::~PinWindow() noexcept
    {
        Close();
    }

    bool PinWindow::Create(RECT anchor_rect, feature_capture::CapturedBitmap bitmap)
    {
        if (!bitmap.IsValid() || RegisterWindowClass() == 0)
        {
            return false;
        }

        Close();
        bitmap_ = std::move(bitmap);

        RECT const window_rect = CalculateWindowRect(anchor_rect);
        window_ = CreateWindowExW(kPinWindowExStyle, kPinWindowClassName, kPinWindowTitle, kPinWindowStyle,
                                  window_rect.left, window_rect.top, window_rect.right - window_rect.left,
                                  window_rect.bottom - window_rect.top, nullptr, nullptr, instance_, this);

        if (window_ == nullptr)
        {
            bitmap_ = {};
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

    RECT PinWindow::CalculateWindowRect(RECT anchor_rect) const noexcept
    {
        SIZE const size = bitmap_.Size();
        RECT window_rect{0, 0, size.cx, size.cy};
        AdjustWindowRectEx(&window_rect, kPinWindowStyle, FALSE, kPinWindowExStyle);
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

    LRESULT PinWindow::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param)
    {
        switch (message)
        {
        case WM_ERASEBKGND:
            return 1;

        case WM_CLOSE:
            DestroyWindow(window_);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC device_context = BeginPaint(window_, &paint);

            RECT client_rect{};
            GetClientRect(window_, &client_rect);
            FillRect(device_context, &client_rect, GetSysColorBrush(COLOR_WINDOW));

            HDC memory_device_context = CreateCompatibleDC(device_context);
            if (memory_device_context != nullptr && bitmap_.IsValid())
            {
                HGDIOBJ previous_bitmap = SelectObject(memory_device_context, bitmap_.Get());
                SIZE const size = bitmap_.Size();
                SetStretchBltMode(device_context, HALFTONE);
                StretchBlt(device_context, 0, 0, client_rect.right - client_rect.left,
                           client_rect.bottom - client_rect.top, memory_device_context, 0, 0, size.cx, size.cy,
                           SRCCOPY);
                SelectObject(memory_device_context, previous_bitmap);
                DeleteDC(memory_device_context);
            }

            EndPaint(window_, &paint);
            return 0;
        }

        case WM_DESTROY:
            bitmap_ = {};
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
} // namespace capturezy::feature_pin
