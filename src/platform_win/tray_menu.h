#pragma once

// clang-format off
#include <windows.h>
// clang-format on

#include "core/app_settings.h"
#include "feature_pin/pin_manager.h"

namespace capturezy::platform_win
{
    namespace TrayMenuCommand
    {
        constexpr UINT_PTR BeginCapture = 1002;
        constexpr UINT_PTR BeginCaptureAndSave = 1003;
        constexpr UINT_PTR BeginFullScreenCapture = 1004;
        constexpr UINT_PTR BeginFullScreenCaptureAndSave = 1005;
        constexpr UINT_PTR ShowAllPins = 1006;
        constexpr UINT_PTR HideAllPins = 1007;
        constexpr UINT_PTR CloseAllPins = 1008;
        constexpr UINT_PTR ExitApplication = 1009;
        constexpr UINT_PTR EditSettingsFile = 1010;
        constexpr UINT_PTR OpenSettingsDirectory = 1011;
        constexpr UINT_PTR ReloadSettings = 1012;
        constexpr UINT_PTR SetDefaultScopeRegion = 1013;
        constexpr UINT_PTR SetDefaultScopeFullScreen = 1014;
        constexpr UINT_PTR SetDefaultActionCopyOnly = 1015;
        constexpr UINT_PTR SetDefaultActionCopyAndPin = 1016;
        constexpr UINT_PTR SetDefaultActionSaveToFile = 1017;
        constexpr UINT_PTR OpenDefaultSaveDirectory = 1018;
        constexpr UINT_PTR OpenSettingsDialog = 1022;
    } // namespace TrayMenuCommand

    void ShowMainTrayMenu(HWND owner_window, core::AppSettings const &app_settings,
                          feature_pin::PinManager &pin_manager);
} // namespace capturezy::platform_win
