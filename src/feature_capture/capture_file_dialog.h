#pragma once

// clang-format off
#include <windows.h>
// clang-format on

#include <string>

#include "core/app_settings.h"
#include "feature_capture/capture_result.h"

namespace capturezy::feature_capture
{
    [[nodiscard]] bool SaveCaptureResultToDefaultPath(CaptureResult const &capture_result,
                                                      core::AppSettings const &app_settings,
                                                      std::wstring *saved_file_path = nullptr);
    [[nodiscard]] bool SaveCaptureResultWithPngDialog(HWND owner_window, CaptureResult const &capture_result,
                                                      core::AppSettings const &app_settings);
} // namespace capturezy::feature_capture
