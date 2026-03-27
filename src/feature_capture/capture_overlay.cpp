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
        [[maybe_unused]] constexpr wchar_t const *kUnusedSelectionAdjustmentInstruction =
            L"已选择区域：拖动边框可调大小，拖动区域内部可移动；P 贴图，Ctrl+C 复制，Ctrl+S 保存，右键重置，Esc 取消";
        constexpr wchar_t const *kSelectionAdjustmentInstructionCommitted =
            L"已选择区域：拖动边框可调大小，拖动区域内部可移动；双击或 Enter 复制，"
            L"中键贴图，P 贴图，Ctrl+C 复制，Ctrl+S 保存，右键重置，Esc 取消";
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
        constexpr int kToolbarToolButtonWidth = 34;
        constexpr int kToolbarResultButtonWidth = 62;
        constexpr int kToolbarButtonHeight = 34;
        constexpr int kToolbarButtonCornerRadius = 8;
        constexpr int kToolbarSpacing = 6;
        constexpr int kToolbarSectionSpacing = 14;
        constexpr int kToolbarPadding = 7;
        constexpr int kToolbarMargin = 12;
        constexpr int kToolbarCornerRadius = 12;
        constexpr int kDebugOverlayMargin = 16;
        constexpr int kDebugOverlayPadding = 10;
        constexpr int kDebugOverlayMaxWidth = 520;
        // 当前先使用硬编码的确认手势默认值，后续截图设置页需要把每个手势独立开放，
        // 并支持持久化用户选择的动作映射。
        constexpr OverlayResult kEnterConfirmResult = OverlayResult::CopyOnly;
        constexpr OverlayResult kDoubleClickConfirmResult = OverlayResult::CopyOnly;
        constexpr OverlayResult kMiddleClickConfirmResult = OverlayResult::CopyAndPin;

        enum class ToolbarButtonVisualState : std::uint8_t
        {
            Normal,
            Hovered,
            Pressed,
            Placeholder,
        };

        void PaintToolbarSeparator(HDC destination_device_context, RECT toolbar_rect, int separator_x) noexcept
        {
            HPEN separator_pen = CreatePen(PS_SOLID, 1, RGB(78, 78, 78));
            HGDIOBJ old_pen = SelectObject(destination_device_context, separator_pen);
            int const top = toolbar_rect.top + kToolbarPadding + 2;
            int const bottom = toolbar_rect.bottom - kToolbarPadding - 2;
            MoveToEx(destination_device_context, separator_x, top, nullptr);
            LineTo(destination_device_context, separator_x, bottom);
            SelectObject(destination_device_context, old_pen);
            DeleteObject(separator_pen);
        }

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

        [[nodiscard]] bool IsOverflowTrayWindow(HWND window) noexcept
        {
            return WindowClassNameEquals(window, L"NotifyIconOverflowWindow") ||
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

            // Maximized windows already map to the visible work area bounds.
            // Trimming the DWM-reported frame border here shrinks them by 1px on
            // each edge, which is why they end up smaller than Snipaste.
            if (IsZoomed(window) != FALSE)
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
            HPEN frame_pen = CreatePen(PS_SOLID, 1, RGB(240, 240, 240));
            HBRUSH background_brush = CreateSolidBrush(RGB(10, 10, 10));
            HGDIOBJ old_pen = SelectObject(destination_device_context, frame_pen);
            HGDIOBJ old_brush = SelectObject(destination_device_context, background_brush);
            RoundRect(destination_device_context, destination_rect.left, destination_rect.top, destination_rect.right,
                      destination_rect.bottom, kToolbarCornerRadius, kToolbarCornerRadius);
            SelectObject(destination_device_context, old_brush);
            SelectObject(destination_device_context, old_pen);
            DeleteObject(background_brush);
            DeleteObject(frame_pen);

            RECT inner_rect = destination_rect;
            InflateRect(&inner_rect, -1, -1);
            if (IsRectNonEmpty(inner_rect))
            {
                HPEN inner_pen = CreatePen(PS_SOLID, 1, RGB(46, 46, 46));
                HBRUSH inner_brush = CreateSolidBrush(RGB(22, 22, 22));
                HGDIOBJ old_inner_pen = SelectObject(destination_device_context, inner_pen);
                HGDIOBJ old_inner_brush = SelectObject(destination_device_context, inner_brush);
                RoundRect(destination_device_context, inner_rect.left, inner_rect.top, inner_rect.right,
                          inner_rect.bottom, kToolbarCornerRadius, kToolbarCornerRadius);
                SelectObject(destination_device_context, old_inner_brush);
                SelectObject(destination_device_context, old_inner_pen);
                DeleteObject(inner_brush);
                DeleteObject(inner_pen);
            }
        }

        void PaintToolbarButton(HDC destination_device_context, RECT destination_rect, wchar_t const *label,
                                ToolbarButtonVisualState visual_state) noexcept
        {
            COLORREF frame_color = RGB(92, 92, 92);
            COLORREF background_color = RGB(36, 36, 36);
            COLORREF text_color = RGB(244, 244, 244);
            if (visual_state == ToolbarButtonVisualState::Placeholder)
            {
                frame_color = RGB(58, 58, 58);
                background_color = RGB(28, 28, 28);
                text_color = RGB(126, 126, 126);
            }
            else if (visual_state == ToolbarButtonVisualState::Hovered)
            {
                frame_color = RGB(255, 214, 102);
                background_color = RGB(84, 62, 18);
            }
            else if (visual_state == ToolbarButtonVisualState::Pressed)
            {
                frame_color = RGB(255, 236, 170);
                background_color = RGB(255, 214, 102);
                text_color = RGB(16, 16, 16);
            }

            HPEN frame_pen = CreatePen(PS_SOLID, 1, frame_color);
            HBRUSH background_brush = CreateSolidBrush(background_color);
            HGDIOBJ old_pen = SelectObject(destination_device_context, frame_pen);
            HGDIOBJ old_brush = SelectObject(destination_device_context, background_brush);
            RoundRect(destination_device_context, destination_rect.left, destination_rect.top, destination_rect.right,
                      destination_rect.bottom, kToolbarButtonCornerRadius, kToolbarButtonCornerRadius);
            SelectObject(destination_device_context, old_brush);
            SelectObject(destination_device_context, old_pen);
            DeleteObject(background_brush);
            DeleteObject(frame_pen);

            RECT text_rect = destination_rect;
            InflateRect(&text_rect, -4, -2);
            SetBkMode(destination_device_context, TRANSPARENT);
            SetTextColor(destination_device_context, text_color);
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

        // Win32 选区命中需要串起顶层窗口、popup root 与桌面回退，主流程保留在一处更容易核对边界顺序。
        // NOLINTNEXTLINE(readability-function-cognitive-complexity)
        [[nodiscard]] bool FindTopLevelWindowRectAtPoint(HWND excluded_window, POINT screen_point, RECT &window_rect,
                                                         HWND &matched_window) noexcept
        {
            RECT const virtual_screen_rect = VirtualScreenRect();
            RECT desktop_shell_fallback_rect{};
            bool has_desktop_shell_fallback = false;
            matched_window = nullptr;
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
                                matched_window = candidate_window;
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
                matched_window = window;
                return true;
            }

            if (has_desktop_shell_fallback)
            {
                window_rect = desktop_shell_fallback_rect;
                matched_window = nullptr;
                return true;
            }

            return false;
        }

        [[nodiscard]] std::wstring ProcessBaseName(DWORD process_id)
        {
            if (process_id == 0)
            {
                return L"(none)";
            }

            HANDLE process_handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
            if (process_handle == nullptr)
            {
                return L"(open failed)";
            }

            std::array<wchar_t, 1024> process_path{};
            auto process_path_length = static_cast<DWORD>(process_path.size());
            bool const queried = QueryFullProcessImageNameW(process_handle, 0, process_path.data(),
                                                            &process_path_length) != FALSE;
            CloseHandle(process_handle);
            if (!queried || process_path_length == 0)
            {
                return L"(query failed)";
            }

            std::wstring full_path(process_path.data(), process_path_length);
            std::size_t const separator = full_path.find_last_of(L"\\/");
            return separator == std::wstring::npos ? full_path : full_path.substr(separator + 1);
        }

        [[nodiscard]] std::wstring WindowTextOrPlaceholder(HWND window)
        {
            std::array<wchar_t, 512> text{};
            int const text_length = GetWindowTextW(window, text.data(), static_cast<int>(text.size()));
            return text_length > 0 ? std::wstring(text.data(), text_length) : L"(empty)";
        }

        [[nodiscard]] std::wstring ClassNameOrPlaceholder(HWND window)
        {
            std::array<wchar_t, 256> class_name{};
            int const class_name_length = GetClassNameW(window, class_name.data(), static_cast<int>(class_name.size()));
            return class_name_length > 0 ? std::wstring(class_name.data(), class_name_length) : L"(unknown)";
        }

        [[nodiscard]] std::wstring HexValue(std::uintptr_t value)
        {
            constexpr std::array<wchar_t, 16> kHexDigits{
                L'0', L'1', L'2', L'3', L'4', L'5', L'6', L'7', L'8', L'9', L'A', L'B', L'C', L'D', L'E', L'F',
            };

            std::wstring hex_text = L"0x";
            bool has_non_zero_digit = false;
            for (int shift = static_cast<int>((sizeof(std::uintptr_t) * 8U) - 4U); shift >= 0; shift -= 4)
            {
                std::uintptr_t const nibble = (value >> shift) & 0xFU;
                if (!has_non_zero_digit && nibble == 0 && shift > 0)
                {
                    continue;
                }

                has_non_zero_digit = true;
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
                hex_text.push_back(kHexDigits[static_cast<std::size_t>(nibble)]);
            }

            if (!has_non_zero_digit)
            {
                hex_text.push_back(L'0');
            }

            return hex_text;
        }

        [[nodiscard]] std::wstring BuildHoverDebugText(HWND window, RECT rect)
        {
            std::wstring debug_text;
            debug_text += L"[Capture Debug]\n";
            debug_text += L"F2: toggle HUD | Ctrl+F2: copy\n";
            debug_text += L"Rect: ";
            debug_text += std::to_wstring(rect.left);
            debug_text += L",";
            debug_text += std::to_wstring(rect.top);
            debug_text += L" - ";
            debug_text += std::to_wstring(rect.right);
            debug_text += L",";
            debug_text += std::to_wstring(rect.bottom);
            debug_text += L"\n";
            debug_text += L"Size: ";
            debug_text += std::to_wstring(std::max<LONG>(0, rect.right - rect.left));
            debug_text += L" x ";
            debug_text += std::to_wstring(std::max<LONG>(0, rect.bottom - rect.top));

            if (window == nullptr)
            {
                debug_text += L"\nSource: desktop fallback";
                return debug_text;
            }

            DWORD process_id = 0;
            GetWindowThreadProcessId(window, &process_id);
            LONG_PTR const style = GetWindowLongPtrW(window, GWL_STYLE);
            LONG_PTR const ex_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            auto const window_value = reinterpret_cast<std::uintptr_t>(window);
            debug_text += L"\nHWND: ";
            debug_text += HexValue(window_value);
            debug_text += L"\nClass: ";
            debug_text += ClassNameOrPlaceholder(window);
            debug_text += L"\nTitle: ";
            debug_text += WindowTextOrPlaceholder(window);
            debug_text += L"\nPID: ";
            debug_text += std::to_wstring(process_id);
            debug_text += L"\nProcess: ";
            debug_text += ProcessBaseName(process_id);
            debug_text += L"\nStyle: ";
            debug_text += HexValue(static_cast<std::uintptr_t>(style));
            debug_text += L"\nExStyle: ";
            debug_text += HexValue(static_cast<std::uintptr_t>(ex_style));
            return debug_text;
        }

        [[nodiscard]] bool CopyTextToClipboard(HWND owner_window, std::wstring const &text) noexcept
        {
            if (OpenClipboard(owner_window) == FALSE)
            {
                CAPTUREZY_LOG_ERROR(core::LogCategory::Clipboard, L"OpenClipboard failed for debug text copy.");
                return false;
            }

            if (EmptyClipboard() == FALSE)
            {
                CAPTUREZY_LOG_ERROR(core::LogCategory::Clipboard, L"EmptyClipboard failed for debug text copy.");
                CloseClipboard();
                return false;
            }

            std::size_t const byte_count = (text.size() + 1) * sizeof(wchar_t);
            HGLOBAL const clipboard_memory = GlobalAlloc(GMEM_MOVEABLE, byte_count);
            if (clipboard_memory == nullptr)
            {
                CAPTUREZY_LOG_ERROR(core::LogCategory::Clipboard, L"GlobalAlloc failed for debug text copy.");
                CloseClipboard();
                return false;
            }

            void *locked_memory = GlobalLock(clipboard_memory);
            if (locked_memory == nullptr)
            {
                CAPTUREZY_LOG_ERROR(core::LogCategory::Clipboard, L"GlobalLock failed for debug text copy.");
                GlobalFree(clipboard_memory);
                CloseClipboard();
                return false;
            }

            std::copy_n(text.c_str(), text.size() + 1, static_cast<wchar_t *>(locked_memory));
            GlobalUnlock(clipboard_memory);

            if (SetClipboardData(CF_UNICODETEXT, clipboard_memory) == nullptr)
            {
                CAPTUREZY_LOG_ERROR(core::LogCategory::Clipboard, L"SetClipboardData failed for debug text copy.");
                GlobalFree(clipboard_memory);
                CloseClipboard();
                return false;
            }

            CloseClipboard();
            CAPTUREZY_LOG_INFO(core::LogCategory::Clipboard, L"Copied capture debug text to clipboard.");
            return true;
        }

        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        void PaintDebugOverlay(HDC destination_device_context, RECT client_rect, RECT paint_rect,
                               std::wstring const &debug_text) noexcept
        {
            if (debug_text.empty())
            {
                return;
            }

            LONG const max_text_right = std::min(client_rect.right - static_cast<LONG>(kDebugOverlayMargin),
                                                 static_cast<LONG>(kDebugOverlayMargin + kDebugOverlayMaxWidth));
            LONG const max_text_bottom = client_rect.bottom - static_cast<LONG>(kDebugOverlayMargin);
            if (max_text_right <= kDebugOverlayMargin || max_text_bottom <= kDebugOverlayMargin)
            {
                return;
            }

            RECT text_rect{
                .left = kDebugOverlayMargin,
                .top = kDebugOverlayMargin,
                .right = max_text_right,
                .bottom = max_text_bottom,
            };
            DrawTextW(destination_device_context, debug_text.c_str(), -1, &text_rect,
                      DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
            InflateRect(&text_rect, kDebugOverlayPadding, kDebugOverlayPadding);

            RECT clipped_rect{};
            if (IntersectRect(&clipped_rect, &text_rect, &paint_rect) == FALSE)
            {
                return;
            }

            HBRUSH background_brush = CreateSolidBrush(RGB(0, 0, 0));
            HPEN frame_pen = CreatePen(PS_SOLID, 1, RGB(0, 255, 170));
            HGDIOBJ old_brush = SelectObject(destination_device_context, background_brush);
            HGDIOBJ old_pen = SelectObject(destination_device_context, frame_pen);
            RoundRect(destination_device_context, text_rect.left, text_rect.top, text_rect.right, text_rect.bottom, 8,
                      8);
            SelectObject(destination_device_context, old_pen);
            SelectObject(destination_device_context, old_brush);
            DeleteObject(frame_pen);
            DeleteObject(background_brush);

            RECT content_rect = text_rect;
            InflateRect(&content_rect, -kDebugOverlayPadding, -kDebugOverlayPadding);
            SetBkMode(destination_device_context, TRANSPARENT);
            SetTextColor(destination_device_context, RGB(255, 255, 255));
            DrawTextW(destination_device_context, debug_text.c_str(), -1, &content_rect,
                      DT_LEFT | DT_TOP | DT_WORDBREAK);
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
        cached_overflow_tray_rect_ = {};
        click_candidate_window_rect_ = {};
        committed_selection_rect_ = {};
        resize_anchor_selection_rect_ = {};
        resize_anchor_handle_ = ResizeHandle::None;
        pointer_down_ = false;
        drag_in_progress_ = false;
        has_selection_ = false;
        has_hover_window_ = false;
        has_cached_overflow_tray_ = false;
        has_click_candidate_window_ = false;
        has_committed_selection_ = false;
        hover_debug_text_.clear();
        cached_overflow_tray_debug_text_.clear();
        pointer_drag_mode_ = PointerDragMode::None;
        active_resize_handle_ = ResizeHandle::None;
        hovered_toolbar_action_ = ToolbarAction::None;
        pressed_toolbar_action_ = ToolbarAction::None;

        if (overlay_window_ != nullptr)
        {
            ShowWindow(overlay_window_, SW_SHOW);
            SetForegroundWindow(overlay_window_);
            SetFocus(overlay_window_);
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
        cached_overflow_tray_rect_ = {};
        has_cached_overflow_tray_ = false;
        hover_debug_text_.clear();
        cached_overflow_tray_debug_text_.clear();
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

    CaptureOverlay::ToolbarActionSpec const &CaptureOverlay::ToolbarActionMetadata(ToolbarAction action) noexcept
    {
        static constexpr std::array<ToolbarActionSpec, 10> kToolbarActionSpecs{{
            ToolbarActionSpec{.action = ToolbarAction::PlaceholderArrow,
                              .label = L"箭",
                              .hint = L"箭头工具（暂未开放）",
                              .group = 0,
                              .index_in_group = 0,
                              .width = kToolbarToolButtonWidth,
                              .interactive = false},
            ToolbarActionSpec{.action = ToolbarAction::PlaceholderPen,
                              .label = L"笔",
                              .hint = L"画笔工具（暂未开放）",
                              .group = 0,
                              .index_in_group = 1,
                              .width = kToolbarToolButtonWidth,
                              .interactive = false},
            ToolbarActionSpec{.action = ToolbarAction::PlaceholderText,
                              .label = L"文",
                              .hint = L"文字工具（暂未开放）",
                              .group = 0,
                              .index_in_group = 2,
                              .width = kToolbarToolButtonWidth,
                              .interactive = false},
            ToolbarActionSpec{.action = ToolbarAction::PlaceholderMosaic,
                              .label = L"码",
                              .hint = L"马赛克工具（暂未开放）",
                              .group = 0,
                              .index_in_group = 3,
                              .width = kToolbarToolButtonWidth,
                              .interactive = false},
            ToolbarActionSpec{.action = ToolbarAction::PlaceholderUndo,
                              .label = L"撤",
                              .hint = L"撤销（暂未开放）",
                              .group = 1,
                              .index_in_group = 0,
                              .width = kToolbarToolButtonWidth,
                              .interactive = false},
            ToolbarActionSpec{.action = ToolbarAction::PlaceholderRedo,
                              .label = L"重",
                              .hint = L"重做（暂未开放）",
                              .group = 1,
                              .index_in_group = 1,
                              .width = kToolbarToolButtonWidth,
                              .interactive = false},
            ToolbarActionSpec{.action = ToolbarAction::Cancel,
                              .label = L"取消",
                              .hint = L"退出截图",
                              .group = 2,
                              .index_in_group = 0,
                              .width = kToolbarResultButtonWidth,
                              .interactive = true},
            ToolbarActionSpec{.action = ToolbarAction::CopyAndPin,
                              .label = L"贴图",
                              .hint = L"复制并贴图",
                              .group = 2,
                              .index_in_group = 1,
                              .width = kToolbarResultButtonWidth,
                              .interactive = true},
            ToolbarActionSpec{.action = ToolbarAction::SaveToFile,
                              .label = L"快存",
                              .hint = L"保存到默认位置",
                              .group = 2,
                              .index_in_group = 2,
                              .width = kToolbarResultButtonWidth,
                              .interactive = true},
            ToolbarActionSpec{.action = ToolbarAction::CopyOnly,
                              .label = L"复制",
                              .hint = L"复制到剪贴板",
                              .group = 2,
                              .index_in_group = 3,
                              .width = kToolbarResultButtonWidth,
                              .interactive = true},
        }};

        for (ToolbarActionSpec const &spec : kToolbarActionSpecs)
        {
            if (spec.action == action)
            {
                return spec;
            }
        }

        static constexpr ToolbarActionSpec kFallbackSpec{.action = ToolbarAction::None,
                                                         .label = L"",
                                                         .hint = L"",
                                                         .group = -1,
                                                         .index_in_group = -1,
                                                         .width = 0,
                                                         .interactive = false};
        return kFallbackSpec;
    }

    wchar_t const *CaptureOverlay::ToolbarActionLabel(ToolbarAction action) noexcept
    {
        return ToolbarActionMetadata(action).label;
    }

    wchar_t const *CaptureOverlay::ToolbarActionHint(ToolbarAction action) noexcept
    {
        return ToolbarActionMetadata(action).hint;
    }

    bool CaptureOverlay::IsInteractiveToolbarAction(ToolbarAction action) noexcept
    {
        return ToolbarActionMetadata(action).interactive;
    }

    int CaptureOverlay::ToolbarActionGroup(ToolbarAction action) noexcept
    {
        return ToolbarActionMetadata(action).group;
    }

    int CaptureOverlay::ToolbarActionIndexInGroup(ToolbarAction action) noexcept
    {
        return ToolbarActionMetadata(action).index_in_group;
    }

    int CaptureOverlay::ToolbarButtonWidth(ToolbarAction action) noexcept
    {
        return ToolbarActionMetadata(action).width;
    }

    int CaptureOverlay::ToolbarGroupActionCount(int group) noexcept
    {
        switch (group)
        {
        case 0:
            return 4;

        case 1:
            return 2;

        case 2:
            return 4;

        default:
            return 0;
        }
    }

    int CaptureOverlay::ToolbarGroupWidth(int group) noexcept
    {
        int const group_action_count = ToolbarGroupActionCount(group);
        if (group_action_count <= 0)
        {
            return 0;
        }

        int const sample_button_width = [&]() noexcept {
            switch (group)
            {
            case 0:
            case 1:
                return kToolbarToolButtonWidth;

            case 2:
                return kToolbarResultButtonWidth;

            default:
                return 0;
            }
        }();
        if (sample_button_width <= 0)
        {
            return 0;
        }

        return (sample_button_width * group_action_count) + (kToolbarSpacing * (group_action_count - 1));
    }

    RECT CaptureOverlay::ToolbarRect(RECT selection_rect, RECT bounds_rect) const noexcept
    {
        if (!has_committed_selection_ || !IsRectNonEmpty(selection_rect))
        {
            return {};
        }

        int const placeholder_group_width = ToolbarGroupWidth(0);
        int const history_group_width = ToolbarGroupWidth(1);
        int const result_group_width = ToolbarGroupWidth(2);
        int const toolbar_width = placeholder_group_width + history_group_width + result_group_width +
                                  (kToolbarSectionSpacing * 2) + (kToolbarPadding * 2);
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

        int const action_group = ToolbarActionGroup(action);
        int const action_index = ToolbarActionIndexInGroup(action);
        int const button_width = ToolbarButtonWidth(action);
        if (action_group < 0 || action_index < 0 || button_width <= 0)
        {
            return {};
        }

        int const group_left = [&]() noexcept {
            switch (action_group)
            {
            case 0:
                return toolbar_rect.left + kToolbarPadding;

            case 1:
                return toolbar_rect.left + kToolbarPadding + ToolbarGroupWidth(0) + kToolbarSectionSpacing;

            case 2:
                return toolbar_rect.left + kToolbarPadding + ToolbarGroupWidth(0) + kToolbarSectionSpacing +
                       ToolbarGroupWidth(1) + kToolbarSectionSpacing;

            default:
                return toolbar_rect.left;
            }
        }();

        RECT button_rect{
            .left = group_left + (action_index * (button_width + kToolbarSpacing)),
            .top = toolbar_rect.top + kToolbarPadding,
            .right = group_left + (action_index * (button_width + kToolbarSpacing)) + button_width,
            .bottom = toolbar_rect.top + kToolbarPadding + kToolbarButtonHeight,
        };
        return button_rect;
    }

    bool CaptureOverlay::IsPointInsideToolbar(POINT overlay_point) const noexcept
    {
        if (!has_committed_selection_)
        {
            return false;
        }

        RECT const toolbar_rect = CurrentToolbarRect();
        return IsRectNonEmpty(toolbar_rect) && PtInRect(&toolbar_rect, overlay_point) != FALSE;
    }

    CaptureOverlay::ToolbarAction CaptureOverlay::HitTestToolbarAction(POINT overlay_point) const noexcept
    {
        if (!IsPointInsideToolbar(overlay_point))
        {
            return ToolbarAction::None;
        }

        constexpr std::array<ToolbarAction, 4> kToolbarActions{
            ToolbarAction::Cancel,
            ToolbarAction::CopyAndPin,
            ToolbarAction::SaveToFile,
            ToolbarAction::CopyOnly,
        };
        RECT const toolbar_rect = CurrentToolbarRect();
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

    RECT CaptureOverlay::CurrentToolbarRect() const noexcept
    {
        if (!has_committed_selection_ || overlay_window_ == nullptr)
        {
            return {};
        }

        RECT client_rect{};
        GetClientRect(overlay_window_, &client_rect);
        return ToolbarRect(OverlayToClientRect(committed_selection_rect_), client_rect);
    }

    void CaptureOverlay::InvalidateToolbarVisual() noexcept
    {
        if (overlay_window_ == nullptr)
        {
            return;
        }

        RECT const toolbar_rect = ExpandedRect(CurrentToolbarRect(), 2);
        if (IsRectNonEmpty(toolbar_rect))
        {
            InvalidateRect(overlay_window_, &toolbar_rect, FALSE);
        }
    }

    void CaptureOverlay::UpdateHoveredToolbarAction(POINT overlay_point) noexcept
    {
        ToolbarAction const hovered_action = has_committed_selection_ ? HitTestToolbarAction(overlay_point)
                                                                      : ToolbarAction::None;
        if (hovered_toolbar_action_ == hovered_action)
        {
            return;
        }

        hovered_toolbar_action_ = hovered_action;
        InvalidateToolbarVisual();
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

    bool CaptureOverlay::UpdateHoverWindowFromScreenPoint(POINT screen_point)
    {
        RECT window_rect{};
        HWND matched_window = nullptr;
        bool const found = FindTopLevelWindowRectAtPoint(overlay_window_, screen_point, window_rect, matched_window);
        bool resolved_found = found;
        RECT resolved_rect = window_rect;
        std::wstring new_hover_debug_text = found ? BuildHoverDebugText(matched_window, window_rect)
                                                  : L"(no hover window)";
        if (found && matched_window != nullptr && IsOverflowTrayWindow(matched_window))
        {
            cached_overflow_tray_rect_ = window_rect;
            cached_overflow_tray_debug_text_ = new_hover_debug_text;
            has_cached_overflow_tray_ = true;
        }
        else if (has_cached_overflow_tray_ && PtInRect(&cached_overflow_tray_rect_, screen_point) != FALSE)
        {
            resolved_found = true;
            resolved_rect = cached_overflow_tray_rect_;
            new_hover_debug_text = cached_overflow_tray_debug_text_;
            new_hover_debug_text += L"\nSource: cached overflow tray";
        }

        bool const changed = resolved_found != has_hover_window_ ||
                             (resolved_found && EqualRect(&resolved_rect, &hover_window_rect_) == FALSE) ||
                             new_hover_debug_text != hover_debug_text_;
        has_hover_window_ = resolved_found;
        hover_window_rect_ = resolved_found ? resolved_rect : RECT{};
        hover_debug_text_ = std::move(new_hover_debug_text);
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
            ToolbarAction const toolbar_action = HitTestToolbarAction(overlay_point);
            if (toolbar_action != ToolbarAction::None)
            {
                active_resize_handle_ = ResizeHandle::None;
                SetCursor(ToolbarCursor());
                return;
            }

            if (IsPointInsideToolbar(overlay_point))
            {
                active_resize_handle_ = ResizeHandle::None;
                SetCursor(CursorForResizeHandle(ResizeHandle::None));
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
        hovered_toolbar_action_ = ToolbarAction::None;
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
        hovered_toolbar_action_ = ToolbarAction::None;
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
        hovered_toolbar_action_ = ToolbarAction::None;
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

        if (w_param == VK_F2)
        {
            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
            {
                return CopyTextToClipboard(overlay_window_, hover_debug_text_);
            }

            debug_overlay_enabled_ = !debug_overlay_enabled_;
            InvalidateRect(overlay_window_, nullptr, FALSE);
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

        if (w_param == VK_RETURN)
        {
            FinishCommittedSelection(kEnterConfirmResult);
            return true;
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
            hovered_toolbar_action_ = toolbar_action;
            pressed_toolbar_action_ = toolbar_action;
            SetCapture(overlay_window_);
            InvalidateToolbarVisual();
            return;
        }
        if (IsPointInsideToolbar(overlay_point))
        {
            active_resize_handle_ = ResizeHandle::None;
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
            UpdateHoveredToolbarAction(overlay_point);
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
            UpdateHoveredToolbarAction(overlay_point);
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
            if (debug_overlay_enabled_)
            {
                InvalidateRect(overlay_window_, nullptr, FALSE);
            }
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
        hovered_toolbar_action_ = ToolbarAction::None;
        pressed_toolbar_action_ = ToolbarAction::None;
        if (pressed_toolbar_action != ToolbarAction::None)
        {
            InvalidateToolbarVisual();
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
                    Finish(OverlayResult::Cancelled);
                    return;

                case ToolbarAction::None:
                default:
                    break;
                }
            }

            UpdateHoveredToolbarAction(drag_current_);
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

    // 绘制流程要按背景、预览、控制点、工具条、指引文案与 HUD 固定顺序叠加，暂时保留在单入口。
    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
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

                    constexpr std::array<ToolbarAction, 4> kPlaceholderActions{
                        ToolbarAction::PlaceholderArrow,
                        ToolbarAction::PlaceholderPen,
                        ToolbarAction::PlaceholderText,
                        ToolbarAction::PlaceholderMosaic,
                    };
                    constexpr std::array<ToolbarAction, 2> kHistoryActions{
                        ToolbarAction::PlaceholderUndo,
                        ToolbarAction::PlaceholderRedo,
                    };
                    constexpr std::array<ToolbarAction, 4> kResultActions{
                        ToolbarAction::Cancel,
                        ToolbarAction::CopyAndPin,
                        ToolbarAction::SaveToFile,
                        ToolbarAction::CopyOnly,
                    };

                    RECT const left_group_last_button = ToolbarButtonRect(toolbar_rect,
                                                                          ToolbarAction::PlaceholderMosaic);
                    RECT const middle_group_first_button = ToolbarButtonRect(toolbar_rect,
                                                                             ToolbarAction::PlaceholderUndo);
                    RECT const middle_group_last_button = ToolbarButtonRect(toolbar_rect,
                                                                            ToolbarAction::PlaceholderRedo);
                    RECT const right_group_first_button = ToolbarButtonRect(toolbar_rect, ToolbarAction::Cancel);
                    if (IsRectNonEmpty(left_group_last_button) && IsRectNonEmpty(middle_group_first_button))
                    {
                        int const separator_x = (left_group_last_button.right + middle_group_first_button.left) / 2;
                        PaintToolbarSeparator(buffer_device_context, local_toolbar_rect, separator_x - paint_rect.left);
                    }
                    if (IsRectNonEmpty(middle_group_last_button) && IsRectNonEmpty(right_group_first_button))
                    {
                        int const separator_x = (middle_group_last_button.right + right_group_first_button.left) / 2;
                        PaintToolbarSeparator(buffer_device_context, local_toolbar_rect, separator_x - paint_rect.left);
                    }

                    for (ToolbarAction const action : kPlaceholderActions)
                    {
                        RECT button_rect = ToolbarButtonRect(toolbar_rect, action);
                        OffsetRect(&button_rect, -paint_rect.left, -paint_rect.top);
                        PaintToolbarButton(buffer_device_context, button_rect, ToolbarActionLabel(action),
                                           ToolbarButtonVisualState::Placeholder);
                    }

                    for (ToolbarAction const action : kHistoryActions)
                    {
                        RECT button_rect = ToolbarButtonRect(toolbar_rect, action);
                        OffsetRect(&button_rect, -paint_rect.left, -paint_rect.top);
                        PaintToolbarButton(buffer_device_context, button_rect, ToolbarActionLabel(action),
                                           ToolbarButtonVisualState::Placeholder);
                    }

                    for (ToolbarAction const action : kResultActions)
                    {
                        RECT button_rect = ToolbarButtonRect(toolbar_rect, action);
                        OffsetRect(&button_rect, -paint_rect.left, -paint_rect.top);
                        ToolbarButtonVisualState visual_state = ToolbarButtonVisualState::Normal;
                        if (pointer_down_ && pressed_toolbar_action_ == action && hovered_toolbar_action_ == action)
                        {
                            visual_state = ToolbarButtonVisualState::Pressed;
                        }
                        else if (hovered_toolbar_action_ == action)
                        {
                            visual_state = ToolbarButtonVisualState::Hovered;
                        }

                        PaintToolbarButton(buffer_device_context, button_rect, ToolbarActionLabel(action),
                                           visual_state);
                    }
                }
            }
        }

        RECT local_client_rect = client_rect;
        OffsetRect(&local_client_rect, -paint_rect.left, -paint_rect.top);
        SetBkMode(buffer_device_context, TRANSPARENT);
        SetTextColor(buffer_device_context, RGB(255, 255, 255));
        DrawTextW(buffer_device_context,
                  has_committed_selection_ ? kSelectionAdjustmentInstructionCommitted : kOverlayInstruction, -1,
                  &local_client_rect, DT_CENTER | DT_WORDBREAK | DT_TOP);
        if (debug_overlay_enabled_)
        {
            PaintDebugOverlay(buffer_device_context, client_rect, paint_rect, hover_debug_text_);
        }

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
        window_class.style = CS_DBLCLKS;
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

        case WM_LBUTTONDBLCLK:
            if (has_committed_selection_)
            {
                POINT const overlay_point{.x = GET_X_LPARAM(l_param), .y = GET_Y_LPARAM(l_param)};
                if (HitTestToolbarAction(overlay_point) == ToolbarAction::None &&
                    IsPointInsideCommittedSelection(overlay_point))
                {
                    FinishCommittedSelection(kDoubleClickConfirmResult);
                    return 0;
                }
            }
            break;

        case WM_MBUTTONUP:
            if (has_committed_selection_)
            {
                POINT const overlay_point{.x = GET_X_LPARAM(l_param), .y = GET_Y_LPARAM(l_param)};
                if (HitTestToolbarAction(overlay_point) == ToolbarAction::None &&
                    IsPointInsideCommittedSelection(overlay_point))
                {
                    FinishCommittedSelection(kMiddleClickConfirmResult);
                    return 0;
                }
            }
            break;

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
            hovered_toolbar_action_ = ToolbarAction::None;
            pressed_toolbar_action_ = ToolbarAction::None;
            InvalidateToolbarVisual();
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
