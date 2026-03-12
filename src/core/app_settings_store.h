#pragma once

#include <string>

#include "core/app_settings.h"

namespace capturezy::core
{
    class AppSettingsStore final
    {
      public:
        [[nodiscard]] static AppSettings Load();
        [[nodiscard]] static AppSettings LoadDefaults();
        [[nodiscard]] static bool Save(AppSettings const &settings);
        [[nodiscard]] static std::wstring SettingsFilePath();
        [[nodiscard]] static std::wstring DefaultSaveDirectory();
    };
} // namespace capturezy::core
