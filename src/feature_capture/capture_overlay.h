#pragma once

#include <cstdint>

// clang-format off
#include <windows.h>
// clang-format on

#include <windowsx.h>

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

        static constexpr UINT ResultMessage() noexcept
        {
            return WM_APP + 20;
        }

      private:
        [[nodiscard]] RECT CurrentSelectionRect() const noexcept;
        [[nodiscard]] ATOM RegisterWindowClass() const;
        [[nodiscard]] LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);
        void Finish(OverlayResult result) noexcept;

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

        HINSTANCE instance_;
        HWND owner_window_{};
        HWND overlay_window_{};
        POINT drag_start_{};
        POINT drag_current_{};
        bool drag_in_progress_{false};
        bool has_selection_{false};
    };
} // namespace capturezy::feature_capture
