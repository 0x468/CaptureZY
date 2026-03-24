#include "feature_capture/capture_overlay.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

// clang-format off
#include <dwmapi.h>
// clang-format on

#include "core/log.h"
#include "feature_capture/capture_result.h"

namespace capturezy::feature_capture
{
    namespace
    {
        constexpr wchar_t const *kOverlayWindowClassName = L"CaptureZY.CaptureOverlay";
        constexpr BYTE kOverlayAlpha = 96;
        constexpr wchar_t const *kOverlayInstruction = L"窗口预选：悬停高亮，单击选择窗口；也可按住左键拖拽框选，Ctrl+"
                                                       L"A 全屏，右键或 Esc 取消";
        constexpr wchar_t const *kSelectionAdjustmentInstruction =
            L"已选择区域：拖动边框可调大小，拖动区域内部可移动；P 贴图，Ctrl+C 复制，Ctrl+S 保存，右键重置，Esc 取消";
        constexpr int kDragThreshold = 4;
        constexpr int kSelectionResizePadding = 6;
        constexpr int kMinSelectionExtent = 8;
        constexpr int kResizeHandleRadius = 4;
        constexpr int kResizeHandleHitRadius = 7;
        constexpr int kResizeHandleDisplayExtent = 70;
        constexpr int kPreviewInvalidationPadding = 10;
        constexpr int kSelectionMetricsPaddingX = 8;
        constexpr int kSelectionMetricsPaddingY = 4;
        constexpr int kSelectionMetricsMargin = 8;
        constexpr int kSelectionMetricsHeight = 24;
        constexpr int kSelectionMetricsMinWidth = 72;
        constexpr int kSelectionMetricsCharWidth = 8;
        constexpr int kToolbarButtonWidth = 68;
        constexpr int kToolbarButtonHeight = 30;
        constexpr int kToolbarSpacing = 8;
        constexpr int kToolbarPadding = 8;
        constexpr int kToolbarMargin = 10;
        constexpr int kToolbarCornerRadius = 10;

        void AlphaFillRect(HDC destination_device_context, RECT rect, BYTE alpha) noexcept;

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

        [[nodiscard]] bool IsRectNonEmpty(RECT rect) noexcept
        {
            return rect.right > rect.left && rect.bottom > rect.top;
        }

        [[nodiscard]] RECT ExpandedRect(RECT rect, int padding) noexcept
        {
            if (!IsRectNonEmpty(rect))
            {
                return {};
            }

            rect.left -= padding;
            rect.top -= padding;
            rect.right += padding;
            rect.bottom += padding;
            return rect;
        }

        [[nodiscard]] bool TryUnionRect(RECT first_rect, RECT second_rect, RECT &result_rect) noexcept
        {
            if (IsRectNonEmpty(first_rect) && IsRectNonEmpty(second_rect))
            {
                return UnionRect(&result_rect, &first_rect, &second_rect) != FALSE;
            }

            if (IsRectNonEmpty(first_rect))
            {
                result_rect = first_rect;
                return true;
            }

            if (IsRectNonEmpty(second_rect))
            {
                result_rect = second_rect;
                return true;
            }

            result_rect = {};
            return false;
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

        [[nodiscard]] bool TryGetDesktopWorkAreaForPoint(POINT screen_point, RECT &desktop_rect) noexcept
        {
            HMONITOR const monitor = MonitorFromPoint(screen_point, MONITOR_DEFAULTTONEAREST);
            if (monitor == nullptr)
            {
                return false;
            }

            MONITORINFO monitor_info{};
            monitor_info.cbSize = sizeof(monitor_info);
            if (GetMonitorInfoW(monitor, &monitor_info) == FALSE || !IsRectNonEmpty(monitor_info.rcWork))
            {
                return false;
            }

            desktop_rect = monitor_info.rcWork;
            return true;
        }

        [[nodiscard]] bool WindowClassNameEquals(HWND window, wchar_t const *expected_class_name) noexcept
        {
            std::array<wchar_t, 64> class_name{};
            int const class_name_length = GetClassNameW(window, class_name.data(), static_cast<int>(class_name.size()));
            return class_name_length > 0 && lstrcmpW(class_name.data(), expected_class_name) == 0;
        }

        [[nodiscard]] bool IsTaskbarShellWindow(HWND window) noexcept
        {
            return WindowClassNameEquals(window, L"Shell_TrayWnd") ||
                   WindowClassNameEquals(window, L"Shell_SecondaryTrayWnd") ||
                   WindowClassNameEquals(window, L"NotifyIconOverflowWindow") ||
                   WindowClassNameEquals(window, L"TopLevelWindowForOverflowXamlIsland");
        }

        [[nodiscard]] bool IsDesktopShellWindow(HWND window) noexcept
        {
            return WindowClassNameEquals(window, L"Progman") || WindowClassNameEquals(window, L"WorkerW");
        }

        [[nodiscard]] bool IsWindowCloaked(HWND window) noexcept
        {
            DWORD cloaked = 0;
            return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
        }

        [[nodiscard]] UINT VisibleFrameBorderThickness(HWND window) noexcept
        {
            UINT border_thickness = 0;
            if (FAILED(DwmGetWindowAttribute(window, DWMWA_VISIBLE_FRAME_BORDER_THICKNESS, &border_thickness,
                                             sizeof(border_thickness))))
            {
                return 0;
            }

            return border_thickness;
        }

        void TrimVisibleFrameBorder(HWND window, RECT &window_rect) noexcept
        {
            if (IsTaskbarShellWindow(window) || IsDesktopShellWindow(window))
            {
                return;
            }

            UINT const border_thickness = VisibleFrameBorderThickness(window);
            if (border_thickness == 0)
            {
                return;
            }

            LONG const inset = static_cast<LONG>(border_thickness);
            if ((window_rect.right - window_rect.left) <= inset * 2 ||
                (window_rect.bottom - window_rect.top) <= inset * 2)
            {
                return;
            }

            window_rect.left += inset;
            window_rect.top += inset;
            window_rect.right -= inset;
            window_rect.bottom -= inset;
        }

        [[nodiscard]] bool TryGetWindowRectForSelection(HWND window, RECT &window_rect) noexcept
        {
            HRESULT const dwm_result = DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &window_rect,
                                                             sizeof(window_rect));
            if (SUCCEEDED(dwm_result) && IsRectNonEmpty(window_rect))
            {
                TrimVisibleFrameBorder(window, window_rect);
                return true;
            }

            if (GetWindowRect(window, &window_rect) == FALSE || !IsRectNonEmpty(window_rect))
            {
                return false;
            }

            TrimVisibleFrameBorder(window, window_rect);
            return IsRectNonEmpty(window_rect);
        }

        [[nodiscard]] bool TryGetClippedWindowRectForSelection(HWND window, RECT clip_rect, RECT &window_rect) noexcept
        {
            RECT candidate_rect{};
            if (!TryGetWindowRectForSelection(window, candidate_rect))
            {
                return false;
            }

            return IntersectRect(&window_rect, &candidate_rect, &clip_rect) != FALSE;
        }

        [[nodiscard]] CapturedBitmap CreateDimmedBitmap(CapturedBitmap const &source_bitmap, BYTE alpha) noexcept
        {
            CapturedBitmap dimmed_bitmap = source_bitmap.Clone();
            if (!dimmed_bitmap.IsValid())
            {
                return {};
            }

            HDC screen_device_context = GetDC(nullptr);
            if (screen_device_context == nullptr)
            {
                return {};
            }

            HDC bitmap_device_context = CreateCompatibleDC(screen_device_context);
            if (bitmap_device_context == nullptr)
            {
                ReleaseDC(nullptr, screen_device_context);
                return {};
            }

            HGDIOBJ previous_bitmap = SelectObject(bitmap_device_context, dimmed_bitmap.Get());
            RECT bitmap_rect{.left = 0, .top = 0, .right = dimmed_bitmap.Size().cx, .bottom = dimmed_bitmap.Size().cy};
            AlphaFillRect(bitmap_device_context, bitmap_rect, alpha);
            SelectObject(bitmap_device_context, previous_bitmap);
            DeleteDC(bitmap_device_context);
            ReleaseDC(nullptr, screen_device_context);
            return dimmed_bitmap;
        }

        void PaintBitmapRect(HDC destination_device_context, RECT destination_rect, HBITMAP bitmap,
                             POINT source_origin) noexcept
        {
            if (bitmap == nullptr || !IsRectNonEmpty(destination_rect))
            {
                return;
            }

            HDC bitmap_device_context = CreateCompatibleDC(destination_device_context);
            if (bitmap_device_context == nullptr)
            {
                return;
            }

            HGDIOBJ previous_bitmap = SelectObject(bitmap_device_context, bitmap);
            (void)BitBlt(destination_device_context, destination_rect.left, destination_rect.top,
                         destination_rect.right - destination_rect.left, destination_rect.bottom - destination_rect.top,
                         bitmap_device_context, source_origin.x, source_origin.y, SRCCOPY);
            SelectObject(bitmap_device_context, previous_bitmap);
            DeleteDC(bitmap_device_context);
        }

        void PaintOverlayBackgroundRect(HDC destination_device_context, RECT destination_rect, POINT source_origin,
                                        CapturedBitmap const &dimmed_background,
                                        CapturedBitmap const &frozen_background) noexcept
        {
            if (dimmed_background.IsValid())
            {
                PaintBitmapRect(destination_device_context, destination_rect, dimmed_background.Get(), source_origin);
                return;
            }

            if (frozen_background.IsValid())
            {
                PaintBitmapRect(destination_device_context, destination_rect, frozen_background.Get(), source_origin);
            }
            else
            {
                FillRect(destination_device_context, &destination_rect, GetSysColorBrush(COLOR_WINDOWTEXT));
            }

            AlphaFillRect(destination_device_context, destination_rect, kOverlayAlpha);
        }

        void PaintOverlayPreviewRect(HDC destination_device_context, RECT destination_preview_rect,
                                     RECT source_preview_rect, CapturedBitmap const &frozen_background,
                                     COLORREF border_color) noexcept
        {
            if (frozen_background.IsValid())
            {
                PaintBitmapRect(destination_device_context, destination_preview_rect, frozen_background.Get(),
                                POINT{.x = source_preview_rect.left, .y = source_preview_rect.top});
            }

            HPEN selection_pen = CreatePen(PS_SOLID, 2, border_color);
            HGDIOBJ old_pen = SelectObject(destination_device_context, selection_pen);
            HGDIOBJ old_brush = SelectObject(destination_device_context, GetStockObject(HOLLOW_BRUSH));
            Rectangle(destination_device_context, destination_preview_rect.left, destination_preview_rect.top,
                      destination_preview_rect.right, destination_preview_rect.bottom);
            SelectObject(destination_device_context, old_brush);
            SelectObject(destination_device_context, old_pen);
            DeleteObject(selection_pen);
        }

        void PaintResizeHandle(HDC destination_device_context, POINT center_point) noexcept
        {
            HPEN handle_pen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
            HBRUSH handle_brush = CreateSolidBrush(RGB(255, 255, 255));
            HGDIOBJ old_pen = SelectObject(destination_device_context, handle_pen);
            HGDIOBJ old_brush = SelectObject(destination_device_context, handle_brush);
            Ellipse(destination_device_context, center_point.x - kResizeHandleRadius,
                    center_point.y - kResizeHandleRadius, center_point.x + kResizeHandleRadius + 1,
                    center_point.y + kResizeHandleRadius + 1);
            SelectObject(destination_device_context, old_brush);
            SelectObject(destination_device_context, old_pen);
            DeleteObject(handle_brush);
            DeleteObject(handle_pen);
        }

        [[nodiscard]] int DecimalDigitCount(LONG value) noexcept
        {
            int digit_count = 1;
            LONG remaining_value = std::max<LONG>(value, 0);
            while (remaining_value >= 10)
            {
                remaining_value /= 10;
                ++digit_count;
            }

            return digit_count;
        }

        [[nodiscard]] int SelectionMetricsLabelWidth(RECT selection_rect) noexcept
        {
            LONG const selection_width = std::max<LONG>(selection_rect.right - selection_rect.left, 0);
            LONG const selection_height = std::max<LONG>(selection_rect.bottom - selection_rect.top, 0);
            int const label_character_count = DecimalDigitCount(selection_width) + DecimalDigitCount(selection_height) +
                                              7;
            return std::max(kSelectionMetricsMinWidth,
                            (label_character_count * kSelectionMetricsCharWidth) + (kSelectionMetricsPaddingX * 2));
        }

        [[nodiscard]] RECT SelectionMetricsRect(RECT selection_rect, RECT bounds_rect) noexcept
        {
            if (!IsRectNonEmpty(selection_rect))
            {
                return {};
            }

            int const label_width = SelectionMetricsLabelWidth(selection_rect);
            RECT metrics_rect{
                .left = selection_rect.left,
                .top = selection_rect.top - kSelectionMetricsHeight - kSelectionMetricsMargin,
                .right = selection_rect.left + label_width,
                .bottom = selection_rect.top - kSelectionMetricsMargin,
            };

            if (metrics_rect.top < bounds_rect.top)
            {
                metrics_rect.top = selection_rect.top + kSelectionMetricsMargin;
                metrics_rect.bottom = metrics_rect.top + kSelectionMetricsHeight;
            }

            if (metrics_rect.right > bounds_rect.right)
            {
                OffsetRect(&metrics_rect, bounds_rect.right - metrics_rect.right, 0);
            }
            if (metrics_rect.left < bounds_rect.left)
            {
                OffsetRect(&metrics_rect, bounds_rect.left - metrics_rect.left, 0);
            }
            if (metrics_rect.bottom > bounds_rect.bottom)
            {
                OffsetRect(&metrics_rect, 0, bounds_rect.bottom - metrics_rect.bottom);
            }
            if (metrics_rect.top < bounds_rect.top)
            {
                OffsetRect(&metrics_rect, 0, bounds_rect.top - metrics_rect.top);
            }

            return metrics_rect;
        }

        void PaintSelectionMetrics(HDC destination_device_context, RECT destination_rect, RECT selection_rect) noexcept
        {
            if (!IsRectNonEmpty(destination_rect))
            {
                return;
            }

            LONG const selection_width = std::max<LONG>(selection_rect.right - selection_rect.left, 0);
            LONG const selection_height = std::max<LONG>(selection_rect.bottom - selection_rect.top, 0);
            std::wstring const metrics_text = std::to_wstring(selection_width) + L" x " +
                                              std::to_wstring(selection_height) + L" px";

            HPEN frame_pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
            HBRUSH background_brush = CreateSolidBrush(RGB(0, 0, 0));
            HGDIOBJ old_pen = SelectObject(destination_device_context, frame_pen);
            HGDIOBJ old_brush = SelectObject(destination_device_context, background_brush);
            RoundRect(destination_device_context, destination_rect.left, destination_rect.top, destination_rect.right,
                      destination_rect.bottom, 8, 8);
            SelectObject(destination_device_context, old_brush);
            SelectObject(destination_device_context, old_pen);
            DeleteObject(background_brush);
            DeleteObject(frame_pen);

            RECT text_rect = destination_rect;
            InflateRect(&text_rect, -kSelectionMetricsPaddingX, -kSelectionMetricsPaddingY);
            SetBkMode(destination_device_context, TRANSPARENT);
            SetTextColor(destination_device_context, RGB(255, 255, 255));
            DrawTextW(destination_device_context, metrics_text.c_str(), -1, &text_rect,
                      DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }

        void PaintToolbarBackground(HDC destination_device_context, RECT destination_rect) noexcept
        {
            HPEN frame_pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
            HBRUSH background_brush = CreateSolidBrush(RGB(0, 0, 0));
            HGDIOBJ old_pen = SelectObject(destination_device_context, frame_pen);
            HGDIOBJ old_brush = SelectObject(destination_device_context, background_brush);
            RoundRect(destination_device_context, destination_rect.left, destination_rect.top, destination_rect.right,
                      destination_rect.bottom, kToolbarCornerRadius, kToolbarCornerRadius);
            SelectObject(destination_device_context, old_brush);
            SelectObject(destination_device_context, old_pen);
            DeleteObject(background_brush);
            DeleteObject(frame_pen);
        }

        void PaintToolbarButton(HDC destination_device_context, RECT destination_rect, wchar_t const *label) noexcept
        {
            HPEN frame_pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
            HBRUSH background_brush = CreateSolidBrush(RGB(32, 32, 32));
            HGDIOBJ old_pen = SelectObject(destination_device_context, frame_pen);
            HGDIOBJ old_brush = SelectObject(destination_device_context, background_brush);
            RoundRect(destination_device_context, destination_rect.left, destination_rect.top, destination_rect.right,
                      destination_rect.bottom, 8, 8);
            SelectObject(destination_device_context, old_brush);
            SelectObject(destination_device_context, old_pen);
            DeleteObject(background_brush);
            DeleteObject(frame_pen);

            RECT text_rect = destination_rect;
            SetBkMode(destination_device_context, TRANSPARENT);
            SetTextColor(destination_device_context, RGB(255, 255, 255));
            DrawTextW(destination_device_context, label, -1, &text_rect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }

        [[nodiscard]] HWND SelectionRootWindow(HWND window) noexcept
        {
            HWND root_window = GetAncestor(window, GA_ROOTOWNER);
            if (root_window == nullptr)
            {
                root_window = GetAncestor(window, GA_ROOT);
            }
            if (root_window == nullptr)
            {
                root_window = window;
            }

            for (;;)
            {
                HWND const popup_window = GetLastActivePopup(root_window);
                if (popup_window == nullptr || popup_window == root_window || IsWindowVisible(popup_window) == FALSE ||
                    IsIconic(popup_window) != FALSE || IsWindowCloaked(popup_window))
                {
                    break;
                }

                root_window = popup_window;
            }

            return root_window;
        }

        [[nodiscard]] bool ShouldSkipWindowForSelection(HWND window, HWND excluded_window,
                                                        bool allow_desktop_shell_window) noexcept
        {
            if (window == nullptr || window == excluded_window || window == GetDesktopWindow())
            {
                return true;
            }

            if (IsWindowVisible(window) == FALSE || IsIconic(window) != FALSE || IsWindowCloaked(window))
            {
                return true;
            }

            if (!allow_desktop_shell_window && IsDesktopShellWindow(window))
            {
                return true;
            }

            LONG_PTR const ex_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
            return (ex_style & WS_EX_TOOLWINDOW) != 0 && !IsTaskbarShellWindow(window) && !allow_desktop_shell_window;
        }

        [[nodiscard]] bool FindTopLevelWindowRectAtPoint(HWND excluded_window, POINT screen_point,
                                                         RECT &window_rect) noexcept
        {
            RECT const virtual_screen_rect = VirtualScreenRect();
            RECT desktop_shell_fallback_rect{};
            bool has_desktop_shell_fallback = false;
            for (HWND window = GetTopWindow(nullptr); window != nullptr; window = GetWindow(window, GW_HWNDNEXT))
            {
                bool const is_desktop_shell_window = IsDesktopShellWindow(window);
                if (ShouldSkipWindowForSelection(window, excluded_window, is_desktop_shell_window))
                {
                    continue;
                }

                RECT direct_rect{};
                if (!TryGetClippedWindowRectForSelection(window, virtual_screen_rect, direct_rect) ||
                    PtInRect(&direct_rect, screen_point) == FALSE)
                {
                    continue;
                }

                HWND const candidate_window = SelectionRootWindow(window);
                if (candidate_window != window)
                {
                    bool const candidate_is_desktop_shell_window = IsDesktopShellWindow(candidate_window);
                    if (!ShouldSkipWindowForSelection(candidate_window, excluded_window,
                                                      candidate_is_desktop_shell_window))
                    {
                        RECT candidate_rect{};
                        if (TryGetClippedWindowRectForSelection(candidate_window, virtual_screen_rect,
                                                                candidate_rect) &&
                            PtInRect(&candidate_rect, screen_point) != FALSE)
                        {
                            if (!candidate_is_desktop_shell_window)
                            {
                                window_rect = candidate_rect;
                                return true;
                            }

                            if (TryGetDesktopWorkAreaForPoint(screen_point, desktop_shell_fallback_rect))
                            {
                                has_desktop_shell_fallback = true;
                            }
                        }
                    }
                }

                if (is_desktop_shell_window)
                {
                    if (TryGetDesktopWorkAreaForPoint(screen_point, desktop_shell_fallback_rect))
                    {
                        has_desktop_shell_fallback = true;
                    }
                    continue;
                }

                window_rect = direct_rect;
                return true;
            }

            if (has_desktop_shell_fallback)
            {
                window_rect = desktop_shell_fallback_rect;
                return true;
            }

            return false;
        }

        void AlphaFillRect(HDC destination_device_context, RECT rect, BYTE alpha) noexcept
        {
            if (!IsRectNonEmpty(rect))
            {
                return;
            }

            HDC source_device_context = CreateCompatibleDC(destination_device_context);
            if (source_device_context == nullptr)
            {
                return;
            }

            HBITMAP bitmap = CreateCompatibleBitmap(destination_device_context, 1, 1);
            if (bitmap == nullptr)
            {
                DeleteDC(source_device_context);
                return;
            }

            HGDIOBJ previous_bitmap = SelectObject(source_device_context, bitmap);
            SetPixelV(source_device_context, 0, 0, RGB(0, 0, 0));
            BLENDFUNCTION const blend_function{
                .BlendOp = AC_SRC_OVER,
                .BlendFlags = 0,
                .SourceConstantAlpha = alpha,
                .AlphaFormat = 0,
            };
            (void)AlphaBlend(destination_device_context, rect.left, rect.top, rect.right - rect.left,
                             rect.bottom - rect.top, source_device_context, 0, 0, 1, 1, blend_function);

            SelectObject(source_device_context, previous_bitmap);
            DeleteObject(bitmap);
            DeleteDC(source_device_context);
        }
    } // namespace

    CaptureOverlay::CaptureOverlay(HINSTANCE instance) noexcept : instance_(instance) {}

    bool CaptureOverlay::Show(HWND owner_window)
    {
        CAPTUREZY_LOG_DEBUG(core::LogCategory::Capture, L"Show capture overlay.");
        owner_window_ = owner_window;
        origin_left_ = 0;
        origin_top_ = 0;
        last_selection_rect_ = {};
        final_capture_result_ = {};
        drag_start_ = {};
        drag_current_ = {};
        hover_window_rect_ = {};
        click_candidate_window_rect_ = {};
        committed_selection_rect_ = {};
        resize_anchor_selection_rect_ = {};
        resize_anchor_handle_ = ResizeHandle::None;
        pointer_down_ = false;
        drag_in_progress_ = false;
        has_selection_ = false;
        has_hover_window_ = false;
        has_click_candidate_window_ = false;
        has_committed_selection_ = false;
        pointer_drag_mode_ = PointerDragMode::None;
        active_resize_handle_ = ResizeHandle::None;
        pressed_toolbar_action_ = ToolbarAction::None;

        if (overlay_window_ != nullptr)
        {
            ShowWindow(overlay_window_, SW_SHOW);
            SetForegroundWindow(overlay_window_);
            return true;
        }

        if (RegisterWindowClass() == 0)
        {
            CAPTUREZY_LOG_ERROR(core::LogCategory::Capture, L"Capture overlay class registration failed.");
            return false;
        }

        int const left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int const top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int const width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int const height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        origin_left_ = left;
        origin_top_ = top;
        frozen_background_ = ScreenCapture::CaptureRegion(
                                 RECT{.left = left, .top = top, .right = left + width, .bottom = top + height})
                                 .Bitmap()
                                 .Clone();
        dimmed_background_ = CreateDimmedBitmap(frozen_background_, kOverlayAlpha);

        overlay_window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, kOverlayWindowClassName,
                                          L"CaptureZY Overlay", WS_POPUP, left, top, width, height, owner_window_,
                                          nullptr, instance_, this);

        if (overlay_window_ == nullptr)
        {
            CAPTUREZY_LOG_ERROR(core::LogCategory::Capture, L"Capture overlay window creation failed.");
            return false;
        }

        SetLayeredWindowAttributes(overlay_window_, 0, 255, LWA_ALPHA);
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
        frozen_background_ = {};
        dimmed_background_ = {};
    }

    bool CaptureOverlay::IsVisible() const noexcept
    {
        return overlay_window_ != nullptr && IsWindowVisible(overlay_window_) != FALSE;
    }

    RECT CaptureOverlay::LastSelectionRect() const noexcept
    {
        return last_selection_rect_;
    }

    CaptureResult CaptureOverlay::FrozenSelectionResult() const noexcept
    {
        if (final_capture_result_.IsValid())
        {
            return final_capture_result_.Clone();
        }

        if (!frozen_background_.IsValid())
        {
            return {};
        }

        int const selection_width = last_selection_rect_.right - last_selection_rect_.left;
        int const selection_height = last_selection_rect_.bottom - last_selection_rect_.top;
        if (selection_width <= 0 || selection_height <= 0)
        {
            return {};
        }

        int const source_left = last_selection_rect_.left - origin_left_;
        int const source_top = last_selection_rect_.top - origin_top_;

        HDC screen_device_context = GetDC(nullptr);
        if (screen_device_context == nullptr)
        {
            return {};
        }

        HDC source_device_context = CreateCompatibleDC(screen_device_context);
        HDC destination_device_context = CreateCompatibleDC(screen_device_context);
        if (source_device_context == nullptr || destination_device_context == nullptr)
        {
            if (destination_device_context != nullptr)
            {
                DeleteDC(destination_device_context);
            }
            if (source_device_context != nullptr)
            {
                DeleteDC(source_device_context);
            }
            ReleaseDC(nullptr, screen_device_context);
            return {};
        }

        HBITMAP cropped_bitmap = CreateCompatibleBitmap(screen_device_context, selection_width, selection_height);
        if (cropped_bitmap == nullptr)
        {
            DeleteDC(destination_device_context);
            DeleteDC(source_device_context);
            ReleaseDC(nullptr, screen_device_context);
            return {};
        }

        HGDIOBJ previous_source_bitmap = SelectObject(source_device_context, frozen_background_.Get());
        HGDIOBJ previous_destination_bitmap = SelectObject(destination_device_context, cropped_bitmap);
        bool const copied = BitBlt(destination_device_context, 0, 0, selection_width, selection_height,
                                   source_device_context, source_left, source_top, SRCCOPY) != FALSE;

        SelectObject(destination_device_context, previous_destination_bitmap);
        SelectObject(source_device_context, previous_source_bitmap);
        DeleteDC(destination_device_context);
        DeleteDC(source_device_context);
        ReleaseDC(nullptr, screen_device_context);

        if (!copied)
        {
            DeleteObject(cropped_bitmap);
            return {};
        }

        return {CapturedBitmap(cropped_bitmap, SIZE{.cx = selection_width, .cy = selection_height}),
                last_selection_rect_, std::chrono::system_clock::now()};
    }

    RECT CaptureOverlay::CurrentSelectionRect() const noexcept
    {
        RECT selection{};
        selection.left = std::min(drag_start_.x, drag_current_.x);
        selection.top = std::min(drag_start_.y, drag_current_.y);
        selection.right = std::max(drag_start_.x, drag_current_.x);
        selection.bottom = std::max(drag_start_.y, drag_current_.y);
        return selection;
    }

    RECT CaptureOverlay::CurrentSelectionRectScreen() const noexcept
    {
        RECT selection = CurrentSelectionRect();
        OffsetRect(&selection, origin_left_, origin_top_);
        return selection;
    }

    RECT CaptureOverlay::OverlayRectScreen() const noexcept
    {
        RECT overlay_rect{};
        if (overlay_window_ != nullptr && GetWindowRect(overlay_window_, &overlay_rect) != FALSE)
        {
            return overlay_rect;
        }

        overlay_rect.left = origin_left_;
        overlay_rect.top = origin_top_;
        overlay_rect.right = origin_left_ + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        overlay_rect.bottom = origin_top_ + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        return overlay_rect;
    }

    RECT CaptureOverlay::OverlayToClientRect(RECT rect) const noexcept
    {
        OffsetRect(&rect, -origin_left_, -origin_top_);
        return rect;
    }

    bool CaptureOverlay::HasResizeHandle(ResizeHandle handle, ResizeHandle component) noexcept
    {
        return (static_cast<unsigned int>(handle) & static_cast<unsigned int>(component)) != 0U;
    }

    bool CaptureOverlay::ShouldShowResizeHandles(RECT selection_rect) noexcept
    {
        return (selection_rect.right - selection_rect.left) >= kResizeHandleDisplayExtent &&
               (selection_rect.bottom - selection_rect.top) >= kResizeHandleDisplayExtent;
    }

    HCURSOR CaptureOverlay::CursorForResizeHandle(ResizeHandle handle) noexcept
    {
        LPCWSTR cursor_id = IDC_CROSS;
        switch (handle)
        {
        case ResizeHandle::Left:
        case ResizeHandle::Right:
            cursor_id = IDC_SIZEWE;
            break;

        case ResizeHandle::Top:
        case ResizeHandle::Bottom:
            cursor_id = IDC_SIZENS;
            break;

        case ResizeHandle::LeftTop:
        case ResizeHandle::RightBottom:
            cursor_id = IDC_SIZENWSE;
            break;

        case ResizeHandle::RightTop:
        case ResizeHandle::LeftBottom:
            cursor_id = IDC_SIZENESW;
            break;

        case ResizeHandle::None:
        default:
            break;
        }

        return LoadCursorW(nullptr, cursor_id);
    }

    HCURSOR CaptureOverlay::MoveSelectionCursor() noexcept
    {
        return LoadCursorW(nullptr, IDC_SIZEALL);
    }

    HCURSOR CaptureOverlay::ToolbarCursor() noexcept
    {
        return LoadCursorW(nullptr, IDC_HAND);
    }

    wchar_t const *CaptureOverlay::ToolbarActionLabel(ToolbarAction action) noexcept
    {
        switch (action)
        {
        case ToolbarAction::CopyAndPin:
            return L"贴图";

        case ToolbarAction::CopyOnly:
            return L"复制";

        case ToolbarAction::SaveToFile:
            return L"保存";

        case ToolbarAction::Cancel:
            return L"取消";

        case ToolbarAction::None:
        default:
            return L"";
        }
    }

    RECT CaptureOverlay::ToolbarRect(RECT selection_rect, RECT bounds_rect) const noexcept
    {
        if (!has_committed_selection_ || !IsRectNonEmpty(selection_rect))
        {
            return {};
        }

        constexpr int kToolbarButtonCount = 4;
        int const toolbar_width = (kToolbarButtonWidth * kToolbarButtonCount) +
                                  (kToolbarSpacing * (kToolbarButtonCount - 1)) + (kToolbarPadding * 2);
        int const toolbar_height = kToolbarButtonHeight + (kToolbarPadding * 2);
        int const selection_center_x = (selection_rect.left + selection_rect.right) / 2;
        RECT toolbar_rect{
            .left = selection_center_x - (toolbar_width / 2),
            .top = selection_rect.bottom + kToolbarMargin,
            .right = selection_center_x + ((toolbar_width + 1) / 2),
            .bottom = selection_rect.bottom + kToolbarMargin + toolbar_height,
        };

        if (toolbar_rect.bottom > bounds_rect.bottom)
        {
            toolbar_rect.top = selection_rect.top - kToolbarMargin - toolbar_height;
            toolbar_rect.bottom = toolbar_rect.top + toolbar_height;
        }

        if (toolbar_rect.left < bounds_rect.left)
        {
            OffsetRect(&toolbar_rect, bounds_rect.left - toolbar_rect.left, 0);
        }
        if (toolbar_rect.right > bounds_rect.right)
        {
            OffsetRect(&toolbar_rect, bounds_rect.right - toolbar_rect.right, 0);
        }
        if (toolbar_rect.top < bounds_rect.top)
        {
            OffsetRect(&toolbar_rect, 0, bounds_rect.top - toolbar_rect.top);
        }
        if (toolbar_rect.bottom > bounds_rect.bottom)
        {
            OffsetRect(&toolbar_rect, 0, bounds_rect.bottom - toolbar_rect.bottom);
        }

        return toolbar_rect;
    }

    RECT CaptureOverlay::ToolbarButtonRect(RECT toolbar_rect, ToolbarAction action) noexcept
    {
        if (!IsRectNonEmpty(toolbar_rect))
        {
            return {};
        }

        int action_index = -1;
        switch (action)
        {
        case ToolbarAction::CopyAndPin:
            action_index = 0;
            break;

        case ToolbarAction::CopyOnly:
            action_index = 1;
            break;

        case ToolbarAction::SaveToFile:
            action_index = 2;
            break;

        case ToolbarAction::Cancel:
            action_index = 3;
            break;

        case ToolbarAction::None:
        default:
            return {};
        }

        RECT button_rect{
            .left = toolbar_rect.left + kToolbarPadding + (action_index * (kToolbarButtonWidth + kToolbarSpacing)),
            .top = toolbar_rect.top + kToolbarPadding,
            .right = toolbar_rect.left + kToolbarPadding + (action_index * (kToolbarButtonWidth + kToolbarSpacing)) +
                     kToolbarButtonWidth,
            .bottom = toolbar_rect.top + kToolbarPadding + kToolbarButtonHeight,
        };
        return button_rect;
    }

    CaptureOverlay::ToolbarAction CaptureOverlay::HitTestToolbarAction(POINT overlay_point) const noexcept
    {
        if (!has_committed_selection_)
        {
            return ToolbarAction::None;
        }

        RECT client_rect{};
        GetClientRect(overlay_window_, &client_rect);
        RECT const selection_rect = OverlayToClientRect(committed_selection_rect_);
        RECT const toolbar_rect = ToolbarRect(selection_rect, client_rect);
        if (!IsRectNonEmpty(toolbar_rect) || PtInRect(&toolbar_rect, overlay_point) == FALSE)
        {
            return ToolbarAction::None;
        }

        constexpr std::array<ToolbarAction, 4> kToolbarActions{
            ToolbarAction::CopyAndPin,
            ToolbarAction::CopyOnly,
            ToolbarAction::SaveToFile,
            ToolbarAction::Cancel,
        };
        for (ToolbarAction const action : kToolbarActions)
        {
            RECT const button_rect = ToolbarButtonRect(toolbar_rect, action);
            if (PtInRect(&button_rect, overlay_point) != FALSE)
            {
                return action;
            }
        }

        return ToolbarAction::None;
    }

    bool CaptureOverlay::IsPointInsideCommittedSelection(POINT overlay_point) const noexcept
    {
        if (!has_committed_selection_)
        {
            return false;
        }

        RECT selection_rect = OverlayToClientRect(committed_selection_rect_);
        return PtInRect(&selection_rect, overlay_point) != FALSE;
    }

    CaptureOverlay::ResizeHandle
    CaptureOverlay::HitTestCommittedSelectionResizeHandle(POINT overlay_point) const noexcept
    {
        if (!has_committed_selection_)
        {
            return ResizeHandle::None;
        }

        RECT selection_rect = OverlayToClientRect(committed_selection_rect_);
        if (!IsRectNonEmpty(selection_rect))
        {
            return ResizeHandle::None;
        }

        RECT expanded_rect = ExpandedRect(selection_rect, std::max(kSelectionResizePadding, kResizeHandleHitRadius));
        if (PtInRect(&expanded_rect, overlay_point) == FALSE)
        {
            return ResizeHandle::None;
        }

        auto const append_handle = [](ResizeHandle handle, ResizeHandle component) noexcept {
            return static_cast<ResizeHandle>(static_cast<unsigned int>(handle) | static_cast<unsigned int>(component));
        };

        if (ShouldShowResizeHandles(selection_rect))
        {
            int const center_x = (selection_rect.left + selection_rect.right) / 2;
            int const center_y = (selection_rect.top + selection_rect.bottom) / 2;
            std::array<std::pair<POINT, ResizeHandle>, 8> const handle_points{{
                {POINT{.x = selection_rect.left, .y = selection_rect.top}, ResizeHandle::LeftTop},
                {POINT{.x = center_x, .y = selection_rect.top}, ResizeHandle::Top},
                {POINT{.x = selection_rect.right, .y = selection_rect.top}, ResizeHandle::RightTop},
                {POINT{.x = selection_rect.right, .y = center_y}, ResizeHandle::Right},
                {POINT{.x = selection_rect.right, .y = selection_rect.bottom}, ResizeHandle::RightBottom},
                {POINT{.x = center_x, .y = selection_rect.bottom}, ResizeHandle::Bottom},
                {POINT{.x = selection_rect.left, .y = selection_rect.bottom}, ResizeHandle::LeftBottom},
                {POINT{.x = selection_rect.left, .y = center_y}, ResizeHandle::Left},
            }};
            for (auto const &[center_point, handle] : handle_points)
            {
                int const delta_x = overlay_point.x - center_point.x;
                int const delta_y = overlay_point.y - center_point.y;
                if ((delta_x * delta_x) + (delta_y * delta_y) <= (kResizeHandleHitRadius * kResizeHandleHitRadius))
                {
                    return handle;
                }
            }
        }

        ResizeHandle handle = ResizeHandle::None;
        if (std::abs(overlay_point.x - selection_rect.left) <= kSelectionResizePadding)
        {
            handle = append_handle(handle, ResizeHandle::Left);
        }
        else if (std::abs(overlay_point.x - selection_rect.right) <= kSelectionResizePadding)
        {
            handle = append_handle(handle, ResizeHandle::Right);
        }

        if (std::abs(overlay_point.y - selection_rect.top) <= kSelectionResizePadding)
        {
            handle = append_handle(handle, ResizeHandle::Top);
        }
        else if (std::abs(overlay_point.y - selection_rect.bottom) <= kSelectionResizePadding)
        {
            handle = append_handle(handle, ResizeHandle::Bottom);
        }

        return handle;
    }

    bool CaptureOverlay::TryGetCurrentPreviewRect(RECT &rect) const noexcept
    {
        if (drag_in_progress_ && has_selection_)
        {
            rect = CurrentSelectionRect();
            return IsRectNonEmpty(rect);
        }

        if (has_committed_selection_)
        {
            rect = OverlayToClientRect(committed_selection_rect_);
            return IsRectNonEmpty(rect);
        }

        if (has_hover_window_ && !drag_in_progress_)
        {
            rect = OverlayToClientRect(hover_window_rect_);
            return IsRectNonEmpty(rect);
        }

        rect = {};
        return false;
    }

    bool CaptureOverlay::UpdateHoverWindowFromScreenPoint(POINT screen_point) noexcept
    {
        RECT window_rect{};
        bool const found = FindTopLevelWindowRectAtPoint(overlay_window_, screen_point, window_rect);
        bool const changed = found != has_hover_window_ ||
                             (found && EqualRect(&window_rect, &hover_window_rect_) == FALSE);
        has_hover_window_ = found;
        hover_window_rect_ = found ? window_rect : RECT{};
        return changed;
    }

    void CaptureOverlay::InvalidatePreviewRectChange(RECT old_preview_rect, bool had_old_preview, RECT new_preview_rect,
                                                     bool had_new_preview) noexcept
    {
        if (overlay_window_ == nullptr || (!had_old_preview && !had_new_preview))
        {
            return;
        }

        RECT client_rect{};
        GetClientRect(overlay_window_, &client_rect);
        RECT invalid_rect{};
        RECT old_metrics_rect{};
        RECT new_metrics_rect{};
        RECT old_toolbar_rect{};
        RECT new_toolbar_rect{};
        if (had_old_preview)
        {
            old_metrics_rect = SelectionMetricsRect(old_preview_rect, client_rect);
            old_toolbar_rect = ToolbarRect(old_preview_rect, client_rect);
        }
        if (had_new_preview)
        {
            new_metrics_rect = SelectionMetricsRect(new_preview_rect, client_rect);
            new_toolbar_rect = ToolbarRect(new_preview_rect, client_rect);
        }
        if (!TryUnionRect(had_old_preview ? ExpandedRect(old_preview_rect, kPreviewInvalidationPadding) : RECT{},
                          had_new_preview ? ExpandedRect(new_preview_rect, kPreviewInvalidationPadding) : RECT{},
                          invalid_rect))
        {
            return;
        }
        (void)TryUnionRect(invalid_rect, old_metrics_rect, invalid_rect);
        (void)TryUnionRect(invalid_rect, new_metrics_rect, invalid_rect);
        (void)TryUnionRect(invalid_rect, old_toolbar_rect, invalid_rect);
        (void)TryUnionRect(invalid_rect, new_toolbar_rect, invalid_rect);

        RECT clipped_invalid_rect{};
        if (IntersectRect(&clipped_invalid_rect, &invalid_rect, &client_rect) != FALSE)
        {
            InvalidateRect(overlay_window_, &clipped_invalid_rect, FALSE);
        }
    }

    void CaptureOverlay::UpdateCursorForOverlayPoint(POINT overlay_point) noexcept
    {
        if (pointer_drag_mode_ == PointerDragMode::ResizeSelection && pointer_down_)
        {
            SetCursor(CursorForResizeHandle(active_resize_handle_));
            return;
        }

        if (pointer_drag_mode_ == PointerDragMode::MoveSelection && pointer_down_)
        {
            SetCursor(MoveSelectionCursor());
            return;
        }

        if (has_committed_selection_)
        {
            if (HitTestToolbarAction(overlay_point) != ToolbarAction::None)
            {
                active_resize_handle_ = ResizeHandle::None;
                SetCursor(ToolbarCursor());
                return;
            }

            ResizeHandle const handle = HitTestCommittedSelectionResizeHandle(overlay_point);
            active_resize_handle_ = handle;
            if (handle != ResizeHandle::None)
            {
                SetCursor(CursorForResizeHandle(handle));
                return;
            }

            if (IsPointInsideCommittedSelection(overlay_point))
            {
                SetCursor(MoveSelectionCursor());
                return;
            }
        }

        active_resize_handle_ = ResizeHandle::None;
        SetCursor(CursorForResizeHandle(ResizeHandle::None));
    }

    void CaptureOverlay::ResetCommittedSelection() noexcept
    {
        has_committed_selection_ = false;
        committed_selection_rect_ = {};
        resize_anchor_selection_rect_ = {};
        resize_anchor_handle_ = ResizeHandle::None;
        has_selection_ = false;
        drag_in_progress_ = false;
        has_click_candidate_window_ = false;
        pointer_drag_mode_ = PointerDragMode::None;
        active_resize_handle_ = ResizeHandle::None;
        pressed_toolbar_action_ = ToolbarAction::None;
    }

    void CaptureOverlay::BeginMoveSelection(POINT overlay_point) noexcept
    {
        drag_start_ = overlay_point;
        drag_current_ = overlay_point;
        pointer_down_ = true;
        drag_in_progress_ = false;
        has_selection_ = false;
        has_click_candidate_window_ = false;
        pointer_drag_mode_ = PointerDragMode::MoveSelection;
        resize_anchor_selection_rect_ = committed_selection_rect_;
        resize_anchor_handle_ = ResizeHandle::None;
        pressed_toolbar_action_ = ToolbarAction::None;
        SetCapture(overlay_window_);
    }

    void CaptureOverlay::UpdateMoveSelection(POINT overlay_point) noexcept
    {
        drag_current_ = overlay_point;

        LONG delta_x = overlay_point.x - drag_start_.x;
        LONG delta_y = overlay_point.y - drag_start_.y;
        if (!drag_in_progress_)
        {
            if (std::abs(delta_x) < kDragThreshold && std::abs(delta_y) < kDragThreshold)
            {
                return;
            }

            drag_in_progress_ = true;
        }

        RECT moved_rect = resize_anchor_selection_rect_;
        RECT const overlay_rect = OverlayRectScreen();
        LONG const min_delta_x = overlay_rect.left - resize_anchor_selection_rect_.left;
        LONG const max_delta_x = overlay_rect.right - resize_anchor_selection_rect_.right;
        LONG const min_delta_y = overlay_rect.top - resize_anchor_selection_rect_.top;
        LONG const max_delta_y = overlay_rect.bottom - resize_anchor_selection_rect_.bottom;
        delta_x = std::clamp(delta_x, min_delta_x, max_delta_x);
        delta_y = std::clamp(delta_y, min_delta_y, max_delta_y);
        OffsetRect(&moved_rect, delta_x, delta_y);

        if (EqualRect(&moved_rect, &committed_selection_rect_) == FALSE)
        {
            committed_selection_rect_ = moved_rect;
        }
    }

    void CaptureOverlay::BeginResizeSelection(POINT overlay_point) noexcept
    {
        drag_start_ = overlay_point;
        drag_current_ = overlay_point;
        pointer_down_ = true;
        drag_in_progress_ = false;
        has_selection_ = false;
        has_click_candidate_window_ = false;
        pointer_drag_mode_ = PointerDragMode::ResizeSelection;
        resize_anchor_selection_rect_ = committed_selection_rect_;
        resize_anchor_handle_ = active_resize_handle_;
        pressed_toolbar_action_ = ToolbarAction::None;
        SetCapture(overlay_window_);
    }

    void CaptureOverlay::UpdateResizeSelection(POINT overlay_point) noexcept
    {
        drag_current_ = overlay_point;
        POINT const screen_point{.x = overlay_point.x + origin_left_, .y = overlay_point.y + origin_top_};
        RECT resized_rect = resize_anchor_selection_rect_;
        ResizeHandle resolved_handle = ResizeHandle::None;
        auto const append_handle = [](ResizeHandle handle, ResizeHandle component) noexcept {
            return static_cast<ResizeHandle>(static_cast<unsigned int>(handle) | static_cast<unsigned int>(component));
        };
        ResizeHandle horizontal_handle = ResizeHandle::None;
        if (HasResizeHandle(resize_anchor_handle_, ResizeHandle::Left) ||
            HasResizeHandle(resize_anchor_handle_, ResizeHandle::Right))
        {
            LONG const fixed_x = HasResizeHandle(resize_anchor_handle_, ResizeHandle::Left)
                                     ? resize_anchor_selection_rect_.right
                                     : resize_anchor_selection_rect_.left;
            if (screen_point.x <= fixed_x)
            {
                resized_rect.left = std::min(screen_point.x, fixed_x - static_cast<LONG>(kMinSelectionExtent));
                resized_rect.right = fixed_x;
                horizontal_handle = ResizeHandle::Left;
            }
            else
            {
                resized_rect.left = fixed_x;
                resized_rect.right = std::max(screen_point.x, fixed_x + static_cast<LONG>(kMinSelectionExtent));
                horizontal_handle = ResizeHandle::Right;
            }
        }
        ResizeHandle vertical_handle = ResizeHandle::None;
        if (HasResizeHandle(resize_anchor_handle_, ResizeHandle::Top) ||
            HasResizeHandle(resize_anchor_handle_, ResizeHandle::Bottom))
        {
            LONG const fixed_y = HasResizeHandle(resize_anchor_handle_, ResizeHandle::Top)
                                     ? resize_anchor_selection_rect_.bottom
                                     : resize_anchor_selection_rect_.top;
            if (screen_point.y <= fixed_y)
            {
                resized_rect.top = std::min(screen_point.y, fixed_y - static_cast<LONG>(kMinSelectionExtent));
                resized_rect.bottom = fixed_y;
                vertical_handle = ResizeHandle::Top;
            }
            else
            {
                resized_rect.top = fixed_y;
                resized_rect.bottom = std::max(screen_point.y, fixed_y + static_cast<LONG>(kMinSelectionExtent));
                vertical_handle = ResizeHandle::Bottom;
            }
        }
        if (horizontal_handle != ResizeHandle::None)
        {
            resolved_handle = append_handle(resolved_handle, horizontal_handle);
        }
        if (vertical_handle != ResizeHandle::None)
        {
            resolved_handle = append_handle(resolved_handle, vertical_handle);
        }
        active_resize_handle_ = resolved_handle;

        if (EqualRect(&resized_rect, &committed_selection_rect_) == FALSE)
        {
            drag_in_progress_ = true;
            committed_selection_rect_ = resized_rect;
        }
    }

    void CaptureOverlay::FinishCommittedSelection(OverlayResult result) noexcept
    {
        if (!has_committed_selection_)
        {
            return;
        }

        last_selection_rect_ = committed_selection_rect_;
        Finish(result);
    }

    bool CaptureOverlay::HandleKeyDown(WPARAM w_param)
    {
        if (w_param == VK_ESCAPE)
        {
            Finish(OverlayResult::Cancelled);
            return true;
        }

        if (!has_committed_selection_)
        {
            if (w_param == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) != 0)
            {
                committed_selection_rect_ = OverlayRectScreen();
                has_committed_selection_ = IsRectNonEmpty(committed_selection_rect_);
                has_selection_ = false;
                drag_in_progress_ = false;
                has_click_candidate_window_ = false;
                resize_anchor_selection_rect_ = {};
                resize_anchor_handle_ = ResizeHandle::None;
                if (has_committed_selection_)
                {
                    POINT cursor_position{};
                    GetCursorPos(&cursor_position);
                    ScreenToClient(overlay_window_, &cursor_position);
                    UpdateCursorForOverlayPoint(cursor_position);
                    InvalidateRect(overlay_window_, nullptr, FALSE);
                }
                return true;
            }

            return false;
        }

        if (w_param == 'P')
        {
            FinishCommittedSelection(OverlayResult::CopyAndPin);
            return true;
        }

        if (w_param == 'C' && (GetKeyState(VK_CONTROL) & 0x8000) != 0)
        {
            FinishCommittedSelection(OverlayResult::CopyOnly);
            return true;
        }

        if (w_param == 'S' && (GetKeyState(VK_CONTROL) & 0x8000) != 0)
        {
            FinishCommittedSelection(OverlayResult::SaveToFile);
            return true;
        }

        if (w_param == VK_BACK || w_param == VK_DELETE)
        {
            RECT old_preview_rect{};
            bool const had_old_preview = TryGetCurrentPreviewRect(old_preview_rect);
            ResetCommittedSelection();
            POINT cursor_position{};
            GetCursorPos(&cursor_position);
            (void)UpdateHoverWindowFromScreenPoint(cursor_position);
            ScreenToClient(overlay_window_, &cursor_position);
            UpdateCursorForOverlayPoint(cursor_position);
            RECT new_preview_rect{};
            bool const had_new_preview = TryGetCurrentPreviewRect(new_preview_rect);
            InvalidatePreviewRectChange(old_preview_rect, had_old_preview, new_preview_rect, had_new_preview);
            return true;
        }

        return false;
    }

    void CaptureOverlay::BeginPointerSelection(LPARAM l_param) noexcept
    {
        POINT const overlay_point{.x = GET_X_LPARAM(l_param), .y = GET_Y_LPARAM(l_param)};
        ToolbarAction const toolbar_action = HitTestToolbarAction(overlay_point);
        if (toolbar_action != ToolbarAction::None)
        {
            drag_start_ = overlay_point;
            drag_current_ = overlay_point;
            pointer_down_ = true;
            drag_in_progress_ = false;
            has_selection_ = false;
            has_click_candidate_window_ = false;
            pointer_drag_mode_ = PointerDragMode::None;
            active_resize_handle_ = ResizeHandle::None;
            resize_anchor_handle_ = ResizeHandle::None;
            pressed_toolbar_action_ = toolbar_action;
            SetCapture(overlay_window_);
            return;
        }

        active_resize_handle_ = HitTestCommittedSelectionResizeHandle(overlay_point);
        if (active_resize_handle_ != ResizeHandle::None)
        {
            BeginResizeSelection(overlay_point);
            return;
        }

        if (IsPointInsideCommittedSelection(overlay_point))
        {
            BeginMoveSelection(overlay_point);
            return;
        }

        drag_start_ = overlay_point;
        drag_current_ = drag_start_;
        pointer_down_ = true;
        drag_in_progress_ = false;
        has_selection_ = false;
        has_click_candidate_window_ = false;
        pointer_drag_mode_ = PointerDragMode::CreateSelection;
        if (!has_committed_selection_ && has_hover_window_)
        {
            click_candidate_window_rect_ = hover_window_rect_;
            has_click_candidate_window_ = true;
        }

        SetCapture(overlay_window_);
    }

    void CaptureOverlay::UpdatePointerSelection(LPARAM l_param)
    {
        RECT old_preview_rect{};
        bool const had_old_preview = TryGetCurrentPreviewRect(old_preview_rect);

        if (pointer_down_)
        {
            POINT const overlay_point{.x = GET_X_LPARAM(l_param), .y = GET_Y_LPARAM(l_param)};
            if (pressed_toolbar_action_ != ToolbarAction::None)
            {
                SetCursor(ToolbarCursor());
            }
            else if (pointer_drag_mode_ == PointerDragMode::ResizeSelection)
            {
                UpdateResizeSelection(overlay_point);
            }
            else if (pointer_drag_mode_ == PointerDragMode::MoveSelection)
            {
                UpdateMoveSelection(overlay_point);
            }
            else
            {
                drag_current_ = overlay_point;
                if (!drag_in_progress_ && (std::abs(drag_current_.x - drag_start_.x) >= kDragThreshold ||
                                           std::abs(drag_current_.y - drag_start_.y) >= kDragThreshold))
                {
                    drag_in_progress_ = true;
                    has_selection_ = true;
                    has_click_candidate_window_ = false;
                }
            }

            if (pressed_toolbar_action_ == ToolbarAction::None)
            {
                UpdateCursorForOverlayPoint(overlay_point);
            }

            RECT new_preview_rect{};
            bool const had_new_preview = TryGetCurrentPreviewRect(new_preview_rect);
            InvalidatePreviewRectChange(old_preview_rect, had_old_preview, new_preview_rect, had_new_preview);
            return;
        }

        if (has_committed_selection_)
        {
            POINT const overlay_point{.x = GET_X_LPARAM(l_param), .y = GET_Y_LPARAM(l_param)};
            UpdateCursorForOverlayPoint(overlay_point);
            return;
        }

        POINT cursor_position{};
        GetCursorPos(&cursor_position);
        if (UpdateHoverWindowFromScreenPoint(cursor_position))
        {
            RECT new_preview_rect{};
            bool const had_new_preview = TryGetCurrentPreviewRect(new_preview_rect);
            InvalidatePreviewRectChange(old_preview_rect, had_old_preview, new_preview_rect, had_new_preview);
        }
    }

    void CaptureOverlay::CompletePointerSelection(LPARAM l_param) noexcept
    {
        if (!pointer_down_)
        {
            return;
        }

        pointer_down_ = false;
        bool const was_dragging = drag_in_progress_;
        bool const had_click_candidate = has_click_candidate_window_;
        PointerDragMode const pointer_drag_mode = pointer_drag_mode_;
        ToolbarAction const pressed_toolbar_action = pressed_toolbar_action_;
        RECT const click_candidate_rect = click_candidate_window_rect_;
        ReleaseCapture();
        drag_current_.x = GET_X_LPARAM(l_param);
        drag_current_.y = GET_Y_LPARAM(l_param);
        pointer_drag_mode_ = PointerDragMode::None;
        active_resize_handle_ = ResizeHandle::None;
        resize_anchor_selection_rect_ = {};
        resize_anchor_handle_ = ResizeHandle::None;
        pressed_toolbar_action_ = ToolbarAction::None;
        if (pressed_toolbar_action != ToolbarAction::None)
        {
            ToolbarAction const released_toolbar_action = HitTestToolbarAction(drag_current_);
            if (pressed_toolbar_action == released_toolbar_action)
            {
                switch (pressed_toolbar_action)
                {
                case ToolbarAction::CopyAndPin:
                    FinishCommittedSelection(OverlayResult::CopyAndPin);
                    return;

                case ToolbarAction::CopyOnly:
                    FinishCommittedSelection(OverlayResult::CopyOnly);
                    return;

                case ToolbarAction::SaveToFile:
                    FinishCommittedSelection(OverlayResult::SaveToFile);
                    return;

                case ToolbarAction::Cancel:
                    ResetCommittedSelection();
                    UpdateCursorForOverlayPoint(drag_current_);
                    InvalidateRect(overlay_window_, nullptr, FALSE);
                    return;

                case ToolbarAction::None:
                default:
                    break;
                }
            }

            UpdateCursorForOverlayPoint(drag_current_);
            return;
        }

        if (pointer_drag_mode == PointerDragMode::ResizeSelection)
        {
            drag_in_progress_ = false;
            if (has_committed_selection_)
            {
                UpdateCursorForOverlayPoint(drag_current_);
                InvalidateRect(overlay_window_, nullptr, FALSE);
            }
            return;
        }

        if (pointer_drag_mode == PointerDragMode::MoveSelection && was_dragging)
        {
            drag_in_progress_ = false;
            if (has_committed_selection_)
            {
                UpdateCursorForOverlayPoint(drag_current_);
                InvalidateRect(overlay_window_, nullptr, FALSE);
            }
            return;
        }

        if (was_dragging)
        {
            drag_in_progress_ = false;
            committed_selection_rect_ = CurrentSelectionRectScreen();
            has_committed_selection_ = IsRectNonEmpty(committed_selection_rect_);
            has_selection_ = false;
            if (has_committed_selection_)
            {
                UpdateCursorForOverlayPoint(drag_current_);
                InvalidateRect(overlay_window_, nullptr, FALSE);
            }
            return;
        }

        if (had_click_candidate)
        {
            has_click_candidate_window_ = false;
            committed_selection_rect_ = click_candidate_rect;
            has_committed_selection_ = IsRectNonEmpty(committed_selection_rect_);
            if (has_committed_selection_)
            {
                UpdateCursorForOverlayPoint(drag_current_);
                InvalidateRect(overlay_window_, nullptr, FALSE);
            }
            return;
        }
    }

    void CaptureOverlay::PaintOverlay() noexcept
    {
        PAINTSTRUCT paint{};
        HDC device_context = BeginPaint(overlay_window_, &paint);

        RECT client_rect{};
        GetClientRect(overlay_window_, &client_rect);
        RECT const paint_rect = paint.rcPaint;
        int const paint_width = paint_rect.right - paint_rect.left;
        int const paint_height = paint_rect.bottom - paint_rect.top;
        if (paint_width <= 0 || paint_height <= 0)
        {
            EndPaint(overlay_window_, &paint);
            return;
        }

        HDC buffer_device_context = CreateCompatibleDC(device_context);
        HBITMAP buffer_bitmap = buffer_device_context != nullptr
                                    ? CreateCompatibleBitmap(device_context, paint_width, paint_height)
                                    : nullptr;
        if (buffer_device_context == nullptr || buffer_bitmap == nullptr)
        {
            if (buffer_bitmap != nullptr)
            {
                DeleteObject(buffer_bitmap);
            }
            if (buffer_device_context != nullptr)
            {
                DeleteDC(buffer_device_context);
            }

            EndPaint(overlay_window_, &paint);
            return;
        }

        HGDIOBJ previous_buffer_bitmap = SelectObject(buffer_device_context, buffer_bitmap);
        RECT local_paint_rect{.left = 0, .top = 0, .right = paint_width, .bottom = paint_height};

        PaintOverlayBackgroundRect(buffer_device_context, local_paint_rect,
                                   POINT{.x = paint_rect.left, .y = paint_rect.top}, dimmed_background_,
                                   frozen_background_);

        RECT preview_rect{};
        bool has_preview_rect = false;
        COLORREF border_color = RGB(255, 215, 0);

        if (drag_in_progress_ && has_selection_)
        {
            preview_rect = CurrentSelectionRect();
            has_preview_rect = true;
            border_color = RGB(255, 255, 255);
        }
        else if (has_committed_selection_)
        {
            preview_rect = OverlayToClientRect(committed_selection_rect_);
            has_preview_rect = true;
            border_color = RGB(255, 255, 255);
        }
        else if (has_hover_window_ && !drag_in_progress_)
        {
            preview_rect = OverlayToClientRect(hover_window_rect_);
            has_preview_rect = true;
        }

        if (has_preview_rect)
        {
            RECT local_preview_rect = preview_rect;
            OffsetRect(&local_preview_rect, -paint_rect.left, -paint_rect.top);
            PaintOverlayPreviewRect(buffer_device_context, local_preview_rect, preview_rect, frozen_background_,
                                    border_color);
            RECT metrics_rect = SelectionMetricsRect(preview_rect, client_rect);
            if (IsRectNonEmpty(metrics_rect))
            {
                RECT local_metrics_rect = metrics_rect;
                OffsetRect(&local_metrics_rect, -paint_rect.left, -paint_rect.top);
                PaintSelectionMetrics(buffer_device_context, local_metrics_rect, preview_rect);
            }
            bool const should_show_resize_handles = ((drag_in_progress_ && has_selection_) ||
                                                     has_committed_selection_) &&
                                                    ShouldShowResizeHandles(preview_rect);
            if (should_show_resize_handles)
            {
                int const center_x = (local_preview_rect.left + local_preview_rect.right) / 2;
                int const center_y = (local_preview_rect.top + local_preview_rect.bottom) / 2;
                std::array<POINT, 8> const handle_points{{
                    POINT{.x = local_preview_rect.left, .y = local_preview_rect.top},
                    POINT{.x = center_x, .y = local_preview_rect.top},
                    POINT{.x = local_preview_rect.right, .y = local_preview_rect.top},
                    POINT{.x = local_preview_rect.right, .y = center_y},
                    POINT{.x = local_preview_rect.right, .y = local_preview_rect.bottom},
                    POINT{.x = center_x, .y = local_preview_rect.bottom},
                    POINT{.x = local_preview_rect.left, .y = local_preview_rect.bottom},
                    POINT{.x = local_preview_rect.left, .y = center_y},
                }};
                for (POINT const handle_point : handle_points)
                {
                    PaintResizeHandle(buffer_device_context, handle_point);
                }
            }
            if (has_committed_selection_)
            {
                RECT const toolbar_rect = ToolbarRect(preview_rect, client_rect);
                if (IsRectNonEmpty(toolbar_rect))
                {
                    RECT local_toolbar_rect = toolbar_rect;
                    OffsetRect(&local_toolbar_rect, -paint_rect.left, -paint_rect.top);
                    PaintToolbarBackground(buffer_device_context, local_toolbar_rect);

                    constexpr std::array<ToolbarAction, 4> kToolbarActions{
                        ToolbarAction::CopyAndPin,
                        ToolbarAction::CopyOnly,
                        ToolbarAction::SaveToFile,
                        ToolbarAction::Cancel,
                    };
                    for (ToolbarAction const action : kToolbarActions)
                    {
                        RECT button_rect = ToolbarButtonRect(toolbar_rect, action);
                        OffsetRect(&button_rect, -paint_rect.left, -paint_rect.top);
                        PaintToolbarButton(buffer_device_context, button_rect, ToolbarActionLabel(action));
                    }
                }
            }
        }

        RECT local_client_rect = client_rect;
        OffsetRect(&local_client_rect, -paint_rect.left, -paint_rect.top);
        SetBkMode(buffer_device_context, TRANSPARENT);
        SetTextColor(buffer_device_context, RGB(255, 255, 255));
        DrawTextW(buffer_device_context,
                  has_committed_selection_ ? kSelectionAdjustmentInstruction : kOverlayInstruction, -1,
                  &local_client_rect, DT_CENTER | DT_WORDBREAK | DT_TOP);

        (void)BitBlt(device_context, paint_rect.left, paint_rect.top, paint_width, paint_height, buffer_device_context,
                     0, 0, SRCCOPY);

        SelectObject(buffer_device_context, previous_buffer_bitmap);
        DeleteObject(buffer_bitmap);
        DeleteDC(buffer_device_context);
        EndPaint(overlay_window_, &paint);
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
            if (HandleKeyDown(w_param))
            {
                return 0;
            }
            break;

        case WM_LBUTTONDOWN:
            BeginPointerSelection(l_param);
            return 0;

        case WM_MOUSEMOVE:
            UpdatePointerSelection(l_param);
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(l_param) == HTCLIENT)
            {
                POINT cursor_position{};
                GetCursorPos(&cursor_position);
                ScreenToClient(overlay_window_, &cursor_position);
                UpdateCursorForOverlayPoint(cursor_position);
                return TRUE;
            }
            break;

        case WM_LBUTTONUP:
            CompletePointerSelection(l_param);
            return 0;

        case WM_RBUTTONUP:
        case WM_NCRBUTTONUP:
            if (has_committed_selection_)
            {
                RECT old_preview_rect{};
                bool const had_old_preview = TryGetCurrentPreviewRect(old_preview_rect);
                ResetCommittedSelection();
                POINT cursor_position{};
                GetCursorPos(&cursor_position);
                (void)UpdateHoverWindowFromScreenPoint(cursor_position);
                ScreenToClient(overlay_window_, &cursor_position);
                UpdateCursorForOverlayPoint(cursor_position);
                RECT new_preview_rect{};
                bool const had_new_preview = TryGetCurrentPreviewRect(new_preview_rect);
                InvalidatePreviewRectChange(old_preview_rect, had_old_preview, new_preview_rect, had_new_preview);
                return 0;
            }

            CAPTUREZY_LOG_DEBUG(core::LogCategory::Capture, L"Overlay cancelled by right click.");
            Finish(OverlayResult::Cancelled);
            return 0;

        case WM_PAINT:
            PaintOverlay();
            return 0;

        case WM_CAPTURECHANGED:
            pointer_down_ = false;
            drag_in_progress_ = false;
            has_click_candidate_window_ = false;
            pointer_drag_mode_ = PointerDragMode::None;
            active_resize_handle_ = ResizeHandle::None;
            resize_anchor_selection_rect_ = {};
            resize_anchor_handle_ = ResizeHandle::None;
            pressed_toolbar_action_ = ToolbarAction::None;
            return 0;

        case WM_DESTROY:
            if (GetCapture() == overlay_window_)
            {
                ReleaseCapture();
            }
            overlay_window_ = nullptr;
            return 0;

        default:
            break;
        }

        return DefWindowProcW(overlay_window_, message, w_param, l_param);
    }

    void CaptureOverlay::Finish(OverlayResult result) noexcept
    {
        CAPTUREZY_LOG_DEBUG(core::LogCategory::Capture, result == OverlayResult::Cancelled
                                                            ? L"Overlay finishing with cancellation."
                                                            : L"Overlay finishing with capture.");
        if (result != OverlayResult::Cancelled)
        {
            final_capture_result_ = FrozenSelectionResult();
        }

        if (owner_window_ != nullptr)
        {
            PostMessageW(owner_window_, ResultMessage(), static_cast<WPARAM>(result), 0);
        }

        Close();
    }

    // `WindowProc` 需要保持 Win32 约定签名，这里对 clang-tidy 的误报做局部抑制。
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
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
