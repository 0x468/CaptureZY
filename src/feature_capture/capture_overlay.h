#pragma once

#include <cstdint>
#include <optional>
#include <string>

// clang-format off
#include <windows.h>
// clang-format on

#include <windowsx.h>

#include "feature_capture/capture_annotation.h"
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
            CreateAnnotation,
            MoveAnnotation,
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
            ToolShape,
            PlaceholderArrow,
            PlaceholderText,
            PlaceholderMosaic,
            PlaceholderUndo,
            PlaceholderRedo,
            Cancel,
            CopyAndPin,
            CopyOnly,
            SaveToFile,
        };

        enum class EditingAction : std::uint8_t
        {
            None,
            CommitCopy,
            CommitCopyAndPin,
            CommitSaveToFile,
            UndoAnnotation,
            RedoAnnotation,
            ExitOverlay,
            ResetSelection,
        };

        enum class AnnotationHitRegion : std::uint8_t
        {
            None,
            HandleTopLeft,
            HandleTop,
            HandleTopRight,
            HandleRight,
            HandleBottomRight,
            HandleBottom,
            HandleBottomLeft,
            HandleLeft,
            Border,
            Fill,
        };

        struct AnnotationHitResult
        {
            std::optional<std::size_t> annotation_index;
            AnnotationHitRegion region{AnnotationHitRegion::None};

            [[nodiscard]] bool HasHit() const noexcept
            {
                return annotation_index.has_value() && region != AnnotationHitRegion::None;
            }
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
        [[nodiscard]] static AnnotationToolFamily ToolbarToolFamily(ToolbarAction action) noexcept;
        [[nodiscard]] static EditingAction ToolbarEditingAction(ToolbarAction action) noexcept;
        [[nodiscard]] static EditingAction GestureEditingAction(WPARAM w_param, bool control_down) noexcept;
        [[nodiscard]] bool IsToolbarActionEnabled(ToolbarAction action) const noexcept;
        [[nodiscard]] bool IsPointInsideAnnotationInteractionRegion(POINT overlay_point) const noexcept;
        [[nodiscard]] bool IsPointInsideAnnotationCreationRegion(POINT overlay_point) const noexcept;
        [[nodiscard]] bool IsAnnotationToolActive() const noexcept;
        [[nodiscard]] RECT AnnotationCanvasRect() const noexcept;
        [[nodiscard]] bool IsPointInsideToolbar(POINT overlay_point) const noexcept;
        [[nodiscard]] bool IsPointInsideCommittedSelection(POINT overlay_point) const noexcept;
        [[nodiscard]] ResizeHandle HitTestCommittedSelectionResizeHandle(POINT overlay_point) const noexcept;
        [[nodiscard]] AnnotationHitResult HitTestAnnotations(POINT overlay_point) const noexcept;
        [[nodiscard]] static AnnotationHitResult HitTestAnnotation(POINT overlay_point, RECT annotation_rect,
                                                                   AnnotationStyle const &style, bool include_handles,
                                                                   RECT interaction_rect,
                                                                   std::size_t annotation_index) noexcept;
        [[nodiscard]] bool TryGetAnnotationRect(std::size_t annotation_index, RECT &rect) const noexcept;
        [[nodiscard]] RECT ToolbarRect(RECT selection_rect, RECT bounds_rect) const noexcept;
        [[nodiscard]] static RECT ToolbarButtonRect(RECT toolbar_rect, ToolbarAction action) noexcept;
        [[nodiscard]] ToolbarAction HitTestToolbarAction(POINT overlay_point) const noexcept;
        [[nodiscard]] bool TryGetCurrentPreviewRect(RECT &rect) const noexcept;
        [[nodiscard]] bool UpdateHoverWindowFromScreenPoint(POINT screen_point);
        [[nodiscard]] RECT CurrentToolbarRect() const noexcept;
        void InvalidateToolbarVisual() noexcept;
        void UpdateHoveredToolbarAction(POINT overlay_point) noexcept;
        void UpdateAnnotationHoverState(AnnotationHitResult const &hit_result) noexcept;
        void ReconcileAnnotationInteractionState() noexcept;
        void ResetAnnotationDragState() noexcept;
        void InvalidatePreviewRectChange(RECT old_preview_rect, bool had_old_preview, RECT new_preview_rect,
                                         bool had_new_preview) noexcept;
        void InvalidateAnnotationCanvas() noexcept;
        void UpdateCursorForOverlayPoint(POINT overlay_point) noexcept;
        void ResetCommittedSelection() noexcept;
        void BeginCreateAnnotation(POINT overlay_point) noexcept;
        void UpdateCreateAnnotation(POINT overlay_point) noexcept;
        void BeginMoveAnnotation(POINT overlay_point, std::size_t annotation_index) noexcept;
        void UpdateMoveAnnotation(POINT overlay_point) noexcept;
        void BeginMoveSelection(POINT overlay_point) noexcept;
        void UpdateMoveSelection(POINT overlay_point) noexcept;
        void BeginResizeSelection(POINT overlay_point) noexcept;
        void UpdateResizeSelection(POINT overlay_point) noexcept;
        void ResetCommittedSelectionAndRefresh();
        void ExecuteToolbarAction(ToolbarAction action);
        void ExecuteEditingAction(EditingAction action);
        void FinishCommittedSelection(OverlayResult result) noexcept;
        [[nodiscard]] bool HandleKeyDown(WPARAM w_param);
        void BeginPointerSelection(LPARAM l_param) noexcept;
        void UpdatePointerSelection(LPARAM l_param);
        [[nodiscard]] bool CompleteToolbarPointerAction(ToolbarAction pressed_toolbar_action);
        [[nodiscard]] bool CompleteSelectionTransform(PointerDragMode pointer_drag_mode, bool was_dragging);
        [[nodiscard]] bool CompleteAnnotationPointerAction(PointerDragMode pointer_drag_mode, bool was_dragging,
                                                           std::optional<std::size_t> drag_annotation_index,
                                                           AnnotationCanvasPixelRect drag_annotation_origin_bounds,
                                                           AnnotationCanvasPixelRect drag_annotation_preview_bounds);
        [[nodiscard]] bool CompleteSelectionCreation(bool was_dragging, bool had_click_candidate,
                                                     RECT click_candidate_rect);
        void CompletePointerSelection(LPARAM l_param);
        void PaintAnnotations(HDC device_context, RECT annotation_canvas, POINT paint_origin) const noexcept;
        void PaintDraftAnnotation(HDC device_context, RECT annotation_canvas, POINT paint_origin) const noexcept;
        void PaintSelectedAnnotationAdorners(HDC device_context, RECT annotation_canvas,
                                             POINT paint_origin) const noexcept;
        void PaintOverlay() noexcept;
        [[nodiscard]] ATOM RegisterWindowClass() const;
        [[nodiscard]] LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);
        void Finish(OverlayResult result) noexcept;

        [[nodiscard]] static HCURSOR CursorForAnnotationHitRegion(AnnotationHitRegion region) noexcept;
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
        AnnotationCanvasPixelRect draft_annotation_bounds_{};
        bool pointer_down_{false};
        bool drag_in_progress_{false};
        bool has_selection_{false};
        bool has_hover_window_{false};
        bool has_cached_overflow_tray_{false};
        bool has_click_candidate_window_{false};
        bool has_committed_selection_{false};
        bool has_draft_annotation_{false};
        bool debug_overlay_enabled_{false};
        PointerDragMode pointer_drag_mode_{PointerDragMode::None};
        ResizeHandle active_resize_handle_{ResizeHandle::None};
        ResizeHandle resize_anchor_handle_{ResizeHandle::None};
        std::optional<std::size_t> drag_annotation_index_;
        AnnotationCanvasPixelRect drag_annotation_origin_bounds_{};
        AnnotationCanvasPixelRect drag_annotation_preview_bounds_{};
        std::optional<std::size_t> selected_annotation_index_;
        std::optional<std::size_t> hovered_annotation_index_;
        AnnotationHitRegion active_annotation_hit_region_{AnnotationHitRegion::None};
        ToolbarAction hovered_toolbar_action_{ToolbarAction::None};
        ToolbarAction pressed_toolbar_action_{ToolbarAction::None};
        AnnotationSession annotation_session_{};
        std::wstring hover_debug_text_;
        std::wstring cached_overflow_tray_debug_text_;
    };
} // namespace capturezy::feature_capture
