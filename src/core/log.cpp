#include "core/log.h"

#include <algorithm>
#include <cwctype>
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
        constexpr wchar_t const *kLogLevelEnvVar = L"CAPTUREZY_LOG_LEVEL";
        constexpr wchar_t const *kLogDebuggerEnvVar = L"CAPTUREZY_LOG_DEBUGGER";
        constexpr wchar_t const *kLogFileEnvVar = L"CAPTUREZY_LOG_FILE";
        constexpr wchar_t const *kLogMaxSizeKbEnvVar = L"CAPTUREZY_LOG_MAX_KB";
        constexpr wchar_t const *kLogMaxFilesEnvVar = L"CAPTUREZY_LOG_MAX_FILES";

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

        [[nodiscard]] std::filesystem::path LogFilePathObject()
        {
            return LogsDirectoryPath() / kLogFileName;
        }

        [[nodiscard]] std::filesystem::path RotatedLogFilePathObject(std::size_t index)
        {
            return LogsDirectoryPath() / std::format(L"{}.{}", kLogFileName, index);
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

        [[nodiscard]] std::wstring ReadEnvironmentVariable(wchar_t const *name)
        {
            DWORD const length = GetEnvironmentVariableW(name, nullptr, 0);
            if (length == 0)
            {
                return {};
            }

            std::wstring value(static_cast<std::size_t>(length), L'\0');
            DWORD const copied = GetEnvironmentVariableW(name, value.data(), length);
            if (copied == 0)
            {
                return {};
            }

            value.resize(static_cast<std::size_t>(copied));
            return value;
        }

        [[nodiscard]] bool EqualsIgnoreCase(std::wstring_view left, std::wstring_view right) noexcept
        {
            if (left.size() != right.size())
            {
                return false;
            }

            auto left_it = left.begin();
            auto right_it = right.begin();
            for (; left_it != left.end(); ++left_it, ++right_it)
            {
                if (std::towlower(*left_it) != std::towlower(*right_it))
                {
                    return false;
                }
            }

            return true;
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

        [[nodiscard]] LogLevel ParseLogLevel(std::wstring_view value, LogLevel fallback) noexcept
        {
            if (EqualsIgnoreCase(value, L"error"))
            {
                return LogLevel::Error;
            }

            if (EqualsIgnoreCase(value, L"warn") || EqualsIgnoreCase(value, L"warning"))
            {
                return LogLevel::Warning;
            }

            if (EqualsIgnoreCase(value, L"debug"))
            {
                return LogLevel::Debug;
            }

            if (EqualsIgnoreCase(value, L"trace"))
            {
                return LogLevel::Trace;
            }

            if (EqualsIgnoreCase(value, L"info"))
            {
                return LogLevel::Info;
            }

            return fallback;
        }

        [[nodiscard]] bool ParseBoolean(std::wstring_view value, bool fallback) noexcept
        {
            if (value == L"1" || EqualsIgnoreCase(value, L"true") || EqualsIgnoreCase(value, L"on") ||
                EqualsIgnoreCase(value, L"yes"))
            {
                return true;
            }

            if (value == L"0" || EqualsIgnoreCase(value, L"false") || EqualsIgnoreCase(value, L"off") ||
                EqualsIgnoreCase(value, L"no"))
            {
                return false;
            }

            return fallback;
        }

        [[nodiscard]] std::size_t ParseSizeKb(std::wstring_view value, std::size_t fallback) noexcept
        {
            try
            {
                std::size_t parsed = std::stoull(std::wstring(value));
                return parsed * 1024U;
            }
            catch (...)
            {
                return fallback;
            }
        }

        [[nodiscard]] std::size_t ParseCount(std::wstring_view value, std::size_t fallback) noexcept
        {
            try
            {
                return std::stoull(std::wstring(value));
            }
            catch (...)
            {
                return fallback;
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

        void RotateLogFiles(std::filesystem::path const &log_path, LogConfig const &config)
        {
            std::error_code error_code;
            if (config.max_retained_files == 0)
            {
                std::ofstream truncate_stream(log_path, std::ios::binary | std::ios::trunc);
                (void)truncate_stream;
                return;
            }

            for (std::size_t index = config.max_retained_files; index >= 1; --index)
            {
                std::filesystem::path const source_path = index == 1 ? log_path : RotatedLogFilePathObject(index - 1);
                std::filesystem::path const target_path = RotatedLogFilePathObject(index);

                if (!std::filesystem::exists(source_path, error_code))
                {
                    error_code.clear();
                    if (index == 1)
                    {
                        break;
                    }

                    continue;
                }
                error_code.clear();

                std::filesystem::remove(target_path, error_code);
                error_code.clear();
                std::filesystem::rename(source_path, target_path, error_code);
                if (error_code)
                {
                    if (index == 1)
                    {
                        std::ofstream truncate_stream(log_path, std::ios::binary | std::ios::trunc);
                        (void)truncate_stream;
                    }
                    return;
                }

                if (index == 1)
                {
                    break;
                }
            }
        }

        void EnsureLogFileReady(LogConfig const &config)
        {
            std::filesystem::path const log_path = LogFilePathObject();
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

            RotateLogFiles(log_path, config);
        }

        [[nodiscard]] bool IsEnabled(LogLevel level, LogConfig const &config) noexcept
        {
            return static_cast<int>(level) <= static_cast<int>(config.minimum_level);
        }
    } // namespace

    LogConfig Log::DefaultConfig() noexcept
    {
        try
        {
            LogConfig config{};
#ifdef _DEBUG
            config.minimum_level = LogLevel::Debug;
#else
            config.minimum_level = LogLevel::Info;
            config.debugger_output_enabled = false;
#endif
            config.minimum_level = ParseLogLevel(ReadEnvironmentVariable(kLogLevelEnvVar), config.minimum_level);
            config.debugger_output_enabled = ParseBoolean(ReadEnvironmentVariable(kLogDebuggerEnvVar),
                                                          config.debugger_output_enabled);
            config.file_output_enabled = ParseBoolean(ReadEnvironmentVariable(kLogFileEnvVar),
                                                      config.file_output_enabled);
            config.max_file_size_bytes = ParseSizeKb(ReadEnvironmentVariable(kLogMaxSizeKbEnvVar),
                                                     config.max_file_size_bytes);
            config.max_retained_files = ParseCount(ReadEnvironmentVariable(kLogMaxFilesEnvVar),
                                                   config.max_retained_files);
            return config;
        }
        catch (...)
        {
            LogConfig fallback{};
#ifdef _DEBUG
            fallback.minimum_level = LogLevel::Debug;
#else
            fallback.minimum_level = LogLevel::Info;
            fallback.debugger_output_enabled = false;
#endif
            return fallback;
        }
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
        return LogFilePathObject().wstring();
    }

    std::wstring Log::RotatedLogFilePath(std::size_t index)
    {
        return RotatedLogFilePathObject(std::max<std::size_t>(1, index)).wstring();
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
            std::ofstream stream(LogFilePathObject(), std::ios::binary | std::ios::app);
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
