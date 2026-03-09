#include "feature_capture/screen_capture.h"

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

    bool ScreenCapture::CopyRegionToClipboard(HWND owner_window, RECT screen_rect) noexcept
    {
        if (RectWidth(screen_rect) <= 0 || RectHeight(screen_rect) <= 0)
        {
            return false;
        }

        HDC screen_device_context = GetDC(nullptr);
        if (screen_device_context == nullptr)
        {
            return false;
        }

        HDC memory_device_context = CreateCompatibleDC(screen_device_context);
        if (memory_device_context == nullptr)
        {
            ReleaseDC(nullptr, screen_device_context);
            return false;
        }

        HBITMAP bitmap = CreateCompatibleBitmap(screen_device_context, RectWidth(screen_rect), RectHeight(screen_rect));
        if (bitmap == nullptr)
        {
            DeleteDC(memory_device_context);
            ReleaseDC(nullptr, screen_device_context);
            return false;
        }

        HGDIOBJ previous_bitmap = SelectObject(memory_device_context, bitmap);
        bool const copied = BitBlt(memory_device_context, 0, 0, RectWidth(screen_rect), RectHeight(screen_rect),
                                   screen_device_context, screen_rect.left, screen_rect.top,
                                   SRCCOPY | CAPTUREBLT) != FALSE;

        SelectObject(memory_device_context, previous_bitmap);
        DeleteDC(memory_device_context);
        ReleaseDC(nullptr, screen_device_context);

        if (!copied)
        {
            DeleteObject(bitmap);
            return false;
        }

        if (OpenClipboard(owner_window) == FALSE)
        {
            DeleteObject(bitmap);
            return false;
        }

        EmptyClipboard();
        if (SetClipboardData(CF_BITMAP, bitmap) == nullptr)
        {
            CloseClipboard();
            DeleteObject(bitmap);
            return false;
        }

        CloseClipboard();
        return true;
    }
} // namespace capturezy::feature_capture
