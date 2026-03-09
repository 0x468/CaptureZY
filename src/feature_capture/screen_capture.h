#pragma once

// clang-format off
#include <windows.h>
// clang-format on

namespace capturezy::feature_capture
{
    class ScreenCapture final
    {
      public:
        [[nodiscard]] static bool CopyRegionToClipboard(HWND owner_window, RECT screen_rect) noexcept;
    };
} // namespace capturezy::feature_capture
