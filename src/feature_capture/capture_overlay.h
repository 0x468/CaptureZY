#pragma once

#include <cstdint>
#include <string>

// clang-format off
#include <windows.h>
// clang-format on

#include <windowsx.h>

#include "feature_capture/capture_result.h"
#include "feature_capture/screen_capture.h"

namespace capturezy::feature_capture
{
    enum class OverlayResult : std::uint8_t
    {
        Cancelled,
        CopyAndPin,
        CopyOnly,
        SaveToFile,
    };

    class CaptureOverlay final
    {
      public:
        explicit CaptureOverlay(HINSTANCE instance) noexcept;

        [[nodiscard]] bool Show(HWND owner_window);
        void Close() noexcept;
        [[nodiscard]] bool IsVisible() const noexcept;
        [[nodiscard]] RECT LastSelectionRect() const noexcept;
        [[nodiscard]] CaptureResult FrozenSelectionResult() const noexcept;

        static constexpr UINT ResultMessage() noexcept
        {
            return WM_APP + 20;
        }

      private:
        enum class PointerDragMode : std::uint8_t
        {
            None,
            CreateSelection,
            MoveSelection,
            ResizeSelection,
        };

        enum class ResizeHandle : std::uint8_t
        {
            None = 0,
            Left = 1,
            Top = 2,
            LeftTop = 3,
            Right = 4,
            RightTop = 6,
            Bottom = 8,
            LeftBottom = 9,
            RightBottom = 12,
        };

        enum class ToolbarAction : std::uint8_t
        {
            None,
            PlaceholderArrow,
            PlaceholderPen,
            PlaceholderText,
            PlaceholderMosaic,
            PlaceholderUndo,
            PlaceholderRedo,
            Cancel,
            CopyAndPin,
            CopyOnly,
            SaveToFile,
        };

        struct ToolbarActionSpec
        {
            ToolbarAction action;
            wchar_t const *label;
            wchar_t const *hint;
            int group;
            int index_in_group;
            int width;
            bool interactive;
        };

        [[nodiscard]] RECT CurrentSelectionRect() const noexcept;
        [[nodiscard]] RECT CurrentSelectionRectScreen() const noexcept;
        [[nodiscard]] RECT OverlayRectScreen() const noexcept;
        [[nodiscard]] RECT OverlayToClientRect(RECT rect) const noexcept;
        [[nodiscard]] static bool HasResizeHandle(ResizeHandle handle, ResizeHandle component) noexcept;
        [[nodiscard]] static bool ShouldShowResizeHandles(RECT selection_rect) noexcept;
        [[nodiscard]] static HCURSOR CursorForResizeHandle(ResizeHandle handle) noexcept;
        [[nodiscard]] static HCURSOR MoveSelectionCursor() noexcept;
        [[nodiscard]] static HCURSOR ToolbarCursor() noexcept;
        [[nodiscard]] static ToolbarActionSpec const &ToolbarActionMetadata(ToolbarAction action) noexcept;
        [[nodiscard]] static wchar_t const *ToolbarActionLabel(ToolbarAction action) noexcept;
        [[nodiscard]] static wchar_t const *ToolbarActionHint(ToolbarAction action) noexcept;
        [[nodiscard]] static bool IsInteractiveToolbarAction(ToolbarAction action) noexcept;
        [[nodiscard]] static int ToolbarActionGroup(ToolbarAction action) noexcept;
        [[nodiscard]] static int ToolbarActionIndexInGroup(ToolbarAction action) noexcept;
        [[nodiscard]] static int ToolbarButtonWidth(ToolbarAction action) noexcept;
        [[nodiscard]] static int ToolbarGroupActionCount(int group) noexcept;
        [[nodiscard]] static int ToolbarGroupWidth(int group) noexcept;
        [[nodiscard]] bool IsPointInsideToolbar(POINT overlay_point) const noexcept;
        [[nodiscard]] bool IsPointInsideCommittedSelection(POINT overlay_point) const noexcept;
        [[nodiscard]] ResizeHandle HitTestCommittedSelectionResizeHandle(POINT overlay_point) const noexcept;
        [[nodiscard]] RECT ToolbarRect(RECT selection_rect, RECT bounds_rect) const noexcept;
        [[nodiscard]] static RECT ToolbarButtonRect(RECT toolbar_rect, ToolbarAction action) noexcept;
        [[nodiscard]] ToolbarAction HitTestToolbarAction(POINT overlay_point) const noexcept;
        [[nodiscard]] bool TryGetCurrentPreviewRect(RECT &rect) const noexcept;
        [[nodiscard]] bool UpdateHoverWindowFromScreenPoint(POINT screen_point);
        [[nodiscard]] RECT CurrentToolbarRect() const noexcept;
        void InvalidateToolbarVisual() noexcept;
        void UpdateHoveredToolbarAction(POINT overlay_point) noexcept;
        void InvalidatePreviewRectChange(RECT old_preview_rect, bool had_old_preview, RECT new_preview_rect,
                                         bool had_new_preview) noexcept;
        void UpdateCursorForOverlayPoint(POINT overlay_point) noexcept;
        void ResetCommittedSelection() noexcept;
        void BeginMoveSelection(POINT overlay_point) noexcept;
        void UpdateMoveSelection(POINT overlay_point) noexcept;
        void BeginResizeSelection(POINT overlay_point) noexcept;
        void UpdateResizeSelection(POINT overlay_point) noexcept;
        void FinishCommittedSelection(OverlayResult result) noexcept;
        [[nodiscard]] bool HandleKeyDown(WPARAM w_param);
        void BeginPointerSelection(LPARAM l_param) noexcept;
        void UpdatePointerSelection(LPARAM l_param);
        void CompletePointerSelection(LPARAM l_param) noexcept;
        void PaintOverlay() noexcept;
        [[nodiscard]] ATOM RegisterWindowClass() const;
        [[nodiscard]] LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);
        void Finish(OverlayResult result) noexcept;

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

        HINSTANCE instance_;
        HWND owner_window_{};
        HWND overlay_window_{};
        CapturedBitmap frozen_background_;
        CapturedBitmap dimmed_background_;
        CaptureResult final_capture_result_;
        int origin_left_{0};
        int origin_top_{0};
        RECT last_selection_rect_{};
        POINT drag_start_{};
        POINT drag_current_{};
        RECT hover_window_rect_{};
        RECT cached_overflow_tray_rect_{};
        RECT click_candidate_window_rect_{};
        RECT committed_selection_rect_{};
        RECT resize_anchor_selection_rect_{};
        bool pointer_down_{false};
        bool drag_in_progress_{false};
        bool has_selection_{false};
        bool has_hover_window_{false};
        bool has_cached_overflow_tray_{false};
        bool has_click_candidate_window_{false};
        bool has_committed_selection_{false};
        bool debug_overlay_enabled_{false};
        PointerDragMode pointer_drag_mode_{PointerDragMode::None};
        ResizeHandle active_resize_handle_{ResizeHandle::None};
        ResizeHandle resize_anchor_handle_{ResizeHandle::None};
        ToolbarAction hovered_toolbar_action_{ToolbarAction::None};
        ToolbarAction pressed_toolbar_action_{ToolbarAction::None};
        std::wstring hover_debug_text_;
        std::wstring cached_overflow_tray_debug_text_;
    };
} // namespace capturezy::feature_capture
