#include "feature_capture/capture_file_dialog.h"

#include <array>
#include <chrono>
#include <commdlg.h>
#include <ctime>
#include <cwchar>
#include <string>

#include "feature_capture/screen_capture.h"

namespace capturezy::feature_capture
{
    namespace
    {
        constexpr auto kSaveDialogFilter = std::to_array(L"PNG Files (*.png)\0*.png\0");

        [[nodiscard]] std::wstring BuildDefaultFileName(CaptureResult::Timestamp captured_at)
        {
            std::time_t const captured_time = std::chrono::system_clock::to_time_t(captured_at);
            std::tm local_time{};
            localtime_s(&local_time, &captured_time);

            std::array<wchar_t, 64> file_name{};
            wcsftime(file_name.data(), file_name.size(), L"CaptureZY_%Y%m%d_%H%M%S.png", &local_time);
            return file_name.data();
        }
    } // namespace

    bool SaveCaptureResultWithPngDialog(HWND owner_window, CaptureResult const &capture_result)
    {
        if (owner_window == nullptr || !capture_result.IsValid())
        {
            return false;
        }

        std::array<wchar_t, 32768> file_path{};
        std::wstring const default_file_name = BuildDefaultFileName(capture_result.CapturedAt());
        wcsncpy_s(file_path.data(), file_path.size(), default_file_name.c_str(), _TRUNCATE);

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
