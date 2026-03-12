#include "feature_capture/capture_file_dialog.h"

#include <array>
#include <chrono>
#include <commdlg.h>
#include <ctime>
#include <cwchar>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include "feature_capture/screen_capture.h"

namespace capturezy::feature_capture
{
    namespace
    {
        constexpr auto kSaveDialogFilter = std::to_array(L"PNG Files (*.png)\0*.png\0");
        constexpr wchar_t const *kDefaultFilePrefix = L"CaptureZY";

        [[nodiscard]] std::wstring SanitizeFileNameFragment(std::wstring_view file_name_fragment)
        {
            std::wstring sanitized_name;
            sanitized_name.reserve(file_name_fragment.size());

            for (wchar_t const character : file_name_fragment)
            {
                switch (character)
                {
                case L'<':
                case L'>':
                case L':':
                case L'"':
                case L'/':
                case L'\\':
                case L'|':
                case L'?':
                case L'*':
                    sanitized_name.push_back(L'_');
                    break;

                default:
                    if (character >= 32)
                    {
                        sanitized_name.push_back(character);
                    }
                    break;
                }
            }

            if (sanitized_name.empty())
            {
                return kDefaultFilePrefix;
            }

            return sanitized_name;
        }

        [[nodiscard]] std::wstring BuildDefaultFileName(CaptureResult::Timestamp captured_at,
                                                        std::wstring_view file_name_prefix)
        {
            std::time_t const captured_time = std::chrono::system_clock::to_time_t(captured_at);
            std::tm local_time{};
            localtime_s(&local_time, &captured_time);

            std::array<wchar_t, 64> file_name{};
            std::wstring const sanitized_prefix = SanitizeFileNameFragment(file_name_prefix);
            wcsftime(file_name.data(), file_name.size(), L"%Y%m%d_%H%M%S", &local_time);
            return sanitized_prefix + L"_" + file_name.data() + L".png";
        }

        [[nodiscard]] bool PathExists(std::filesystem::path const &candidate_path) noexcept
        {
            std::error_code error_code;
            return std::filesystem::exists(candidate_path, error_code) && !error_code;
        }

        [[nodiscard]] std::filesystem::path BuildUniqueSavePath(std::wstring_view directory,
                                                                CaptureResult const &capture_result,
                                                                core::AppSettings const &app_settings)
        {
            std::filesystem::path const base_directory(directory);
            std::wstring const file_name = BuildDefaultFileName(capture_result.CapturedAt(),
                                                                app_settings.default_save_file_prefix);
            std::filesystem::path candidate_path = base_directory / file_name;
            if (!PathExists(candidate_path))
            {
                return candidate_path;
            }

            std::filesystem::path const stem = candidate_path.stem();
            std::filesystem::path const extension = candidate_path.extension();
            for (int suffix = 2; suffix < 1000; ++suffix)
            {
                candidate_path = base_directory /
                                 (stem.wstring() + L"_" + std::to_wstring(suffix) + extension.wstring());
                if (!PathExists(candidate_path))
                {
                    return candidate_path;
                }
            }

            return base_directory / file_name;
        }
    } // namespace

    bool SaveCaptureResultToDefaultPath(CaptureResult const &capture_result, core::AppSettings const &app_settings,
                                        std::wstring *saved_file_path)
    {
        if (!capture_result.IsValid() || app_settings.default_save_directory.empty())
        {
            return false;
        }

        std::filesystem::path const save_directory(app_settings.default_save_directory);
        std::error_code error_code;
        std::filesystem::create_directories(save_directory, error_code);
        if (error_code)
        {
            return false;
        }

        std::filesystem::path const save_path = BuildUniqueSavePath(app_settings.default_save_directory, capture_result,
                                                                    app_settings);
        if (!ScreenCapture::SaveBitmapToPng(capture_result, save_path.c_str()))
        {
            return false;
        }

        if (saved_file_path != nullptr)
        {
            *saved_file_path = save_path.wstring();
        }

        return true;
    }

    bool SaveCaptureResultWithPngDialog(HWND owner_window, CaptureResult const &capture_result,
                                        core::AppSettings const &app_settings)
    {
        if (owner_window == nullptr || !capture_result.IsValid())
        {
            return false;
        }

        std::array<wchar_t, 32768> file_path{};
        std::wstring const default_file_name = BuildDefaultFileName(capture_result.CapturedAt(),
                                                                    app_settings.default_save_file_prefix);
        std::filesystem::path initial_path(default_file_name);
        if (!app_settings.default_save_directory.empty())
        {
            std::error_code error_code;
            std::filesystem::create_directories(app_settings.default_save_directory, error_code);
            if (!error_code)
            {
                initial_path = std::filesystem::path(app_settings.default_save_directory) / default_file_name;
            }
        }

        wcsncpy_s(file_path.data(), file_path.size(), initial_path.c_str(), _TRUNCATE);

        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = owner_window;
        dialog.lpstrFilter = kSaveDialogFilter.data();
        dialog.lpstrFile = file_path.data();
        dialog.nMaxFile = static_cast<DWORD>(file_path.size());
        dialog.lpstrDefExt = L"png";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;

        if (GetSaveFileNameW(&dialog) == FALSE)
        {
            return false;
        }

        return ScreenCapture::SaveBitmapToPng(capture_result, file_path.data());
    }
} // namespace capturezy::feature_capture
