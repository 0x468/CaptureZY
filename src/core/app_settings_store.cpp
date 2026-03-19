#include "core/app_settings_store.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <shlobj.h>
#include <string>
#include <string_view>
#include <system_error>

#include "core/log.h"

namespace capturezy::core
{
    namespace
    {
        constexpr wchar_t const *kSettingsDirectoryName = L"CaptureZY";
        constexpr wchar_t const *kSettingsFileName = L"settings.json";
        constexpr wchar_t const *kDefaultSaveDirectoryName = L"CaptureZY";
        constexpr wchar_t const *kDefaultFilePrefix = L"CaptureZY";
        constexpr unsigned int kSettingsVersion = 4;

        [[nodiscard]] std::wstring WideToUtf8FallbackPath(std::filesystem::path const &path)
        {
            return path.wstring();
        }

        [[nodiscard]] std::wstring GetKnownFolderPath(REFKNOWNFOLDERID folder_id)
        {
            PWSTR folder_path = nullptr;
            HRESULT const result = SHGetKnownFolderPath(folder_id, KF_FLAG_CREATE, nullptr, &folder_path);
            if (FAILED(result) || folder_path == nullptr)
            {
                return {};
            }

            std::wstring resolved_path = folder_path;
            CoTaskMemFree(folder_path);
            return resolved_path;
        }

        [[nodiscard]] std::string ToUtf8(std::wstring_view wide_text)
        {
            if (wide_text.empty())
            {
                return {};
            }

            int const size = WideCharToMultiByte(CP_UTF8, 0, wide_text.data(), static_cast<int>(wide_text.size()),
                                                 nullptr, 0, nullptr, nullptr);
            if (size <= 0)
            {
                return {};
            }

            std::string utf8_text(static_cast<std::size_t>(size), '\0');
            (void)WideCharToMultiByte(CP_UTF8, 0, wide_text.data(), static_cast<int>(wide_text.size()),
                                      utf8_text.data(), size, nullptr, nullptr);
            return utf8_text;
        }

        [[nodiscard]] std::wstring FromUtf8(std::string_view utf8_text)
        {
            if (utf8_text.empty())
            {
                return {};
            }

            int const size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_text.data(),
                                                 static_cast<int>(utf8_text.size()), nullptr, 0);
            if (size <= 0)
            {
                return {};
            }

            std::wstring wide_text(static_cast<std::size_t>(size), L'\0');
            (void)MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_text.data(),
                                      static_cast<int>(utf8_text.size()), wide_text.data(), size);
            return wide_text;
        }

        [[nodiscard]] std::string EscapeJsonString(std::wstring_view value)
        {
            std::string const utf8_value = ToUtf8(value);
            std::string escaped_value;
            escaped_value.reserve(utf8_value.size() + 8);

            for (char const character : utf8_value)
            {
                switch (character)
                {
                case '\\':
                    escaped_value += "\\\\";
                    break;

                case '"':
                    escaped_value += "\\\"";
                    break;

                case '\n':
                    escaped_value += "\\n";
                    break;

                case '\r':
                    escaped_value += "\\r";
                    break;

                case '\t':
                    escaped_value += "\\t";
                    break;

                default:
                    escaped_value.push_back(character);
                    break;
                }
            }

            return escaped_value;
        }

        [[nodiscard]] std::optional<CaptureActionSetting> ParseCaptureActionSetting(std::wstring_view value) noexcept
        {
            if (value == L"copy_only")
            {
                return CaptureActionSetting::CopyOnly;
            }

            if (value == L"save_to_file")
            {
                return CaptureActionSetting::SaveToFile;
            }

            if (value == L"copy_and_pin")
            {
                return CaptureActionSetting::CopyAndPin;
            }

            return std::nullopt;
        }

        [[nodiscard]] std::optional<TrayIconClickActionSetting>
        ParseTrayClickActionSetting(std::wstring_view value) noexcept
        {
            if (value == L"disabled")
            {
                return TrayIconClickActionSetting::Disabled;
            }

            if (value == L"open_menu")
            {
                return TrayIconClickActionSetting::OpenMenu;
            }

            if (value == L"start_capture")
            {
                return TrayIconClickActionSetting::StartCapture;
            }

            return std::nullopt;
        }

        [[nodiscard]] std::string BuildSettingsJson(AppSettings const &settings)
        {
            std::string json_text;
            json_text += "{\n";
            json_text += "  \"version\": ";
            json_text += std::to_string(kSettingsVersion);
            json_text += ",\n";
            json_text += "  \"capture_hotkey_modifiers\": ";
            json_text += std::to_string(settings.capture_hotkey.modifiers);
            json_text += ",\n";
            json_text += "  \"capture_hotkey_virtual_key\": ";
            json_text += std::to_string(settings.capture_hotkey.virtual_key);
            json_text += ",\n";
            json_text += R"(  "default_capture_scope": ")";
            json_text += settings.default_capture_scope == CaptureScopeSetting::FullScreen ? "fullscreen" : "region";
            json_text += "\",\n";
            json_text += R"(  "default_capture_action": ")";

            switch (settings.default_capture_action)
            {
            case CaptureActionSetting::CopyOnly:
                json_text += "copy_only";
                break;

            case CaptureActionSetting::SaveToFile:
                json_text += "save_to_file";
                break;

            case CaptureActionSetting::CopyAndPin:
            default:
                json_text += "copy_and_pin";
                break;
            }

            json_text += "\",\n";
            json_text += R"(  "tray_single_click_action": ")";
            switch (settings.tray_single_click_action)
            {
            case TrayIconClickActionSetting::Disabled:
                json_text += "disabled";
                break;

            case TrayIconClickActionSetting::StartCapture:
                json_text += "start_capture";
                break;

            case TrayIconClickActionSetting::OpenMenu:
            default:
                json_text += "open_menu";
                break;
            }

            json_text += "\",\n";
            json_text += R"(  "tray_double_click_action": ")";
            switch (settings.tray_double_click_action)
            {
            case TrayIconClickActionSetting::OpenMenu:
                json_text += "open_menu";
                break;

            case TrayIconClickActionSetting::StartCapture:
                json_text += "start_capture";
                break;

            case TrayIconClickActionSetting::Disabled:
            default:
                json_text += "disabled";
                break;
            }
            json_text += "\",\n";
            json_text += R"(  "confirm_exit": )";
            json_text += settings.confirm_exit ? "true" : "false";
            json_text += ",\n";
            json_text += R"(  "default_save_directory": ")";
            json_text += EscapeJsonString(settings.default_save_directory);
            json_text += "\",\n";
            json_text += R"(  "default_save_file_prefix": ")";
            json_text += EscapeJsonString(settings.default_save_file_prefix);
            json_text += "\"\n";
            json_text += "}\n";
            return json_text;
        }

        [[nodiscard]] std::size_t FindJsonValueStart(std::string_view json_text, char const *key)
        {
            std::string const quoted_key = "\"" + std::string(key) + "\"";
            std::size_t const key_position = json_text.find(quoted_key);
            if (key_position == std::string_view::npos)
            {
                return std::string_view::npos;
            }

            std::size_t value_position = json_text.find(':', key_position + quoted_key.size());
            if (value_position == std::string_view::npos)
            {
                return std::string_view::npos;
            }

            ++value_position;
            while (value_position < json_text.size() &&
                   std::isspace(static_cast<unsigned char>(json_text.at(value_position))) != 0)
            {
                ++value_position;
            }

            return value_position;
        }

        [[nodiscard]] bool TryReadUnsigned(std::string_view json_text, char const *key, UINT &value)
        {
            std::size_t position = FindJsonValueStart(json_text, key);
            if (position == std::string_view::npos)
            {
                return false;
            }

            std::size_t end_position = position;
            while (end_position < json_text.size() &&
                   std::isdigit(static_cast<unsigned char>(json_text.at(end_position))) != 0)
            {
                ++end_position;
            }

            if (end_position == position)
            {
                return false;
            }

            value = static_cast<UINT>(std::stoul(std::string(json_text.substr(position, end_position - position))));
            return true;
        }

        [[nodiscard]] bool TryReadBoolean(std::string_view json_text, char const *key, bool &value)
        {
            std::size_t const position = FindJsonValueStart(json_text, key);
            if (position == std::string_view::npos)
            {
                return false;
            }

            if (json_text.substr(position, 4) == "true")
            {
                value = true;
                return true;
            }

            if (json_text.substr(position, 5) == "false")
            {
                value = false;
                return true;
            }

            return false;
        }

        [[nodiscard]] bool TryReadString(std::string_view json_text, char const *key, std::wstring &value)
        {
            std::size_t position = FindJsonValueStart(json_text, key);
            if (position == std::string_view::npos || position >= json_text.size() || json_text.at(position) != '"')
            {
                return false;
            }

            ++position;
            std::string utf8_value;

            while (position < json_text.size())
            {
                char const character = json_text.at(position);
                if (character == '"')
                {
                    value = FromUtf8(utf8_value);
                    return true;
                }

                if (character == '\\')
                {
                    ++position;
                    if (position >= json_text.size())
                    {
                        return false;
                    }

                    switch (json_text.at(position))
                    {
                    case '\\':
                    case '"':
                    case '/':
                        utf8_value.push_back(json_text.at(position));
                        break;

                    case 'n':
                        utf8_value.push_back('\n');
                        break;

                    case 'r':
                        utf8_value.push_back('\r');
                        break;

                    case 't':
                        utf8_value.push_back('\t');
                        break;

                    default:
                        return false;
                    }
                }
                else
                {
                    utf8_value.push_back(character);
                }

                ++position;
            }

            return false;
        }

        void NormalizeSettings(AppSettings &settings)
        {
            if (!settings.HasValidCaptureHotkey())
            {
                settings.capture_hotkey = {};
            }

            if (settings.default_save_directory.empty())
            {
                settings.default_save_directory = AppSettingsStore::DefaultSaveDirectory();
            }

            if (settings.default_save_file_prefix.empty())
            {
                settings.default_save_file_prefix = kDefaultFilePrefix;
            }
        }

        [[nodiscard]] AppSettings DefaultSettings()
        {
            AppSettings settings;
            settings.default_save_directory = AppSettingsStore::DefaultSaveDirectory();
            settings.default_save_file_prefix = kDefaultFilePrefix;
            return settings;
        }
    } // namespace

    AppSettings AppSettingsStore::LoadDefaults()
    {
        AppSettings settings = DefaultSettings();
        NormalizeSettings(settings);
        return settings;
    }

    // 配置读取需要集中处理兼容字段和默认值回填，这里保留单入口实现并局部抑制复杂度告警。
    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    AppSettings AppSettingsStore::Load()
    {
        AppSettings settings = LoadDefaults();
        std::filesystem::path const settings_path = SettingsFilePath();
        CAPTUREZY_LOG_DEBUG(LogCategory::Settings, std::wstring(L"Loading settings from ") + settings_path.wstring());
        std::error_code error_code;
        if (std::filesystem::exists(settings_path, error_code))
        {
            std::ifstream settings_stream(settings_path, std::ios::binary);
            std::string settings_json((std::istreambuf_iterator<char>(settings_stream)),
                                      std::istreambuf_iterator<char>());

            UINT numeric_value = 0;
            if (TryReadUnsigned(settings_json, "capture_hotkey_modifiers", numeric_value))
            {
                settings.capture_hotkey.modifiers = numeric_value;
            }

            if (TryReadUnsigned(settings_json, "capture_hotkey_virtual_key", numeric_value))
            {
                settings.capture_hotkey.virtual_key = numeric_value;
            }

            std::wstring string_value;
            if (TryReadString(settings_json, "default_capture_scope", string_value) && string_value == L"fullscreen")
            {
                settings.default_capture_scope = CaptureScopeSetting::FullScreen;
            }

            if (TryReadString(settings_json, "default_capture_action", string_value))
            {
                if (auto const parsed_action = ParseCaptureActionSetting(string_value); parsed_action.has_value())
                {
                    settings.default_capture_action = *parsed_action;
                }
            }

            if (TryReadString(settings_json, "tray_single_click_action", string_value))
            {
                if (auto const parsed_action = ParseTrayClickActionSetting(string_value); parsed_action.has_value())
                {
                    settings.tray_single_click_action = *parsed_action;
                }
            }
            else if (TryReadString(settings_json, "tray_left_click_action", string_value))
            {
                if (auto const parsed_action = ParseTrayClickActionSetting(string_value); parsed_action.has_value())
                {
                    settings.tray_single_click_action = *parsed_action;
                }
            }

            if (TryReadString(settings_json, "tray_double_click_action", string_value))
            {
                if (auto const parsed_action = ParseTrayClickActionSetting(string_value); parsed_action.has_value())
                {
                    settings.tray_double_click_action = *parsed_action;
                }
            }

            bool boolean_value = false;
            if (TryReadBoolean(settings_json, "confirm_exit", boolean_value))
            {
                settings.confirm_exit = boolean_value;
            }

            if (TryReadString(settings_json, "default_save_directory", string_value))
            {
                settings.default_save_directory = string_value;
            }

            if (TryReadString(settings_json, "default_save_file_prefix", string_value))
            {
                settings.default_save_file_prefix = string_value;
            }
        }

        NormalizeSettings(settings);
        CAPTUREZY_LOG_INFO(LogCategory::Settings,
                           std::wstring(L"Loaded settings file. single=") +
                               std::to_wstring(static_cast<int>(settings.tray_single_click_action)) + L", double=" +
                               std::to_wstring(static_cast<int>(settings.tray_double_click_action)) + L".");
        (void)Save(settings);
        return settings;
    }

    bool AppSettingsStore::Save(AppSettings const &settings)
    {
        std::filesystem::path const settings_path = SettingsFilePath();
        std::error_code error_code;
        std::filesystem::create_directories(settings_path.parent_path(), error_code);
        if (error_code)
        {
            CAPTUREZY_LOG_ERROR(LogCategory::Settings, L"Failed to create settings directory.");
            return false;
        }

        std::ofstream settings_stream(settings_path, std::ios::binary | std::ios::trunc);
        if (!settings_stream.is_open())
        {
            CAPTUREZY_LOG_ERROR(LogCategory::Settings, L"Failed to open settings file for writing.");
            return false;
        }

        std::string const settings_json = BuildSettingsJson(settings);
        settings_stream.write(settings_json.data(), static_cast<std::streamsize>(settings_json.size()));
        bool const save_ok = settings_stream.good();
        if (save_ok)
        {
            CAPTUREZY_LOG_DEBUG(LogCategory::Settings,
                                std::wstring(L"Saved settings. single=") +
                                    std::to_wstring(static_cast<int>(settings.tray_single_click_action)) +
                                    L", double=" +
                                    std::to_wstring(static_cast<int>(settings.tray_double_click_action)) + L".");
        }
        else
        {
            CAPTUREZY_LOG_ERROR(LogCategory::Settings,
                                std::wstring(L"Failed while saving settings. single=") +
                                    std::to_wstring(static_cast<int>(settings.tray_single_click_action)) +
                                    L", double=" +
                                    std::to_wstring(static_cast<int>(settings.tray_double_click_action)) + L".");
        }
        return save_ok;
    }

    std::wstring AppSettingsStore::SettingsFilePath()
    {
        std::wstring settings_directory = GetKnownFolderPath(FOLDERID_RoamingAppData);
        if (settings_directory.empty())
        {
            return WideToUtf8FallbackPath(std::filesystem::current_path() / kSettingsDirectoryName / kSettingsFileName);
        }

        return (std::filesystem::path(settings_directory) / kSettingsDirectoryName / kSettingsFileName).wstring();
    }

    std::wstring AppSettingsStore::DefaultSaveDirectory()
    {
        std::wstring pictures_directory = GetKnownFolderPath(FOLDERID_Pictures);
        if (pictures_directory.empty())
        {
            return (std::filesystem::current_path() / kDefaultSaveDirectoryName).wstring();
        }

        return (std::filesystem::path(pictures_directory) / kDefaultSaveDirectoryName).wstring();
    }
} // namespace capturezy::core
