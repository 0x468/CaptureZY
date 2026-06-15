#include "feature_capture/countdown_overlay.h"

#include <string>

// clang-format off
#include <windowsx.h>
// clang-format on

#include "core/log.h"

namespace capturezy::feature_capture
{
    CountdownOverlay::CountdownOverlay(HINSTANCE instance) noexcept : instance_(instance) {}

    CountdownOverlay::~CountdownOverlay() noexcept
    {
        Close();
    }

    bool CountdownOverlay::Show(HWND owner_window, std::uint32_t countdown_seconds)
    {
        if (overlay_window_ != nullptr)
        {
            return false;
        }

        if (countdown_seconds == 0)
        {
            // 无延迟，直接通知完成
            PostMessageW(owner_window, CountdownCompleteMessage(), 0, 0);
            return true;
        }

        owner_window_ = owner_window;
        countdown_remaining_ = countdown_seconds;
        initial_countdown_ = countdown_seconds;

        // 创建覆盖层窗口 — 全屏、置顶、分层、无激活
        overlay_window_ = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"CaptureZY.CountdownOverlay",
            L"",
            WS_POPUP | WS_VISIBLE,
            0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
            nullptr, nullptr, instance_, this);

        if (overlay_window_ == nullptr)
        {
            CAPTUREZY_LOG_ERROR(core::LogCategory::Capture, L"Failed to create countdown overlay window.");
            return false;
        }

        SetLayeredWindowAttributes(overlay_window_, 0, 180, LWA_ALPHA); // 半透明背景
        SetWindowLongPtrW(overlay_window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        // 启动倒计时定时器
        SetTimer(overlay_window_, kCountdownTimerId, kCountdownTimerIntervalMs, nullptr);

        CAPTUREZY_LOG_INFO(core::LogCategory::Capture,
                           std::wstring(L"Countdown overlay started with ") +
                               std::to_wstring(countdown_seconds) + L" seconds.");

        return true;
    }

    void CountdownOverlay::Close() noexcept
    {
        if (overlay_window_ == nullptr)
        {
            return;
        }

        KillTimer(overlay_window_, kCountdownTimerId);
        DestroyWindow(overlay_window_);
        overlay_window_ = nullptr;
    }

    bool CountdownOverlay::IsVisible() const noexcept
    {
        return overlay_window_ != nullptr;
    }

    ATOM CountdownOverlay::RegisterWindowClass() const
    {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = &WindowProc;
        window_class.hInstance = instance_;
        window_class.lpszClassName = L"CaptureZY.CountdownOverlay";
        window_class.hbrBackground = nullptr;
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        return RegisterClassExW(&window_class);
    }

    LRESULT CountdownOverlay::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param)
    {
        switch (message)
        {
        case WM_TIMER:
            if (w_param == kCountdownTimerId)
            {
                --countdown_remaining_;
                if (countdown_remaining_ == 0)
                {
                    KillTimer(overlay_window_, kCountdownTimerId);
                    DestroyWindow(overlay_window_);
                    overlay_window_ = nullptr;
                    CAPTUREZY_LOG_INFO(core::LogCategory::Capture, L"Countdown complete, notifying owner window.");
                    PostMessageW(owner_window_, CountdownCompleteMessage(), 0, 0);
                    return 0;
                }

                InvalidateRect(overlay_window_, nullptr, FALSE);
                return 0;
            }
            break;

        case WM_PAINT:
            PaintOverlay();
            return 0;

        case WM_KEYDOWN:
            if (w_param == VK_ESCAPE)
            {
                CAPTUREZY_LOG_INFO(core::LogCategory::Capture, L"Countdown cancelled by user (Esc).");
                KillTimer(overlay_window_, kCountdownTimerId);
                DestroyWindow(overlay_window_);
                overlay_window_ = nullptr;
                // 取消时不发送完成消息 — 主窗口在 BeginCaptureEntry 中会检测
                return 0;
            }
            break;

        case WM_DESTROY:
            KillTimer(overlay_window_, kCountdownTimerId);
            return 0;
        }

        return DefWindowProcW(overlay_window_, message, w_param, l_param);
    }

    void CountdownOverlay::PaintOverlay() noexcept
    {
        PAINTSTRUCT paint{};
        HDC device_context = BeginPaint(overlay_window_, &paint);

        RECT client_rect{};
        GetClientRect(overlay_window_, &client_rect);

        int const center_x = (client_rect.left + client_rect.right) / 2;
        int const center_y = (client_rect.top + client_rect.bottom) / 2;

        // 绘制半透明黑色背景圆区域
        int const radius = 120;
        HBRUSH background_brush = CreateSolidBrush(RGB(0, 0, 0));
        HPEN background_pen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));

        HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(device_context, background_brush));
        HPEN old_pen = static_cast<HPEN>(SelectObject(device_context, background_pen));

        Ellipse(device_context, center_x - radius, center_y - radius, center_x + radius, center_y + radius);

        SelectObject(device_context, old_pen);
        SelectObject(device_context, old_brush);
        DeleteObject(background_pen);
        DeleteObject(background_brush);

        // 绘制倒计时数字 — 白色粗体
        std::wstring number_text = std::to_wstring(countdown_remaining_);
        HFONT number_font = CreateFontW(
            180, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        HFONT old_font = static_cast<HFONT>(SelectObject(device_context, number_font));
        COLORREF old_color = SetTextColor(device_context, RGB(255, 255, 255));
        int old_bk_mode = SetBkMode(device_context, TRANSPARENT);

        RECT text_rect{
            .left = center_x - radius,
            .top = center_y - radius,
            .right = center_x + radius,
            .bottom = center_y + radius,
        };
        DrawTextW(device_context, number_text.c_str(), -1, &text_rect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SetBkMode(device_context, old_bk_mode);
        SetTextColor(device_context, old_color);
        SelectObject(device_context, old_font);
        DeleteObject(number_font);

        EndPaint(overlay_window_, &paint);
    }

    LRESULT CALLBACK CountdownOverlay::WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
    {
        if (message == WM_NCCREATE)
        {
            // 首次注册窗口类
            auto const *create_struct = reinterpret_cast<CREATESTRUCTW *>(l_param);
            auto *overlay = static_cast<CountdownOverlay *>(create_struct->lpCreateParams);
            if (overlay != nullptr)
            {
                overlay->RegisterWindowClass();
            }
            return DefWindowProcW(window, message, w_param, l_param);
        }

        auto *overlay = reinterpret_cast<CountdownOverlay *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (overlay != nullptr)
        {
            return overlay->HandleMessage(message, w_param, l_param);
        }

        return DefWindowProcW(window, message, w_param, l_param);
    }
} // namespace capturezy::feature_capture
