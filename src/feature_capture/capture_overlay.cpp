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
        constexpr wchar_t const *kOverlayInstruction = L"窗口预选：悬停高亮，单击截取窗口；也可随时按住左键拖拽框选，Ct"
                                                       L"rl+A 全屏，右键或 Esc 取消";
        constexpr int kDragThreshold = 4;

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
        pointer_down_ = false;
        drag_in_progress_ = false;
        has_selection_ = false;
        has_hover_window_ = false;
        has_click_candidate_window_ = false;

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

    bool CaptureOverlay::HandleKeyDown(WPARAM w_param)
    {
        if (w_param == VK_ESCAPE)
        {
            Finish(OverlayResult::Cancelled);
            return true;
        }

        if (w_param == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) != 0)
        {
            last_selection_rect_ = OverlayRectScreen();
            Finish(OverlayResult::PlaceholderCaptured);
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
        if (has_hover_window_)
        {
            click_candidate_window_rect_ = hover_window_rect_;
            has_click_candidate_window_ = true;
        }

        SetCapture(overlay_window_);
    }

    void CaptureOverlay::UpdatePointerSelection(LPARAM l_param)
    {
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
            }

            if (drag_in_progress_)
            {
                InvalidateRect(overlay_window_, nullptr, TRUE);
            }
            return;
        }

        POINT cursor_position{};
        GetCursorPos(&cursor_position);
        if (UpdateHoverWindowFromScreenPoint(cursor_position))
        {
            InvalidateRect(overlay_window_, nullptr, TRUE);
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
        RECT const click_candidate_rect = click_candidate_window_rect_;
        ReleaseCapture();
        drag_current_.x = GET_X_LPARAM(l_param);
        drag_current_.y = GET_Y_LPARAM(l_param);
        if (was_dragging)
        {
            drag_in_progress_ = false;
            last_selection_rect_ = CurrentSelectionRectScreen();
            Finish(IsRectNonEmpty(last_selection_rect_) ? OverlayResult::PlaceholderCaptured
                                                        : OverlayResult::Cancelled);
            return;
        }

        if (had_click_candidate)
        {
            has_click_candidate_window_ = false;
            last_selection_rect_ = click_candidate_rect;
            Finish(OverlayResult::PlaceholderCaptured);
            return;
        }

        Finish(OverlayResult::Cancelled);
    }

    void CaptureOverlay::PaintOverlay() noexcept
    {
        PAINTSTRUCT paint{};
        HDC device_context = BeginPaint(overlay_window_, &paint);

        RECT client_rect{};
        GetClientRect(overlay_window_, &client_rect);
        if (frozen_background_.IsValid())
        {
            HDC bitmap_device_context = CreateCompatibleDC(device_context);
            if (bitmap_device_context != nullptr)
            {
                HGDIOBJ previous_bitmap = SelectObject(bitmap_device_context, frozen_background_.Get());
                SIZE const background_size = frozen_background_.Size();
                (void)BitBlt(device_context, 0, 0, background_size.cx, background_size.cy, bitmap_device_context, 0, 0,
                             SRCCOPY);
                SelectObject(bitmap_device_context, previous_bitmap);
                DeleteDC(bitmap_device_context);
            }
        }
        else
        {
            FillRect(device_context, &client_rect, GetSysColorBrush(COLOR_WINDOWTEXT));
        }

        AlphaFillRect(device_context, client_rect, kOverlayAlpha);

        RECT preview_rect{};
        bool has_preview_rect = false;
        COLORREF border_color = RGB(255, 215, 0);

        if (has_selection_)
        {
            preview_rect = CurrentSelectionRect();
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
            if (frozen_background_.IsValid())
            {
                HDC bitmap_device_context = CreateCompatibleDC(device_context);
                if (bitmap_device_context != nullptr)
                {
                    HGDIOBJ previous_bitmap = SelectObject(bitmap_device_context, frozen_background_.Get());
                    (void)BitBlt(device_context, preview_rect.left, preview_rect.top,
                                 preview_rect.right - preview_rect.left, preview_rect.bottom - preview_rect.top,
                                 bitmap_device_context, preview_rect.left, preview_rect.top, SRCCOPY);
                    SelectObject(bitmap_device_context, previous_bitmap);
                    DeleteDC(bitmap_device_context);
                }
            }

            HPEN selection_pen = CreatePen(PS_SOLID, 2, border_color);
            HGDIOBJ old_pen = SelectObject(device_context, selection_pen);
            HGDIOBJ old_brush = SelectObject(device_context, GetStockObject(HOLLOW_BRUSH));
            Rectangle(device_context, preview_rect.left, preview_rect.top, preview_rect.right, preview_rect.bottom);
            SelectObject(device_context, old_brush);
            SelectObject(device_context, old_pen);
            DeleteObject(selection_pen);
        }

        SetBkMode(device_context, TRANSPARENT);
        SetTextColor(device_context, RGB(255, 255, 255));
        DrawTextW(device_context, kOverlayInstruction, -1, &client_rect, DT_CENTER | DT_WORDBREAK | DT_TOP);

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
