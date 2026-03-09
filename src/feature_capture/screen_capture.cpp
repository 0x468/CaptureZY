#include "feature_capture/screen_capture.h"

#include <utility>

namespace capturezy::feature_capture
{
    namespace
    {
        [[nodiscard]] int RectWidth(RECT rect) noexcept
        {
            return rect.right - rect.left;
        }

        [[nodiscard]] int RectHeight(RECT rect) noexcept
        {
            return rect.bottom - rect.top;
        }
    } // namespace

    CapturedBitmap::CapturedBitmap(HBITMAP bitmap, SIZE size) noexcept : bitmap_(bitmap), size_(size) {}

    CapturedBitmap::~CapturedBitmap() noexcept
    {
        if (bitmap_ != nullptr)
        {
            DeleteObject(bitmap_);
        }
    }

    CapturedBitmap::CapturedBitmap(CapturedBitmap &&other) noexcept
        : bitmap_(std::exchange(other.bitmap_, nullptr)), size_(std::exchange(other.size_, SIZE{}))
    {
    }

    CapturedBitmap &CapturedBitmap::operator=(CapturedBitmap &&other) noexcept
    {
        if (this != &other)
        {
            if (bitmap_ != nullptr)
            {
                DeleteObject(bitmap_);
            }

            bitmap_ = std::exchange(other.bitmap_, nullptr);
            size_ = std::exchange(other.size_, SIZE{});
        }

        return *this;
    }

    bool CapturedBitmap::IsValid() const noexcept
    {
        return bitmap_ != nullptr;
    }

    HBITMAP CapturedBitmap::Get() const noexcept
    {
        return bitmap_;
    }

    HBITMAP CapturedBitmap::Release() noexcept
    {
        size_ = {};
        return std::exchange(bitmap_, nullptr);
    }

    SIZE CapturedBitmap::Size() const noexcept
    {
        return size_;
    }

    CapturedBitmap CapturedBitmap::Clone() const noexcept
    {
        if (bitmap_ == nullptr)
        {
            return {};
        }

        return {static_cast<HBITMAP>(CopyImage(bitmap_, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION)), size_};
    }

    CapturedBitmap ScreenCapture::CaptureRegion(RECT screen_rect) noexcept
    {
        int const width = RectWidth(screen_rect);
        int const height = RectHeight(screen_rect);

        if (width <= 0 || height <= 0)
        {
            return {};
        }

        HDC screen_device_context = GetDC(nullptr);
        if (screen_device_context == nullptr)
        {
            return {};
        }

        HDC memory_device_context = CreateCompatibleDC(screen_device_context);
        if (memory_device_context == nullptr)
        {
            ReleaseDC(nullptr, screen_device_context);
            return {};
        }

        HBITMAP bitmap = CreateCompatibleBitmap(screen_device_context, width, height);
        if (bitmap == nullptr)
        {
            DeleteDC(memory_device_context);
            ReleaseDC(nullptr, screen_device_context);
            return {};
        }

        HGDIOBJ previous_bitmap = SelectObject(memory_device_context, bitmap);
        bool const copied = BitBlt(memory_device_context, 0, 0, width, height, screen_device_context, screen_rect.left,
                                   screen_rect.top, SRCCOPY | CAPTUREBLT) != FALSE;

        SelectObject(memory_device_context, previous_bitmap);
        DeleteDC(memory_device_context);
        ReleaseDC(nullptr, screen_device_context);

        if (!copied)
        {
            DeleteObject(bitmap);
            return {};
        }

        return {bitmap, SIZE{width, height}};
    }

    bool ScreenCapture::CopyBitmapToClipboard(HWND owner_window, CapturedBitmap bitmap) noexcept
    {
        if (!bitmap.IsValid() || OpenClipboard(owner_window) == FALSE)
        {
            return false;
        }

        if (EmptyClipboard() == FALSE)
        {
            CloseClipboard();
            return false;
        }

        HBITMAP const clipboard_bitmap = bitmap.Release();
        if (SetClipboardData(CF_BITMAP, clipboard_bitmap) == nullptr)
        {
            CloseClipboard();
            DeleteObject(clipboard_bitmap);
            return false;
        }

        CloseClipboard();
        return true;
    }
} // namespace capturezy::feature_capture
