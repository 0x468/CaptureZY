#include "feature_capture/capture_result.h"

#include <utility>

namespace capturezy::feature_capture
{
    CaptureResult::CaptureResult(CapturedBitmap bitmap, RECT screen_rect, Timestamp captured_at) noexcept
        : bitmap_(std::move(bitmap)), screen_rect_(screen_rect), captured_at_(captured_at)
    {
    }

    bool CaptureResult::IsValid() const noexcept
    {
        return bitmap_.IsValid();
    }

    RECT CaptureResult::ScreenRect() const noexcept
    {
        return screen_rect_;
    }

    SIZE CaptureResult::PixelSize() const noexcept
    {
        return bitmap_.Size();
    }

    CaptureResult::Timestamp CaptureResult::CapturedAt() const noexcept
    {
        return captured_at_;
    }

    CapturedBitmap const &CaptureResult::Bitmap() const noexcept
    {
        return bitmap_;
    }

    CapturedBitmap CaptureResult::CloneBitmap() const noexcept
    {
        return bitmap_.Clone();
    }

    CaptureResult CaptureResult::Clone() const noexcept
    {
        return {bitmap_.Clone(), screen_rect_, captured_at_};
    }
} // namespace capturezy::feature_capture
