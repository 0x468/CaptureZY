#include "core/app_settings.h"

namespace capturezy::core
{
    bool AppSettings::HasValidCaptureHotkey() const noexcept
    {
        return capture_hotkey.virtual_key != 0;
    }
} // namespace capturezy::core
