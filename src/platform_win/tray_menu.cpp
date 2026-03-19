#include "platform_win/tray_menu.h"

#include <string>

namespace capturezy::platform_win
{
    namespace
    {
        struct BatchPinMenuLabelParts final
        {
            wchar_t const *action_prefix;
            wchar_t const *item_label;
        };

        struct PinInventoryCounts final
        {
            std::size_t open_pin_count;
            std::size_t visible_pin_count;
            std::size_t hidden_pin_count;
        };

        [[nodiscard]] std::wstring BatchPinMenuLabel(BatchPinMenuLabelParts parts, std::size_t count)
        {
            if (count == 0)
            {
                return parts.action_prefix;
            }

            std::wstring label = parts.action_prefix;
            label += L" ";
            label += std::to_wstring(count);
            label += L" ";
            label += parts.item_label;
            return label;
        }

        [[nodiscard]] std::wstring PinInventorySummaryLabel(PinInventoryCounts counts)
        {
            if (counts.visible_pin_count == 0 && counts.hidden_pin_count == 0)
            {
                return L"贴图：当前无贴图";
            }

            std::wstring label = L"贴图：显示 ";
            label += std::to_wstring(counts.visible_pin_count);
            label += L" 个";
            if (counts.hidden_pin_count != 0)
            {
                label += L"，隐藏 ";
                label += std::to_wstring(counts.hidden_pin_count);
                label += L" 个";
            }
            return label;
        }

        [[nodiscard]] std::wstring CloseAllPinsLabel(PinInventoryCounts counts)
        {
            if (counts.open_pin_count == 0)
            {
                return L"关闭全部贴图";
            }

            std::wstring label = BatchPinMenuLabel({.action_prefix = L"关闭", .item_label = L"个贴图"},
                                                   counts.open_pin_count);
            if (counts.hidden_pin_count != 0)
            {
                label += L"（含 ";
                label += std::to_wstring(counts.hidden_pin_count);
                label += L" 个隐藏）";
            }
            return label;
        }

        void AppendDefaultScopeMenu(HMENU menu, core::AppSettings const &app_settings)
        {
            HMENU default_scope_menu = CreatePopupMenu();
            if (default_scope_menu == nullptr)
            {
                return;
            }

            AppendMenuW(default_scope_menu,
                        app_settings.default_capture_scope == core::CaptureScopeSetting::Region ? MF_STRING | MF_CHECKED
                                                                                                : MF_STRING,
                        TrayMenuCommand::SetDefaultScopeRegion, L"区域截图");
            AppendMenuW(default_scope_menu,
                        app_settings.default_capture_scope == core::CaptureScopeSetting::FullScreen
                            ? MF_STRING | MF_CHECKED
                            : MF_STRING,
                        TrayMenuCommand::SetDefaultScopeFullScreen, L"全屏截图");
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
            auto const default_scope_menu_handle = reinterpret_cast<UINT_PTR>(default_scope_menu);
            AppendMenuW(menu, MF_POPUP, default_scope_menu_handle, L"默认截图范围");
        }

        void AppendDefaultActionMenu(HMENU menu, core::AppSettings const &app_settings)
        {
            HMENU default_action_menu = CreatePopupMenu();
            if (default_action_menu == nullptr)
            {
                return;
            }

            AppendMenuW(default_action_menu,
                        app_settings.default_capture_action == core::CaptureActionSetting::CopyOnly
                            ? MF_STRING | MF_CHECKED
                            : MF_STRING,
                        TrayMenuCommand::SetDefaultActionCopyOnly, L"仅复制");
            AppendMenuW(default_action_menu,
                        app_settings.default_capture_action == core::CaptureActionSetting::CopyAndPin
                            ? MF_STRING | MF_CHECKED
                            : MF_STRING,
                        TrayMenuCommand::SetDefaultActionCopyAndPin, L"复制并贴图");
            AppendMenuW(default_action_menu,
                        app_settings.default_capture_action == core::CaptureActionSetting::SaveToFile
                            ? MF_STRING | MF_CHECKED
                            : MF_STRING,
                        TrayMenuCommand::SetDefaultActionSaveToFile, L"直接保存");
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
            auto const default_action_menu_handle = reinterpret_cast<UINT_PTR>(default_action_menu);
            AppendMenuW(menu, MF_POPUP, default_action_menu_handle, L"默认截图动作");
        }

        void AppendPinSection(HMENU menu, feature_pin::PinManager &pin_manager)
        {
            std::size_t const open_pin_count = pin_manager.OpenPinCount();
            std::size_t const visible_pin_count = pin_manager.VisiblePinCount();
            std::size_t const hidden_pin_count = pin_manager.HiddenPinCount();
            PinInventoryCounts const pin_counts{.open_pin_count = open_pin_count,
                                                .visible_pin_count = visible_pin_count,
                                                .hidden_pin_count = hidden_pin_count};

            UINT show_all_pins_flags = hidden_pin_count == 0 ? MF_STRING | MF_GRAYED : MF_STRING;
            UINT hide_all_pins_flags = visible_pin_count == 0 ? MF_STRING | MF_GRAYED : MF_STRING;
            UINT close_all_pins_flags = open_pin_count == 0 ? MF_STRING | MF_GRAYED : MF_STRING;

            std::wstring const show_all_pins_label = hidden_pin_count == 0
                                                         ? L"恢复全部贴图"
                                                         : BatchPinMenuLabel(
                                                               {.action_prefix = L"恢复", .item_label = L"个隐藏贴图"},
                                                               hidden_pin_count);
            std::wstring const hide_all_pins_label = visible_pin_count == 0
                                                         ? L"隐藏全部贴图"
                                                         : BatchPinMenuLabel(
                                                               {.action_prefix = L"隐藏", .item_label = L"个贴图"},
                                                               visible_pin_count);
            std::wstring const close_all_pins_label = CloseAllPinsLabel(pin_counts);

            AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, PinInventorySummaryLabel(pin_counts).c_str());
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, show_all_pins_flags, TrayMenuCommand::ShowAllPins, show_all_pins_label.c_str());
            AppendMenuW(menu, hide_all_pins_flags, TrayMenuCommand::HideAllPins, hide_all_pins_label.c_str());
            AppendMenuW(menu, close_all_pins_flags, TrayMenuCommand::CloseAllPins, close_all_pins_label.c_str());
        }
    } // namespace

    void ShowMainTrayMenu(HWND owner_window, core::AppSettings const &app_settings,
                          feature_pin::PinManager &pin_manager)
    {
        if (owner_window == nullptr)
        {
            return;
        }

        HMENU menu = CreatePopupMenu();
        if (menu == nullptr)
        {
            return;
        }

        AppendMenuW(menu, MF_STRING, TrayMenuCommand::BeginCapture, L"开始截图");
        AppendMenuW(menu, MF_STRING, TrayMenuCommand::BeginCaptureAndSave, L"开始截图并保存");
        AppendMenuW(menu, MF_STRING, TrayMenuCommand::BeginFullScreenCapture, L"全屏截图");
        AppendMenuW(menu, MF_STRING, TrayMenuCommand::BeginFullScreenCaptureAndSave, L"全屏截图并保存");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, TrayMenuCommand::OpenSettingsDialog, L"设置...");
        AppendDefaultScopeMenu(menu, app_settings);
        AppendDefaultActionMenu(menu, app_settings);
        AppendMenuW(menu, MF_STRING, TrayMenuCommand::EditSettingsFile, L"编辑配置文件");
        AppendMenuW(menu, MF_STRING, TrayMenuCommand::OpenSettingsDirectory, L"打开配置目录");
        AppendMenuW(menu, MF_STRING, TrayMenuCommand::OpenDefaultSaveDirectory, L"打开默认保存目录");
        AppendMenuW(menu, MF_STRING, TrayMenuCommand::ReloadSettings, L"重新加载配置");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendPinSection(menu, pin_manager);
        AppendMenuW(menu, MF_STRING, TrayMenuCommand::ExitApplication, L"退出");

        POINT cursor_position{};
        GetCursorPos(&cursor_position);
        SetForegroundWindow(owner_window);
        TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RIGHTBUTTON, cursor_position.x, cursor_position.y, 0,
                       owner_window, nullptr);
        PostMessageW(owner_window, WM_NULL, 0, 0);
        DestroyMenu(menu);
    }
} // namespace capturezy::platform_win
