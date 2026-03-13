#pragma once

// clang-format off
#include <windows.h>
// clang-format on

#include <string>
#include <string_view>

namespace capturezy::core
{
    struct CaughtExceptionInfo final
    {
        std::string_view origin;
        std::string_view details;
    };

    class CrashDiagnostics final
    {
      public:
        static void Install() noexcept;
        [[nodiscard]] static std::wstring DiagnosticsDirectory();
        [[nodiscard]] static std::wstring WriteCaughtExceptionReport(CaughtExceptionInfo exception_info) noexcept;
    };
} // namespace capturezy::core
