#pragma once

// clang-format off
#include <windows.h>
// clang-format on

#include <cstdint>

#include "feature_capture/screen_capture.h"

namespace capturezy::feature_pin
{
    class PinWindow final
    {
      public:
        explicit PinWindow(HINSTANCE instance) noexcept;
        ~PinWindow() noexcept;

        PinWindow(PinWindow const &) = delete;
        PinWindow &operator=(PinWindow const &) = delete;
        PinWindow(PinWindow &&) = delete;
        PinWindow &operator=(PinWindow &&) = delete;

        [[nodiscard]] bool Create(RECT anchor_rect, feature_capture::CapturedBitmap bitmap);
        void Close() noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;

      private:
        [[nodiscard]] static RECT CalculateWindowRect(RECT anchor_rect, SIZE bitmap_size) noexcept;
        [[nodiscard]] SIZE CurrentClientSize() const noexcept;
        [[nodiscard]] ATOM RegisterWindowClass() const;
        [[nodiscard]] ATOM RegisterScaleOverlayClass() const;
        bool UpdateScale(short wheel_delta, POINT anchor_screen_point) noexcept;
        void PaintWindow() const noexcept;
        void PaintScaleOverlay(HWND overlay_window) const noexcept;
        void ShowScaleOverlay() noexcept;
        void HideScaleOverlay() noexcept;
        void UpdateScaleOverlayPosition() const noexcept;
        [[nodiscard]] LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
        static LRESULT CALLBACK ScaleOverlayProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

        HINSTANCE instance_;
        HWND window_{};
        HWND scale_overlay_window_{};
        feature_capture::CapturedBitmap bitmap_{};
        std::int32_t scale_percent_{100};
    };
} // namespace capturezy::feature_pin
