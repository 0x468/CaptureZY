#include "core/log.h"

#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

// clang-format off
#include <windows.h>
#include <shlobj.h>
// clang-format on

#include "core/app_metadata.h"

namespace capturezy::core
{
    namespace
    {
        constexpr wchar_t const *kLogsDirectoryName = L"Logs";
        constexpr wchar_t const *kLogFileName = L"capturezy.log";
        constexpr wchar_t const *kRotatedLogFileName = L"capturezy.log.1";

        struct LogState final
        {
            LogConfig config{};
            bool initialized{false};
            std::mutex mutex;
        };

        [[nodiscard]] LogState &State() noexcept
        {
            static LogState state{};
            return state;
        }

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

        [[nodiscard]] std::filesystem::path CurrentPathFallback()
        {
            std::error_code error_code;
            std::filesystem::path const current_path = std::filesystem::current_path(error_code);
            return error_code ? std::filesystem::path(L".") : current_path;
        }

        [[nodiscard]] std::filesystem::path LogsDirectoryPath()
        {
            std::wstring const local_app_data = GetKnownFolderPath(FOLDERID_LocalAppData);
            if (local_app_data.empty())
            {
                return CurrentPathFallback() / AppMetadata::ProductName() / kLogsDirectoryName;
            }

            return std::filesystem::path(local_app_data) / AppMetadata::ProductName() / kLogsDirectoryName;
        }

        [[nodiscard]] std::wstring WidenUtf8(std::string_view utf8_text)
        {
            if (utf8_text.empty())
            {
                return {};
            }

            int const size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_text.data(),
                                                 static_cast<int>(utf8_text.size()), nullptr, 0);
            if (size <= 0)
            {
                return L"<utf8-conversion-failed>";
            }

            std::wstring wide_text(static_cast<std::size_t>(size), L'\0');
            (void)MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_text.data(),
                                      static_cast<int>(utf8_text.size()), wide_text.data(), size);
            return wide_text;
        }

        [[nodiscard]] std::string ToUtf8(std::wstring_view wide_text)
        {
            if (wide_text.empty())
            {
                return {};
            }

            int const size = WideCharToMultiByte(CP_UTF8, 0, wide_text.data(), static_cast<int>(wide_text.size()),
                                                 nullptr, 0, nullptr, nullptr);
            if (size <= 0)
            {
                return {};
            }

            std::string utf8_text(static_cast<std::size_t>(size), '\0');
            (void)WideCharToMultiByte(CP_UTF8, 0, wide_text.data(), static_cast<int>(wide_text.size()),
                                      utf8_text.data(), size, nullptr, nullptr);
            return utf8_text;
        }

        [[nodiscard]] wchar_t const *LevelName(LogLevel level) noexcept
        {
            switch (level)
            {
            case LogLevel::Error:
                return L"ERROR";

            case LogLevel::Warning:
                return L"WARN";

            case LogLevel::Debug:
                return L"DEBUG";

            case LogLevel::Trace:
                return L"TRACE";

            case LogLevel::Info:
            default:
                return L"INFO";
            }
        }

        [[nodiscard]] std::wstring TimestampString()
        {
            SYSTEMTIME local_time{};
            GetLocalTime(&local_time);
            return std::format(L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}", local_time.wYear, local_time.wMonth,
                               local_time.wDay, local_time.wHour, local_time.wMinute, local_time.wSecond,
                               local_time.wMilliseconds);
        }

        [[nodiscard]] std::wstring BuildLogLine(LogLevel level, std::wstring_view category, std::wstring_view message)
        {
            std::wstring line = L"[";
            line += TimestampString();
            line += L"] [";
            line += LevelName(level);
            line += L"] [";
            line += category;
            line += L"] ";
            line += message;
            line += L"\r\n";
            return line;
        }

        [[nodiscard]] std::uintmax_t CurrentLogFileSize(std::filesystem::path const &log_path) noexcept
        {
            WIN32_FILE_ATTRIBUTE_DATA file_data{};
            if (GetFileAttributesExW(log_path.c_str(), GetFileExInfoStandard, &file_data) == FALSE)
            {
                return 0;
            }

            ULARGE_INTEGER file_size{};
            file_size.HighPart = file_data.nFileSizeHigh;
            file_size.LowPart = file_data.nFileSizeLow;
            return file_size.QuadPart;
        }

        void EnsureLogFileReady(LogConfig const &config)
        {
            std::filesystem::path const log_path = Log::LogFilePath();
            std::filesystem::path const rotated_log_path = Log::RotatedLogFilePath();
            std::error_code error_code;
            std::filesystem::create_directories(log_path.parent_path(), error_code);
            if (error_code)
            {
                return;
            }

            if (config.max_file_size_bytes == 0)
            {
                return;
            }

            std::uintmax_t const file_size = CurrentLogFileSize(log_path);
            if (file_size < config.max_file_size_bytes)
            {
                return;
            }

            std::filesystem::remove(rotated_log_path, error_code);
            error_code.clear();
            std::filesystem::rename(log_path, rotated_log_path, error_code);
            if (!error_code)
            {
                return;
            }

            std::ofstream truncate_stream(log_path, std::ios::binary | std::ios::trunc);
            (void)truncate_stream;
        }

        [[nodiscard]] bool IsEnabled(LogLevel level, LogConfig const &config) noexcept
        {
            return static_cast<int>(level) <= static_cast<int>(config.minimum_level);
        }
    } // namespace

    LogConfig Log::DefaultConfig() noexcept
    {
        LogConfig config{};
#ifdef _DEBUG
        config.minimum_level = LogLevel::Debug;
#else
        config.minimum_level = LogLevel::Info;
#endif
        return config;
    }

    void Log::Initialize() noexcept
    {
        Initialize(DefaultConfig());
    }

    void Log::Initialize(LogConfig config) noexcept
    {
        try
        {
            LogState &state = State();
            std::scoped_lock lock(state.mutex);
            state.config = config;
            EnsureLogFileReady(state.config);
            state.initialized = true;
        }
        catch (...)
        {
            OutputDebugStringW(L"CaptureZY: log initialization failed.\n");
        }
    }

    void Log::Shutdown() noexcept
    {
        LogState &state = State();
        std::scoped_lock lock(state.mutex);
        state.initialized = false;
    }

    void Log::SetMinimumLevel(LogLevel level) noexcept
    {
        LogState &state = State();
        std::scoped_lock lock(state.mutex);
        state.config.minimum_level = level;
    }

    std::wstring Log::LogFilePath()
    {
        return (LogsDirectoryPath() / kLogFileName).wstring();
    }

    std::wstring Log::RotatedLogFilePath()
    {
        return (LogsDirectoryPath() / kRotatedLogFileName).wstring();
    }

    void Log::Write(LogLevel level, std::wstring_view category, std::wstring_view message) noexcept
    {
        try
        {
            LogState &state = State();
            std::scoped_lock lock(state.mutex);
            if (!state.initialized)
            {
                state.config = {};
                EnsureLogFileReady(state.config);
                state.initialized = true;
            }

            if (!IsEnabled(level, state.config))
            {
                return;
            }

            std::wstring const line = BuildLogLine(level, category, message);
            if (state.config.debugger_output_enabled)
            {
                OutputDebugStringW(line.c_str());
            }

            if (!state.config.file_output_enabled)
            {
                return;
            }

            EnsureLogFileReady(state.config);
            std::ofstream stream(LogFilePath(), std::ios::binary | std::ios::app);
            if (!stream.is_open())
            {
                return;
            }

            std::string const utf8_line = ToUtf8(line);
            stream.write(utf8_line.data(), static_cast<std::streamsize>(utf8_line.size()));
        }
        catch (...)
        {
            OutputDebugStringW(L"CaptureZY: file logging failed.\n");
        }
    }

    void Log::Write(LogLevel level, std::wstring_view category, std::string_view message) noexcept
    {
        try
        {
            Write(level, category, WidenUtf8(message));
        }
        catch (...)
        {
            OutputDebugStringW(L"CaptureZY: utf8 log conversion failed.\n");
        }
    }
} // namespace capturezy::core
