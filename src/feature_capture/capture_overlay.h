#pragma once

#include <cstdint>

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
        PlaceholderCaptured,
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
        [[nodiscard]] RECT CurrentSelectionRect() const noexcept;
        [[nodiscard]] RECT CurrentSelectionRectScreen() const noexcept;
        [[nodiscard]] RECT OverlayRectScreen() const noexcept;
        [[nodiscard]] RECT OverlayToClientRect(RECT rect) const noexcept;
        [[nodiscard]] bool IsPointInsideCommittedSelection(POINT overlay_point) const noexcept;
        [[nodiscard]] bool TryGetCurrentPreviewRect(RECT &rect) const noexcept;
        [[nodiscard]] bool UpdateHoverWindowFromScreenPoint(POINT screen_point) noexcept;
        void InvalidatePreviewRectChange(RECT old_preview_rect, bool had_old_preview, RECT new_preview_rect,
                                         bool had_new_preview) noexcept;
        void ResetCommittedSelection() noexcept;
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
        CaptureResult final_capture_result_{};
        int origin_left_{0};
        int origin_top_{0};
        RECT last_selection_rect_{};
        POINT drag_start_{};
        POINT drag_current_{};
        RECT hover_window_rect_{};
        RECT click_candidate_window_rect_{};
        RECT committed_selection_rect_{};
        bool pointer_down_{false};
        bool drag_in_progress_{false};
        bool has_selection_{false};
        bool has_hover_window_{false};
        bool has_click_candidate_window_{false};
        bool has_committed_selection_{false};
        bool confirm_selection_on_click_{false};
    };
} // namespace capturezy::feature_capture
