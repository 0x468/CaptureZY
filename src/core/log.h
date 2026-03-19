#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace capturezy::core
{
    namespace LogCategory
    {
        constexpr std::wstring_view App = L"app";
        constexpr std::wstring_view Capture = L"capture";
        constexpr std::wstring_view Platform = L"platform";
        constexpr std::wstring_view Settings = L"settings";
        constexpr std::wstring_view SettingsDialog = L"settings_dialog";
        constexpr std::wstring_view Tray = L"tray";
    } // namespace LogCategory

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
        [[nodiscard]] static LogConfig DefaultConfig() noexcept;
        static void Initialize() noexcept;
        static void Initialize(LogConfig config) noexcept;
        static void Shutdown() noexcept;
        static void SetMinimumLevel(LogLevel level) noexcept;
        [[nodiscard]] static std::wstring LogFilePath();
        [[nodiscard]] static std::wstring RotatedLogFilePath();
        static void Write(LogLevel level, std::wstring_view category, std::wstring_view message) noexcept;
        static void Write(LogLevel level, std::wstring_view category, std::string_view message) noexcept;
    };
} // namespace capturezy::core

#define CAPTUREZY_LOG_ERROR(category, message)                                                                         \
    ::capturezy::core::Log::Write(::capturezy::core::LogLevel::Error, category, message)
#define CAPTUREZY_LOG_WARNING(category, message)                                                                       \
    ::capturezy::core::Log::Write(::capturezy::core::LogLevel::Warning, category, message)
#define CAPTUREZY_LOG_INFO(category, message)                                                                          \
    ::capturezy::core::Log::Write(::capturezy::core::LogLevel::Info, category, message)
#define CAPTUREZY_LOG_DEBUG(category, message)                                                                         \
    ::capturezy::core::Log::Write(::capturezy::core::LogLevel::Debug, category, message)
#define CAPTUREZY_LOG_TRACE(category, message)                                                                         \
    ::capturezy::core::Log::Write(::capturezy::core::LogLevel::Trace, category, message)
