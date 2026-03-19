#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace capturezy::core
{
    enum class LogLevel : std::uint8_t
    {
        Error,
        Warning,
        Info,
        Debug,
        Trace,
    };

    struct LogConfig final
    {
        LogLevel minimum_level{LogLevel::Info};
        bool debugger_output_enabled{true};
        bool file_output_enabled{true};
        std::size_t max_file_size_bytes{1024U * 1024U};
    };

    class Log final
    {
      public:
        static void Initialize(LogConfig config = {}) noexcept;
        static void Shutdown() noexcept;
        static void SetMinimumLevel(LogLevel level) noexcept;
        [[nodiscard]] static std::wstring LogFilePath();
        static void Write(LogLevel level, std::wstring_view category, std::wstring_view message) noexcept;
        static void Write(LogLevel level, std::wstring_view category, std::string_view message) noexcept;
    };
} // namespace capturezy::core
