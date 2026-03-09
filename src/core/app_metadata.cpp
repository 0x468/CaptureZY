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
            .width = 1280,
            .height = 768,
        };
    }
} // namespace capturezy::core
