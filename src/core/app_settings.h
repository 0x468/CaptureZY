#pragma once

// clang-format off
#include <windows.h>
// clang-format on

#include <cstdint>
#include <string>

namespace capturezy::core
{
    enum class CaptureScopeSetting : std::uint8_t
    {
        Region,
        FullScreen,
    };

    enum class CaptureActionSetting : std::uint8_t
    {
        CopyOnly,
        CopyAndPin,
        SaveToFile,
    };

    enum class TrayIconClickActionSetting : std::uint8_t
    {
        Disabled,
        OpenMenu,
        StartCapture,
    };

    struct HotkeySetting final
    {
        UINT modifiers{MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT};
        UINT virtual_key{0x41};
    };

    struct AppSettings final
    {
        HotkeySetting capture_hotkey{};
        CaptureScopeSetting default_capture_scope{CaptureScopeSetting::Region};
        CaptureActionSetting default_capture_action{CaptureActionSetting::CopyAndPin};
        TrayIconClickActionSetting tray_single_click_action{TrayIconClickActionSetting::OpenMenu};
        TrayIconClickActionSetting tray_double_click_action{TrayIconClickActionSetting::Disabled};
        std::wstring default_save_directory;
        std::wstring default_save_file_prefix{L"CaptureZY"};

        [[nodiscard]] bool HasValidCaptureHotkey() const noexcept;
    };
} // namespace capturezy::core
