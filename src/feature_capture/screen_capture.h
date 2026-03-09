#pragma once

// clang-format off
#include <windows.h>
// clang-format on

namespace capturezy::feature_capture
{
    class CaptureResult;

    class CapturedBitmap final
    {
      public:
        CapturedBitmap() noexcept = default;
        CapturedBitmap(HBITMAP bitmap, SIZE size) noexcept;
        ~CapturedBitmap() noexcept;

        CapturedBitmap(CapturedBitmap const &) = delete;
        CapturedBitmap &operator=(CapturedBitmap const &) = delete;
        CapturedBitmap(CapturedBitmap &&other) noexcept;
        CapturedBitmap &operator=(CapturedBitmap &&other) noexcept;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] HBITMAP Get() const noexcept;
        [[nodiscard]] HBITMAP Release() noexcept;
        [[nodiscard]] SIZE Size() const noexcept;
        [[nodiscard]] CapturedBitmap Clone() const noexcept;

      private:
        HBITMAP bitmap_{};
        SIZE size_{};
    };

    class ScreenCapture final
    {
      public:
        [[nodiscard]] static CaptureResult CaptureRegion(RECT screen_rect) noexcept;
        [[nodiscard]] static bool CopyBitmapToClipboard(HWND owner_window,
                                                        CaptureResult const &capture_result) noexcept;
    };
} // namespace capturezy::feature_capture
