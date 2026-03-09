#pragma once

// clang-format off
#include <windows.h>
// clang-format on

#include <chrono>

#include "feature_capture/screen_capture.h"

namespace capturezy::feature_capture
{
    class CaptureResult final
    {
      public:
        using Timestamp = std::chrono::system_clock::time_point;

        CaptureResult() noexcept = default;
        CaptureResult(CapturedBitmap bitmap, RECT screen_rect, Timestamp captured_at) noexcept;

        CaptureResult(CaptureResult const &) = delete;
        CaptureResult &operator=(CaptureResult const &) = delete;
        CaptureResult(CaptureResult &&) noexcept = default;
        CaptureResult &operator=(CaptureResult &&) noexcept = default;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] RECT ScreenRect() const noexcept;
        [[nodiscard]] SIZE PixelSize() const noexcept;
        [[nodiscard]] Timestamp CapturedAt() const noexcept;
        [[nodiscard]] CapturedBitmap const &Bitmap() const noexcept;
        [[nodiscard]] CapturedBitmap CloneBitmap() const noexcept;
        [[nodiscard]] CaptureResult Clone() const noexcept;

      private:
        CapturedBitmap bitmap_{};
        RECT screen_rect_{};
        Timestamp captured_at_{};
    };
} // namespace capturezy::feature_capture
