#include "feature_pin/pin_state_store.h"

#include <filesystem>
#include <fstream>
#include <format>
#include <shlobj.h>
#include <string>
#include <string_view>

#include "core/log.h"

namespace capturezy::feature_pin
{
    namespace
    {
        constexpr wchar_t const *kPinStateDirectoryName = L"CaptureZY\\pins";
        constexpr wchar_t const *kPinStateFileName = L"pins.json";
        constexpr wchar_t const *kPinImageDirectoryName = L"images";

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

        // NOLINTNEXTLINE(readability-function-cognitive-complexity)
        [[nodiscard]] std::string BuildPinStatesJson(std::vector<PinState> const &pin_states)
        {
            std::string json = "{\n  \"pins\": [\n";
            for (std::size_t index = 0; index < pin_states.size(); ++index)
            {
                auto const &state = pin_states[index];
                json += "    {\n";

                // 手动拼接 JSON 字段（image_path 是 wstring，不能直接 std::format）
                std::string utf8_image_path;
                for (wchar_t wc : state.image_path)
                {
                    if (wc < 128)
                    {
                        utf8_image_path += static_cast<char>(wc);
                    }
                    else
                    {
                        // 简单 UTF-8 编码（3字节序列）
                        utf8_image_path += static_cast<char>(0xE0 | ((wc >> 12) & 0x0F));
                        utf8_image_path += static_cast<char>(0x80 | ((wc >> 6) & 0x3F));
                        utf8_image_path += static_cast<char>(0x80 | (wc & 0x3F));
                    }
                }
                json += "      \"image_path\": \"" + utf8_image_path + "\",\n";
                json += "      \"position_x\": " + std::to_string(state.position_x) + ",\n";
                json += "      \"position_y\": " + std::to_string(state.position_y) + ",\n";
                json += "      \"scale_percent\": " + std::to_string(state.scale_percent) + ",\n";
                json += "      \"opacity_percent\": " + std::to_string(state.opacity_percent) + ",\n";
                json += "      \"topmost\": " + std::string(state.topmost ? "true" : "false") + ",\n";
                json += "      \"shadow_enabled\": " + std::string(state.shadow_enabled ? "true" : "false") + ",\n";
                json += "      \"click_through\": " + std::string(state.click_through ? "true" : "false") + ",\n";
                json += "      \"locked\": " + std::string(state.locked ? "true" : "false") + ",\n";
                json += "      \"visible\": " + std::string(state.visible ? "true" : "false") + "\n";
                json += "    }";
                if (index + 1 < pin_states.size())
                {
                    json += ",";
                }
                json += "\n";
            }
            json += "  ]\n}\n";
            return json;
        }

        [[nodiscard]] bool TryReadString(std::string_view json, std::string_view key, std::wstring &out_value)
        {
            std::string search_key = std::string("\"") + std::string(key) + std::string("\"");
            auto key_pos = json.find(search_key);
            if (key_pos == std::string_view::npos)
            {
                return false;
            }

            auto value_start = json.find('"', key_pos + search_key.size());
            if (value_start == std::string_view::npos)
            {
                return false;
            }

            auto value_end = json.find('"', value_start + 1);
            if (value_end == std::string_view::npos)
            {
                return false;
            }

            std::string utf8_value(json.substr(value_start + 1, value_end - value_start - 1));
            // 简单转换：对 ASCII 范围内的值直接扩展为 wchar_t
            out_value.resize(utf8_value.size());
            for (std::size_t i = 0; i < utf8_value.size(); ++i)
            {
                out_value[i] = static_cast<wchar_t>(utf8_value[i]);
            }
            return true;
        }

        [[nodiscard]] bool TryReadInt(std::string_view json, std::string_view key, std::int32_t &out_value)
        {
            std::string search_key = std::string("\"") + std::string(key) + std::string("\"");
            auto key_pos = json.find(search_key);
            if (key_pos == std::string_view::npos)
            {
                return false;
            }

            auto colon_pos = json.find(':', key_pos + search_key.size());
            if (colon_pos == std::string_view::npos)
            {
                return false;
            }

            auto value_start = colon_pos + 1;
            while (value_start < json.size() && (json[value_start] == ' ' || json[value_start] == '\t'))
            {
                ++value_start;
            }

            bool is_negative = false;
            if (value_start < json.size() && json[value_start] == '-')
            {
                is_negative = true;
                ++value_start;
            }

            std::int32_t numeric_value = 0;
            while (value_start < json.size() && json[value_start] >= '0' && json[value_start] <= '9')
            {
                numeric_value = numeric_value * 10 + (json[value_start] - '0');
                ++value_start;
            }

            out_value = is_negative ? -numeric_value : numeric_value;
            return true;
        }

        [[nodiscard]] bool TryReadBoolean(std::string_view json, std::string_view key, bool &out_value)
        {
            std::string search_key = std::string("\"") + std::string(key) + std::string("\"");
            auto key_pos = json.find(search_key);
            if (key_pos == std::string_view::npos)
            {
                return false;
            }

            auto colon_pos = json.find(':', key_pos + search_key.size());
            if (colon_pos == std::string_view::npos)
            {
                return false;
            }

            auto value_start = colon_pos + 1;
            while (value_start < json.size() && (json[value_start] == ' ' || json[value_start] == '\t'))
            {
                ++value_start;
            }

            if (json.substr(value_start, 4) == "true")
            {
                out_value = true;
                return true;
            }
            if (json.substr(value_start, 5) == "false")
            {
                out_value = false;
                return true;
            }
            return false;
        }
    } // namespace

    std::filesystem::path PinStateStore::PinStateDirectory()
    {
        std::wstring app_data = GetKnownFolderPath(FOLDERID_RoamingAppData);
        if (app_data.empty())
        {
            return std::filesystem::current_path() / kPinStateDirectoryName;
        }
        return std::filesystem::path(app_data) / kPinStateDirectoryName;
    }

    std::filesystem::path PinStateStore::PinStateFilePath()
    {
        return PinStateDirectory() / kPinStateFileName;
    }

    bool PinStateStore::SavePinStates(std::vector<PinState> const &pin_states)
    {
        std::filesystem::path const state_dir = PinStateDirectory();
        std::filesystem::path const images_dir = state_dir / kPinImageDirectoryName;
        std::error_code error_code;
        std::filesystem::create_directories(images_dir, error_code);
        if (error_code)
        {
            CAPTUREZY_LOG_ERROR(core::LogCategory::Pin, L"Failed to create pin state directory.");
            return false;
        }

        std::filesystem::path const state_path = PinStateFilePath();
        std::ofstream state_stream(state_path, std::ios::binary | std::ios::trunc);
        if (!state_stream.is_open())
        {
            CAPTUREZY_LOG_ERROR(core::LogCategory::Pin, L"Failed to open pin state file for writing.");
            return false;
        }

        std::string const state_json = BuildPinStatesJson(pin_states);
        state_stream.write(state_json.data(), static_cast<std::streamsize>(state_json.size()));
        bool const save_ok = state_stream.good();

        if (save_ok)
        {
            try
            {
                std::wstring save_msg =
                    std::format(L"Saved {} pin states.", static_cast<std::uint32_t>(pin_states.size()));
                CAPTUREZY_LOG_INFO(core::LogCategory::Pin, save_msg);
            }
            catch (...)
            {
                CAPTUREZY_LOG_INFO(core::LogCategory::Pin, L"Saved pin states.");
            }
        }
        else
        {
            CAPTUREZY_LOG_ERROR(core::LogCategory::Pin, L"Failed while saving pin states.");
        }
        return save_ok;
    }

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    std::vector<PinState> PinStateStore::LoadPinStates()
    {
        std::vector<PinState> pin_states;
        std::filesystem::path const state_path = PinStateFilePath();
        std::error_code error_code;
        if (!std::filesystem::exists(state_path, error_code))
        {
            CAPTUREZY_LOG_DEBUG(core::LogCategory::Pin, L"No pin state file found, returning empty.");
            return pin_states;
        }

        std::ifstream state_stream(state_path, std::ios::binary);
        std::string state_json((std::istreambuf_iterator<char>(state_stream)), std::istreambuf_iterator<char>());

        // 解析 JSON 数组中的每个 pin 对象
        auto pins_start = state_json.find("\"pins\"");
        if (pins_start == std::string::npos)
        {
            return pin_states;
        }

        auto array_start = state_json.find('[', pins_start);
        if (array_start == std::string::npos)
        {
            return pin_states;
        }

        // 遍历数组中的每个对象
        std::size_t pos = array_start + 1;
        while (pos < state_json.size())
        {
            auto obj_start = state_json.find('{', pos);
            if (obj_start == std::string::npos)
            {
                break;
            }

            auto obj_end = state_json.find('}', obj_start);
            if (obj_end == std::string::npos)
            {
                break;
            }

            std::string_view obj_view(state_json.data() + obj_start, obj_end - obj_start + 1);
            PinState state;

            (void)TryReadString(obj_view, "image_path", state.image_path);
            (void)TryReadInt(obj_view, "position_x", state.position_x);
            (void)TryReadInt(obj_view, "position_y", state.position_y);
            (void)TryReadInt(obj_view, "scale_percent", state.scale_percent);
            (void)TryReadInt(obj_view, "opacity_percent", state.opacity_percent);
            (void)TryReadBoolean(obj_view, "topmost", state.topmost);
            (void)TryReadBoolean(obj_view, "shadow_enabled", state.shadow_enabled);
            (void)TryReadBoolean(obj_view, "click_through", state.click_through);
            (void)TryReadBoolean(obj_view, "locked", state.locked);
            (void)TryReadBoolean(obj_view, "visible", state.visible);

            pin_states.push_back(std::move(state));
            pos = obj_end + 1;
        }

        try
        {
            std::wstring load_msg =
                std::format(L"Loaded {} pin states.", static_cast<std::uint32_t>(pin_states.size()));
            CAPTUREZY_LOG_INFO(core::LogCategory::Pin, load_msg);
        }
        catch (...)
        {
            CAPTUREZY_LOG_INFO(core::LogCategory::Pin, L"Loaded pin states.");
        }
        return pin_states;
    }

    bool PinStateStore::ClearPinStateDirectory()
    {
        std::filesystem::path const state_dir = PinStateDirectory();
        std::error_code error_code;
        std::filesystem::remove_all(state_dir, error_code);
        if (error_code)
        {
            CAPTUREZY_LOG_ERROR(core::LogCategory::Pin, L"Failed to clear pin state directory.");
            return false;
        }
        CAPTUREZY_LOG_INFO(core::LogCategory::Pin, L"Pin state directory cleared.");
        return true;
    }
} // namespace capturezy::feature_pin
