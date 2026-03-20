#include "feature_capture/capture_overlay.h"

#include <algorithm>
#include <array>

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
            L"已选择区域：Enter 确认截图；左键单击区域内确认，重新拖拽可改选区，右键重置，Esc 取消";
        constexpr int kDragThreshold = 4;

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
                   WindowClassNameEquals(window, L"NotifyIconOverflowWindow");
        }

        [[nodiscard]] bool IsExcludedShellWindow(HWND window) noexcept
        {
            return WindowClassNameEquals(window, L"Progman") || WindowClassNameEquals(window, L"WorkerW");
        }

        [[nodiscard]] bool IsWindowCloaked(HWND window) noexcept
        {
            DWORD cloaked = 0;
            return SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
        }

        [[nodiscard]] bool TryGetWindowRectForSelection(HWND window, RECT &window_rect) noexcept
        {
            HRESULT const dwm_result = DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &window_rect,
                                                             sizeof(window_rect));
            if (SUCCEEDED(dwm_result) && IsRectNonEmpty(window_rect))
            {
                return true;
            }

            return GetWindowRect(window, &window_rect) != FALSE && IsRectNonEmpty(window_rect);
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

        [[nodiscard]] bool ShouldSkipWindowForSelection(HWND window, HWND excluded_window) noexcept
        {
            if (window == nullptr || window == excluded_window || window == GetDesktopWindow())
            {
                return true;
            }

            if (IsWindowVisible(window) == FALSE || IsIconic(window) != FALSE || IsWindowCloaked(window))
            {
                return true;
            }

            LONG_PTR const ex_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
            if ((ex_style & WS_EX_TOOLWINDOW) != 0 && !IsTaskbarShellWindow(window))
            {
                return true;
            }

            return IsExcludedShellWindow(window);
        }

        [[nodiscard]] bool FindTopLevelWindowRectAtPoint(HWND excluded_window, POINT screen_point,
                                                         RECT &window_rect) noexcept
        {
            RECT const virtual_screen_rect = VirtualScreenRect();
            for (HWND window = GetTopWindow(nullptr); window != nullptr; window = GetWindow(window, GW_HWNDNEXT))
            {
                if (ShouldSkipWindowForSelection(window, excluded_window))
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
                if (candidate_window != window && !ShouldSkipWindowForSelection(candidate_window, excluded_window))
                {
                    RECT candidate_rect{};
                    if (TryGetClippedWindowRectForSelection(candidate_window, virtual_screen_rect, candidate_rect) &&
                        PtInRect(&candidate_rect, screen_point) != FALSE)
                    {
                        window_rect = candidate_rect;
                        return true;
                    }
                }

                window_rect = direct_rect;
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
        pointer_down_ = false;
        drag_in_progress_ = false;
        has_selection_ = false;
        has_hover_window_ = false;
        has_click_candidate_window_ = false;
        has_committed_selection_ = false;
        confirm_selection_on_click_ = false;

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

    bool CaptureOverlay::IsPointInsideCommittedSelection(POINT overlay_point) const noexcept
    {
        if (!has_committed_selection_)
        {
            return false;
        }

        RECT selection_rect = OverlayToClientRect(committed_selection_rect_);
        return PtInRect(&selection_rect, overlay_point) != FALSE;
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

        RECT invalid_rect{};
        if (!TryUnionRect(had_old_preview ? ExpandedRect(old_preview_rect, 4) : RECT{},
                          had_new_preview ? ExpandedRect(new_preview_rect, 4) : RECT{}, invalid_rect))
        {
            return;
        }

        RECT client_rect{};
        GetClientRect(overlay_window_, &client_rect);
        RECT clipped_invalid_rect{};
        if (IntersectRect(&clipped_invalid_rect, &invalid_rect, &client_rect) != FALSE)
        {
            InvalidateRect(overlay_window_, &clipped_invalid_rect, FALSE);
        }
    }

    void CaptureOverlay::ResetCommittedSelection() noexcept
    {
        has_committed_selection_ = false;
        committed_selection_rect_ = {};
        has_selection_ = false;
        drag_in_progress_ = false;
        has_click_candidate_window_ = false;
        confirm_selection_on_click_ = false;
    }

    bool CaptureOverlay::HandleKeyDown(WPARAM w_param)
    {
        if (w_param == VK_ESCAPE)
        {
            Finish(OverlayResult::Cancelled);
            return true;
        }

        if (w_param == VK_RETURN && has_committed_selection_)
        {
            last_selection_rect_ = committed_selection_rect_;
            Finish(OverlayResult::PlaceholderCaptured);
            return true;
        }

        if (w_param == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) != 0)
        {
            committed_selection_rect_ = OverlayRectScreen();
            has_committed_selection_ = IsRectNonEmpty(committed_selection_rect_);
            has_selection_ = false;
            drag_in_progress_ = false;
            has_click_candidate_window_ = false;
            confirm_selection_on_click_ = false;
            if (has_committed_selection_)
            {
                InvalidateRect(overlay_window_, nullptr, FALSE);
            }
            return true;
        }

        return false;
    }

    void CaptureOverlay::BeginPointerSelection(LPARAM l_param) noexcept
    {
        drag_start_.x = GET_X_LPARAM(l_param);
        drag_start_.y = GET_Y_LPARAM(l_param);
        drag_current_ = drag_start_;
        pointer_down_ = true;
        drag_in_progress_ = false;
        has_selection_ = false;
        has_click_candidate_window_ = false;
        confirm_selection_on_click_ = IsPointInsideCommittedSelection(drag_start_);
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
            drag_current_.x = GET_X_LPARAM(l_param);
            drag_current_.y = GET_Y_LPARAM(l_param);
            if (!drag_in_progress_ && (std::abs(drag_current_.x - drag_start_.x) >= kDragThreshold ||
                                       std::abs(drag_current_.y - drag_start_.y) >= kDragThreshold))
            {
                drag_in_progress_ = true;
                has_selection_ = true;
                has_click_candidate_window_ = false;
                confirm_selection_on_click_ = false;
            }

            RECT new_preview_rect{};
            bool const had_new_preview = TryGetCurrentPreviewRect(new_preview_rect);
            InvalidatePreviewRectChange(old_preview_rect, had_old_preview, new_preview_rect, had_new_preview);
            return;
        }

        if (has_committed_selection_)
        {
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
        bool const confirm_selection_on_click = confirm_selection_on_click_;
        RECT const click_candidate_rect = click_candidate_window_rect_;
        ReleaseCapture();
        drag_current_.x = GET_X_LPARAM(l_param);
        drag_current_.y = GET_Y_LPARAM(l_param);
        confirm_selection_on_click_ = false;
        if (was_dragging)
        {
            drag_in_progress_ = false;
            committed_selection_rect_ = CurrentSelectionRectScreen();
            has_committed_selection_ = IsRectNonEmpty(committed_selection_rect_);
            has_selection_ = false;
            if (has_committed_selection_)
            {
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
                InvalidateRect(overlay_window_, nullptr, FALSE);
            }
            return;
        }

        if (confirm_selection_on_click && has_committed_selection_)
        {
            last_selection_rect_ = committed_selection_rect_;
            Finish(OverlayResult::PlaceholderCaptured);
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
            confirm_selection_on_click_ = false;
            has_click_candidate_window_ = false;
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
        CAPTUREZY_LOG_DEBUG(core::LogCategory::Capture, result == OverlayResult::PlaceholderCaptured
                                                            ? L"Overlay finishing with capture."
                                                            : L"Overlay finishing with cancellation.");
        if (result == OverlayResult::PlaceholderCaptured)
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
