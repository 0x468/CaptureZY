#include <filesystem>
#include <string>
#include <utility>

#include "core/app_settings_store.h"
#include "core/log.h"
#include "platform_win/main_window.h"
#include "platform_win/settings_dialog.h"

namespace capturezy::platform_win
{
    namespace
    {
        [[nodiscard]] bool ShellExecuteSucceeded(HINSTANCE result) noexcept
        {
            // NOLINTNEXTLINE(performance-no-int-to-ptr,cppcoreguidelines-pro-type-reinterpret-cast)
            return reinterpret_cast<INT_PTR>(result) > 32;
        }
    } // namespace

    bool MainWindow::ApplySettings(core::AppSettings new_settings, bool persist, wchar_t const *hotkey_error_message,
                                   wchar_t const *save_error_message)
    {
        core::AppSettings const previous_settings = *app_settings_;
        bool const previous_hotkeys_registered = hotkeys_registered_;

        CancelPendingSingleTrayClickAction();
        ignore_next_tray_left_button_up_ = false;

        UnregisterHotkeys();
        *app_settings_ = std::move(new_settings);
        CAPTUREZY_LOG_INFO(core::LogCategory::Settings,
                           std::wstring(L"Apply settings. single=") +
                               std::to_wstring(static_cast<int>(app_settings_->tray_single_click_action)) +
                               L", double=" +
                               std::to_wstring(static_cast<int>(app_settings_->tray_double_click_action)) + L".");
        hotkeys_registered_ = RegisterHotkeys();
        if (!hotkeys_registered_)
        {
            *app_settings_ = previous_settings;
            hotkeys_registered_ = previous_hotkeys_registered;
            if (hotkeys_registered_)
            {
                hotkeys_registered_ = RegisterHotkeys();
            }

            CAPTUREZY_LOG_WARNING(core::LogCategory::Settings, hotkey_error_message);
            ShowMessageDialog(L"CaptureZY", hotkey_error_message, MB_ICONWARNING);
            return false;
        }

        if (!persist || core::AppSettingsStore::Save(*app_settings_))
        {
            return true;
        }

        UnregisterHotkeys();
        *app_settings_ = previous_settings;
        hotkeys_registered_ = previous_hotkeys_registered;
        if (hotkeys_registered_)
        {
            hotkeys_registered_ = RegisterHotkeys();
        }

        CAPTUREZY_LOG_ERROR(core::LogCategory::Settings, save_error_message);
        ShowMessageDialog(L"CaptureZY", save_error_message, MB_ICONERROR);
        return false;
    }

    void MainWindow::OpenSettingsDialog()
    {
        CAPTUREZY_LOG_DEBUG(core::LogCategory::SettingsDialog, L"Open settings dialog.");
        bool const hotkeys_were_active = hotkeys_registered_;
        if (hotkeys_were_active)
        {
            UnregisterHotkeys();
        }

        if (auto updated_settings = ShowSettingsDialog(instance_, window_, *app_settings_);
            updated_settings.has_value())
        {
            if (ApplySettings(std::move(*updated_settings), true, L"新设置中的截图热键注册失败，已保留当前配置。",
                              L"设置写回配置文件失败，已恢复当前配置。"))
            {
                CAPTUREZY_LOG_INFO(core::LogCategory::SettingsDialog, L"Settings dialog accepted.");
                ShowMessageDialog(L"CaptureZY", L"设置已更新。", MB_ICONINFORMATION);
            }

            return;
        }

        CAPTUREZY_LOG_DEBUG(core::LogCategory::SettingsDialog, L"Settings dialog cancelled.");
        if (hotkeys_were_active)
        {
            hotkeys_registered_ = RegisterHotkeys();
        }
    }

    bool MainWindow::OpenSettingsFileForEditing() const
    {
        std::wstring const settings_file_path = core::AppSettingsStore::SettingsFilePath();
        if (settings_file_path.empty())
        {
            return false;
        }

        std::wstring const parameters = L"\"" + settings_file_path + L"\"";
        HINSTANCE const result = ShellExecuteW(window_, L"open", L"notepad.exe", parameters.c_str(), nullptr,
                                               SW_SHOWNORMAL);
        return ShellExecuteSucceeded(result);
    }

    bool MainWindow::OpenSettingsDirectory() const
    {
        std::filesystem::path const settings_file_path(core::AppSettingsStore::SettingsFilePath());
        std::filesystem::path const settings_directory = settings_file_path.parent_path();
        if (settings_directory.empty())
        {
            return false;
        }

        HINSTANCE const result = ShellExecuteW(window_, L"open", settings_directory.c_str(), nullptr, nullptr,
                                               SW_SHOWNORMAL);
        return ShellExecuteSucceeded(result);
    }

    bool MainWindow::OpenDefaultSaveDirectory() const
    {
        std::filesystem::path const save_directory(app_settings_->default_save_directory);
        if (save_directory.empty())
        {
            return false;
        }

        std::error_code error_code;
        std::filesystem::create_directories(save_directory, error_code);
        if (error_code)
        {
            return false;
        }

        HINSTANCE const result = ShellExecuteW(window_, L"open", save_directory.c_str(), nullptr, nullptr,
                                               SW_SHOWNORMAL);
        return ShellExecuteSucceeded(result);
    }

    bool MainWindow::ReloadSettings()
    {
        if (ApplySettings(core::AppSettingsStore::Load(), false, L"配置已读取，但新热键注册失败，已恢复旧热键。", L""))
        {
            CAPTUREZY_LOG_INFO(core::LogCategory::Settings, L"Settings reloaded from disk.");
            ShowMessageDialog(L"CaptureZY", L"配置已重载。", MB_ICONINFORMATION);
            return true;
        }

        CAPTUREZY_LOG_WARNING(core::LogCategory::Settings, L"Settings reload failed.");
        return false;
    }
} // namespace capturezy::platform_win
