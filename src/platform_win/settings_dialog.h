#pragma once

// clang-format off
#include <windows.h>
// clang-format on

#include <optional>
#include <string>
#include <string_view>

#include "core/app_settings.h"

namespace capturezy::platform_win
{
    [[nodiscard]] std::optional<core::AppSettings> ShowSettingsDialog(HINSTANCE instance, HWND owner_window,
                                                                      core::AppSettings const &initial_settings);
    [[nodiscard]] std::optional<std::wstring> PickDirectoryDialog(HWND owner_window, wchar_t const *title,
                                                                  std::wstring_view initial_directory);
} // namespace capturezy::platform_win
