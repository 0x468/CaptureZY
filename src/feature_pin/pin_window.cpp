#include "feature_pin/pin_window.h"

#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <windowsx.h>

#include "core/log.h"
#include "feature_capture/capture_file_dialog.h"
#include "feature_capture/screen_capture.h"

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
        constexpr UINT_PTR kScaleCommitTimerId = 2;
        constexpr UINT kScaleOverlayDurationMs = 900;
        constexpr UINT kScaleCommitDelayMs = 120;
        constexpr int kScaleOverlayWidth = 76;
        constexpr int kScaleOverlayHeight = 36;
        constexpr int kScaleOverlayMargin = 12;
        constexpr COLORREF kScaleOverlayBackgroundColor = RGB(32, 32, 32);
        constexpr COLORREF kScaleOverlayTextColor = RGB(255, 255, 255);
        constexpr UINT_PTR kToggleTopmostCommandId = 2001;
        constexpr UINT_PTR kCopyPinCommandId = 2002;
        constexpr UINT_PTR kSavePinCommandId = 2003;
        constexpr UINT_PTR kHidePinCommandId = 2004;
        constexpr UINT_PTR kClosePinCommandId = 2005;
        constexpr UINT_PTR kResetScaleCommandId = 2006;

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

        void LogScaleMessage(int scale_percent) noexcept
        {
            try
            {
                core::Log::Write(core::LogLevel::Debug, core::LogCategory::Pin,
                                 std::format(L"Pin window scale updated to {}%.", scale_percent));
            }
            catch (...)
            {
                core::Log::Write(core::LogLevel::Debug, core::LogCategory::Pin, L"Pin window scale updated.");
            }
        }

    } // namespace

    PinWindow::PinWindow(HINSTANCE instance, core::AppSettings const &app_settings) noexcept
        : instance_(instance), app_settings_(&app_settings)
    {
    }

    PinWindow::~PinWindow() noexcept
    {
        Close();
    }

    bool PinWindow::Create(feature_capture::CaptureResult capture_result)
    {
        if (!capture_result.IsValid() || RegisterWindowClass() == 0)
        {
            CAPTUREZY_LOG_ERROR(core::LogCategory::Pin, L"Pin window setup failed before window creation.");
            return false;
        }

        Close();
        capture_result_ = std::move(capture_result);
        ResetScaledBitmapCache();
        ResetPaintBuffer();
        scale_percent_ = kDefaultScalePercent;
        topmost_ = true;
        scale_interaction_active_ = false;

        RECT const window_rect = CalculateWindowRect(capture_result_.ScreenRect(), CurrentClientSize());
        window_ = CreateWindowExW(kPinWindowExStyle, kPinWindowClassName, kPinWindowTitle, kPinWindowStyle,
                                  window_rect.left, window_rect.top, window_rect.right - window_rect.left,
                                  window_rect.bottom - window_rect.top, nullptr, nullptr, instance_, this);

        if (window_ == nullptr)
        {
            capture_result_ = {};
            CAPTUREZY_LOG_ERROR(core::LogCategory::Pin, L"CreateWindowExW failed for pin window.");
            return false;
        }

        ShowWindow(window_, SW_SHOWNORMAL);
        UpdateWindow(window_);
        CAPTUREZY_LOG_INFO(core::LogCategory::Pin, L"Pin window created and shown.");
        return true;
    }

    void PinWindow::Close() noexcept
    {
        if (window_ != nullptr)
        {
            DestroyWindow(window_);
            window_ = nullptr;
        }

        ResetScaledBitmapCache();
        ResetPaintBuffer();
    }

    bool PinWindow::IsOpen() const noexcept
    {
        return window_ != nullptr;
    }

    void PinWindow::SetStateChangedCallback(StateChangedCallback callback)
    {
        state_changed_callback_ = std::move(callback);
    }

    void PinWindow::Show() noexcept
    {
        if (window_ == nullptr || IsVisible())
        {
            return;
        }

        ShowWindow(window_, SW_SHOWNOACTIVATE);
        CAPTUREZY_LOG_DEBUG(core::LogCategory::Pin, L"Pin window shown.");
        if (scale_overlay_window_ != nullptr && IsWindowVisible(scale_overlay_window_) != FALSE)
        {
            UpdateScaleOverlayPosition();
        }

        if (state_changed_callback_)
        {
            state_changed_callback_();
        }
    }

    void PinWindow::Hide() noexcept
    {
        if (window_ == nullptr || !IsVisible())
        {
            return;
        }

        HideScaleOverlay();
        ShowWindow(window_, SW_HIDE);
        CAPTUREZY_LOG_DEBUG(core::LogCategory::Pin, L"Pin window hidden.");

        if (state_changed_callback_)
        {
            state_changed_callback_();
        }
    }

    bool PinWindow::IsVisible() const noexcept
    {
        return window_ != nullptr && IsWindowVisible(window_) != FALSE;
    }

    void PinWindow::SetTopmost(bool topmost) noexcept
    {
        if (window_ == nullptr || topmost_ == topmost)
        {
            return;
        }

        topmost_ = topmost;
        SetWindowPos(window_, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

        if (scale_overlay_window_ != nullptr)
        {
            SetWindowPos(scale_overlay_window_, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }

        CAPTUREZY_LOG_INFO(core::LogCategory::Pin,
                           topmost_ ? L"Pin window topmost changed to on." : L"Pin window topmost changed to off.");
    }

    void PinWindow::CopyToClipboard() const noexcept
    {
        if (window_ == nullptr || !capture_result_.IsValid())
        {
            return;
        }

        if (feature_capture::ScreenCapture::CopyBitmapToClipboard(window_, capture_result_))
        {
            CAPTUREZY_LOG_INFO(core::LogCategory::Pin, L"Copied pinned image to clipboard.");
            return;
        }

        CAPTUREZY_LOG_WARNING(core::LogCategory::Pin, L"Failed to copy pinned image to clipboard.");
    }

    void PinWindow::SaveToFile() const
    {
        if (window_ == nullptr || !capture_result_.IsValid())
        {
            return;
        }

        if (feature_capture::SaveCaptureResultWithPngDialog(window_, capture_result_, *app_settings_))
        {
            CAPTUREZY_LOG_INFO(core::LogCategory::Pin, L"Pinned image saved through dialog.");
            return;
        }

        CAPTUREZY_LOG_DEBUG(core::LogCategory::Pin, L"Pinned image save dialog closed without saving.");
    }

    void PinWindow::ShowContextMenu(POINT anchor_screen_point) noexcept
    {
        if (window_ == nullptr)
        {
            return;
        }

        HMENU menu = CreatePopupMenu();
        if (menu == nullptr)
        {
            return;
        }

        UINT reset_scale_flags = MF_STRING;
        if (scale_percent_ == kDefaultScalePercent)
        {
            reset_scale_flags |= MF_GRAYED;
        }

        wchar_t const *scale_label = nullptr;
        std::wstring dynamic_scale_label;
        try
        {
            dynamic_scale_label = L"缩放：";
            dynamic_scale_label += std::to_wstring(scale_percent_);
            dynamic_scale_label += L"%";
            scale_label = dynamic_scale_label.c_str();
        }
        catch (...)
        {
            scale_label = L"缩放";
        }

        AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, scale_label);
        AppendMenuW(menu, reset_scale_flags, kResetScaleCommandId, L"重置缩放为 100%");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kCopyPinCommandId, L"复制图片");
        AppendMenuW(menu, MF_STRING, kSavePinCommandId, L"另存为 PNG");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, topmost_ ? MF_STRING | MF_CHECKED : MF_STRING, kToggleTopmostCommandId, L"始终置顶");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kHidePinCommandId, L"隐藏此贴图");
        AppendMenuW(menu, MF_STRING, kClosePinCommandId, L"关闭此贴图");

        SetForegroundWindow(window_);
        TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, anchor_screen_point.x,
                       anchor_screen_point.y, 0, window_, nullptr);
        PostMessageW(window_, WM_NULL, 0, 0);
        DestroyMenu(menu);
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
        window_class.style = CS_DBLCLKS;
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

    bool PinWindow::UpdateScale(short wheel_delta) noexcept
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

        scale_percent_ = new_scale_percent;
        scale_interaction_active_ = true;
        ResetScaledBitmapCache();
        KillTimer(window_, kScaleCommitTimerId);
        SetTimer(window_, kScaleCommitTimerId, kScaleCommitDelayMs, nullptr);

        SIZE const new_size = CurrentClientSize();
        SetWindowPos(window_, nullptr, window_rect.left, window_rect.top, new_size.cx, new_size.cy,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        InvalidateRect(window_, nullptr, FALSE);
        ShowScaleOverlay();
        LogScaleMessage(scale_percent_);
        return true;
    }

    void PinWindow::BeginDrag(POINT cursor_screen_point) noexcept
    {
        if (window_ == nullptr || dragging_)
        {
            return;
        }

        RECT window_rect{};
        GetWindowRect(window_, &window_rect);
        drag_offset_.x = cursor_screen_point.x - window_rect.left;
        drag_offset_.y = cursor_screen_point.y - window_rect.top;
        SetCapture(window_);
        dragging_ = GetCapture() == window_;
    }

    void PinWindow::UpdateDrag(POINT cursor_screen_point) noexcept
    {
        if (window_ == nullptr || !dragging_)
        {
            return;
        }

        RECT window_rect{};
        GetWindowRect(window_, &window_rect);
        int const width = std::max(1, static_cast<int>(window_rect.right - window_rect.left));
        int const height = std::max(1, static_cast<int>(window_rect.bottom - window_rect.top));
        int const new_left = cursor_screen_point.x - drag_offset_.x;
        int const new_top = cursor_screen_point.y - drag_offset_.y;
        SetWindowPos(window_, nullptr, new_left, new_top, width, height, SWP_NOACTIVATE | SWP_NOZORDER);
    }

    void PinWindow::EndDrag() noexcept
    {
        if (!dragging_)
        {
            return;
        }

        dragging_ = false;
        if (GetCapture() == window_)
        {
            ReleaseCapture();
        }
    }

    void PinWindow::ResetScaledBitmapCache() noexcept
    {
        scaled_bitmap_cache_ = {};
        scaled_bitmap_cache_size_ = {};
    }

    void PinWindow::ResetPaintBuffer() noexcept
    {
        paint_buffer_ = {};
        paint_buffer_size_ = {};
    }

    bool PinWindow::EnsureScaledBitmapCache(HDC device_context, SIZE target_size) noexcept
    {
        if (!capture_result_.IsValid() || target_size.cx <= 0 || target_size.cy <= 0)
        {
            ResetScaledBitmapCache();
            return false;
        }

        if (scaled_bitmap_cache_.IsValid() && scaled_bitmap_cache_size_.cx == target_size.cx &&
            scaled_bitmap_cache_size_.cy == target_size.cy)
        {
            return true;
        }

        HDC source_device_context = CreateCompatibleDC(device_context);
        HDC cache_device_context = CreateCompatibleDC(device_context);
        if (source_device_context == nullptr || cache_device_context == nullptr)
        {
            if (cache_device_context != nullptr)
            {
                DeleteDC(cache_device_context);
            }
            if (source_device_context != nullptr)
            {
                DeleteDC(source_device_context);
            }
            ResetScaledBitmapCache();
            return false;
        }

        HBITMAP scaled_bitmap = CreateCompatibleBitmap(device_context, target_size.cx, target_size.cy);
        if (scaled_bitmap == nullptr)
        {
            DeleteDC(cache_device_context);
            DeleteDC(source_device_context);
            ResetScaledBitmapCache();
            return false;
        }

        HGDIOBJ previous_source_bitmap = SelectObject(source_device_context, capture_result_.Bitmap().Get());
        HGDIOBJ previous_cache_bitmap = SelectObject(cache_device_context, scaled_bitmap);
        RECT cache_rect{.left = 0, .top = 0, .right = target_size.cx, .bottom = target_size.cy};
        FillRect(cache_device_context, &cache_rect, GetSysColorBrush(COLOR_WINDOW));
        SetStretchBltMode(cache_device_context, HALFTONE);
        SIZE const source_size = capture_result_.PixelSize();
        bool const stretched = StretchBlt(cache_device_context, 0, 0, target_size.cx, target_size.cy,
                                          source_device_context, 0, 0, source_size.cx, source_size.cy,
                                          SRCCOPY) != FALSE;

        SelectObject(cache_device_context, previous_cache_bitmap);
        SelectObject(source_device_context, previous_source_bitmap);
        DeleteDC(cache_device_context);
        DeleteDC(source_device_context);

        if (!stretched)
        {
            DeleteObject(scaled_bitmap);
            ResetScaledBitmapCache();
            return false;
        }

        scaled_bitmap_cache_ = feature_capture::CapturedBitmap(scaled_bitmap, target_size);
        scaled_bitmap_cache_size_ = target_size;
        return true;
    }

    bool PinWindow::EnsurePaintBuffer(HDC device_context, SIZE target_size) noexcept
    {
        if (target_size.cx <= 0 || target_size.cy <= 0)
        {
            ResetPaintBuffer();
            return false;
        }

        if (paint_buffer_.IsValid() && paint_buffer_size_.cx == target_size.cx &&
            paint_buffer_size_.cy == target_size.cy)
        {
            return true;
        }

        HBITMAP buffer_bitmap = CreateCompatibleBitmap(device_context, target_size.cx, target_size.cy);
        if (buffer_bitmap == nullptr)
        {
            ResetPaintBuffer();
            return false;
        }

        paint_buffer_ = feature_capture::CapturedBitmap(buffer_bitmap, target_size);
        paint_buffer_size_ = target_size;
        return true;
    }

    bool PinWindow::DrawScaledBitmap(HDC destination_device_context, SIZE target_size) noexcept
    {
        if (!capture_result_.IsValid() || target_size.cx <= 0 || target_size.cy <= 0)
        {
            return false;
        }

        if (!scale_interaction_active_ && EnsureScaledBitmapCache(destination_device_context, target_size))
        {
            HDC image_device_context = CreateCompatibleDC(destination_device_context);
            if (image_device_context == nullptr)
            {
                return false;
            }

            HGDIOBJ previous_image_bitmap = SelectObject(image_device_context, scaled_bitmap_cache_.Get());
            bool const copied = BitBlt(destination_device_context, 0, 0, target_size.cx, target_size.cy,
                                       image_device_context, 0, 0, SRCCOPY) != FALSE;
            SelectObject(image_device_context, previous_image_bitmap);
            DeleteDC(image_device_context);
            return copied;
        }

        HDC source_device_context = CreateCompatibleDC(destination_device_context);
        if (source_device_context == nullptr)
        {
            return false;
        }

        HGDIOBJ previous_source_bitmap = SelectObject(source_device_context, capture_result_.Bitmap().Get());
        SetStretchBltMode(destination_device_context, scale_interaction_active_ ? COLORONCOLOR : HALFTONE);
        SIZE const source_size = capture_result_.PixelSize();
        bool const stretched = StretchBlt(destination_device_context, 0, 0, target_size.cx, target_size.cy,
                                          source_device_context, 0, 0, source_size.cx, source_size.cy,
                                          SRCCOPY) != FALSE;
        SelectObject(source_device_context, previous_source_bitmap);
        DeleteDC(source_device_context);
        return stretched;
    }

    void PinWindow::FinishScaleInteraction() noexcept
    {
        if (window_ == nullptr)
        {
            return;
        }

        KillTimer(window_, kScaleCommitTimerId);
        if (!scale_interaction_active_)
        {
            return;
        }

        scale_interaction_active_ = false;
        ResetScaledBitmapCache();
        RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    }

    void PinWindow::PaintWindow() noexcept
    {
        PAINTSTRUCT paint{};
        HDC device_context = BeginPaint(window_, &paint);

        RECT client_rect{};
        GetClientRect(window_, &client_rect);
        int const client_width = client_rect.right - client_rect.left;
        int const client_height = client_rect.bottom - client_rect.top;
        SIZE const target_size{.cx = client_width, .cy = client_height};
        HDC paint_device_context = device_context;
        HDC buffer_device_context = nullptr;
        HGDIOBJ previous_buffer_bitmap = nullptr;
        if (EnsurePaintBuffer(device_context, target_size))
        {
            buffer_device_context = CreateCompatibleDC(device_context);
            if (buffer_device_context != nullptr)
            {
                previous_buffer_bitmap = SelectObject(buffer_device_context, paint_buffer_.Get());
                paint_device_context = buffer_device_context;
            }
        }

        FillRect(paint_device_context, &client_rect, GetSysColorBrush(COLOR_WINDOW));
        (void)DrawScaledBitmap(paint_device_context, target_size);
        FrameRect(paint_device_context, &client_rect, GetSysColorBrush(COLOR_WINDOWFRAME));

        if (buffer_device_context != nullptr)
        {
            BitBlt(device_context, 0, 0, client_width, client_height, buffer_device_context, 0, 0, SRCCOPY);
            SelectObject(buffer_device_context, previous_buffer_bitmap);
            DeleteDC(buffer_device_context);
        }

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

        SetWindowPos(scale_overlay_window_, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST, overlay_left, overlay_top,
                     kScaleOverlayWidth, kScaleOverlayHeight, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
    }

    LRESULT PinWindow::HandleMessage(UINT message, WPARAM w_param, LPARAM l_param)
    {
        switch (message)
        {
        case WM_ERASEBKGND:
            return 1;

        case WM_COMMAND:
            switch (LOWORD(w_param))
            {
            case kToggleTopmostCommandId:
                SetTopmost(!topmost_);
                return 0;

            case kCopyPinCommandId:
                CopyToClipboard();
                return 0;

            case kSavePinCommandId:
                SaveToFile();
                return 0;

            case kResetScaleCommandId:
                if (scale_percent_ != kDefaultScalePercent)
                {
                    RECT window_rect{};
                    GetWindowRect(window_, &window_rect);
                    scale_percent_ = kDefaultScalePercent;
                    scale_interaction_active_ = false;
                    KillTimer(window_, kScaleCommitTimerId);
                    ResetScaledBitmapCache();
                    SIZE const size = CurrentClientSize();
                    SetWindowPos(window_, nullptr, window_rect.left, window_rect.top, size.cx, size.cy,
                                 SWP_NOACTIVATE | SWP_NOZORDER);
                    RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
                    ShowScaleOverlay();
                }
                return 0;

            case kHidePinCommandId:
                Hide();
                return 0;

            case kClosePinCommandId:
                DestroyWindow(window_);
                return 0;

            default:
                break;
            }
            break;

        case WM_MOUSEACTIVATE:
            return MA_ACTIVATE;

        case WM_ACTIVATE:
            if (LOWORD(w_param) != WA_INACTIVE && !dragging_ && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0)
            {
                POINT cursor_screen_point{};
                GetCursorPos(&cursor_screen_point);
                BeginDrag(cursor_screen_point);
            }
            break;

        case WM_NCLBUTTONDBLCLK:
        case WM_LBUTTONDBLCLK:
        case WM_CLOSE:
            DestroyWindow(window_);
            return 0;

        case WM_LBUTTONDOWN: {
            POINT anchor_screen_point{.x = GET_X_LPARAM(l_param), .y = GET_Y_LPARAM(l_param)};
            ClientToScreen(window_, &anchor_screen_point);
            BeginDrag(anchor_screen_point);
            return 0;
        }

        case WM_MOUSEMOVE:
            if ((w_param & MK_LBUTTON) != 0U)
            {
                POINT anchor_screen_point{.x = GET_X_LPARAM(l_param), .y = GET_Y_LPARAM(l_param)};
                ClientToScreen(window_, &anchor_screen_point);
                UpdateDrag(anchor_screen_point);
            }
            return 0;

        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
            EndDrag();
            return 0;

        case WM_NCRBUTTONUP: {
            POINT const anchor_screen_point{.x = GET_X_LPARAM(l_param), .y = GET_Y_LPARAM(l_param)};
            ShowContextMenu(anchor_screen_point);
            return 0;
        }

        case WM_RBUTTONUP: {
            POINT anchor_screen_point{.x = GET_X_LPARAM(l_param), .y = GET_Y_LPARAM(l_param)};
            ClientToScreen(window_, &anchor_screen_point);
            ShowContextMenu(anchor_screen_point);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            UpdateScale(GET_WHEEL_DELTA_WPARAM(w_param));
            return 0;
        }

        case WM_TIMER:
            if (w_param == kScaleOverlayTimerId)
            {
                HideScaleOverlay();
                return 0;
            }
            if (w_param == kScaleCommitTimerId)
            {
                FinishScaleInteraction();
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
            EndDrag();
            HideScaleOverlay();
            KillTimer(window_, kScaleCommitTimerId);
            if (scale_overlay_window_ != nullptr)
            {
                DestroyWindow(scale_overlay_window_);
                scale_overlay_window_ = nullptr;
            }
            capture_result_ = {};
            ResetScaledBitmapCache();
            ResetPaintBuffer();
            window_ = nullptr;
            CAPTUREZY_LOG_INFO(core::LogCategory::Pin, L"Pin window destroyed.");
            if (state_changed_callback_)
            {
                state_changed_callback_();
            }
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
