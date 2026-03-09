#pragma once

// clang-format off
#include <windows.h>
// clang-format on

#include "feature_capture/capture_result.h"

namespace capturezy::feature_capture
{
    [[nodiscard]] bool SaveCaptureResultWithPngDialog(HWND owner_window, CaptureResult const &capture_result);
} // namespace capturezy::feature_capture
