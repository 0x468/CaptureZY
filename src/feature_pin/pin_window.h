#pragma once

// clang-format off
#include <windows.h>
// clang-format on

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
        [[nodiscard]] RECT CalculateWindowRect(RECT anchor_rect) const noexcept;
        [[nodiscard]] ATOM RegisterWindowClass() const;
        [[nodiscard]] LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

        HINSTANCE instance_;
        HWND window_{};
        feature_capture::CapturedBitmap bitmap_{};
    };
} // namespace capturezy::feature_pin
