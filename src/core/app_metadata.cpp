#include "core/app_metadata.h"

namespace capturezy::core
{
    wchar_t const *AppMetadata::ProductName() noexcept
    {
        return L"CaptureZY";
    }

    wchar_t const *AppMetadata::MainWindowClassName() noexcept
    {
        return L"CaptureZY.MainWindow";
    }

    Size AppMetadata::MainWindowSize() noexcept
    {
        return {
            .width = 420,
            .height = 160,
        };
    }
} // namespace capturezy::core
