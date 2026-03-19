#include "core/crash_diagnostics.h"

#include <cstdlib>
#include <dbghelp.h>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <shlobj.h>
#include <sstream>
#include <string>
#include <system_error>

#include "core/app_metadata.h"
#include "core/log.h"

namespace capturezy::core
{
    namespace
    {
        constexpr wchar_t const *kDiagnosticsDirectoryName = L"Diagnostics";

        struct TextReportSpec final
        {
            std::wstring_view base_name;
            std::wstring_view contents;
        };

        struct CaughtExceptionReportSpec final
        {
            std::wstring_view origin;
            std::wstring_view details;
        };

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

        [[nodiscard]] std::filesystem::path DiagnosticsDirectoryPath()
        {
            std::wstring const local_app_data_directory = GetKnownFolderPath(FOLDERID_LocalAppData);
            if (local_app_data_directory.empty())
            {
                return CurrentPathFallback() / AppMetadata::ProductName() / kDiagnosticsDirectoryName;
            }

            return std::filesystem::path(local_app_data_directory) / AppMetadata::ProductName() /
                   kDiagnosticsDirectoryName;
        }

        void EnsureDiagnosticsDirectory()
        {
            std::error_code error_code;
            std::filesystem::create_directories(DiagnosticsDirectoryPath(), error_code);
        }

        [[nodiscard]] std::wstring LocalTimestampString()
        {
            SYSTEMTIME local_time{};
            GetLocalTime(&local_time);

            std::wostringstream stream;
            stream << std::setfill(L'0') << std::setw(4) << local_time.wYear << std::setw(2) << local_time.wMonth
                   << std::setw(2) << local_time.wDay << L"-" << std::setw(2) << local_time.wHour << std::setw(2)
                   << local_time.wMinute << std::setw(2) << local_time.wSecond;
            return stream.str();
        }

        [[nodiscard]] std::wstring HexValueString(unsigned long value)
        {
            std::wostringstream stream;
            stream << std::uppercase << std::hex << std::setfill(L'0') << std::setw(8) << value;
            return stream.str();
        }

        [[nodiscard]] std::wstring PointerValueString(void const *value)
        {
            std::wostringstream stream;
            stream << value;
            return stream.str();
        }

        [[nodiscard]] std::wstring BuildDiagnosticBaseName(std::wstring_view prefix)
        {
            std::wostringstream stream;
            stream << prefix << L"-" << LocalTimestampString() << L"-p" << GetCurrentProcessId() << L"-t"
                   << GetCurrentThreadId();
            return stream.str();
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

        [[nodiscard]] std::filesystem::path BuildReportPath(std::wstring_view base_name, wchar_t const *extension)
        {
            return DiagnosticsDirectoryPath() / (std::wstring(base_name) + extension);
        }

        [[nodiscard]] std::filesystem::path WriteTextReport(TextReportSpec const &report_spec)
        {
            EnsureDiagnosticsDirectory();
            std::filesystem::path report_path = BuildReportPath(report_spec.base_name, L".txt");

            std::wofstream report_stream(report_path, std::ios::binary | std::ios::trunc);
            if (!report_stream.is_open())
            {
                return {};
            }

            report_stream << report_spec.contents;
            if (!report_stream.good())
            {
                return {};
            }

            return report_path;
        }

        [[nodiscard]] bool WriteMiniDump(std::wstring_view base_name, EXCEPTION_POINTERS *exception_pointers)
        {
            if (exception_pointers == nullptr)
            {
                return false;
            }

            EnsureDiagnosticsDirectory();
            std::filesystem::path const dump_path = BuildReportPath(base_name, L".dmp");

            HANDLE dump_file = CreateFileW(dump_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                           FILE_ATTRIBUTE_NORMAL, nullptr);
            if (dump_file == INVALID_HANDLE_VALUE)
            {
                return false;
            }

            MINIDUMP_EXCEPTION_INFORMATION exception_information{};
            exception_information.ThreadId = GetCurrentThreadId();
            exception_information.ExceptionPointers = exception_pointers;
            exception_information.ClientPointers = FALSE;

            BOOL const dump_written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump_file,
                                                        MiniDumpNormal, &exception_information, nullptr, nullptr);
            CloseHandle(dump_file);
            return dump_written != FALSE;
        }

        [[nodiscard]] std::wstring BuildUnhandledExceptionReportText(std::wstring_view base_name,
                                                                     EXCEPTION_POINTERS *exception_pointers,
                                                                     bool dump_written)
        {
            std::wostringstream stream;
            stream << L"product: " << AppMetadata::ProductName() << L"\n";
            stream << L"type: unhandled_exception\n";
            stream << L"timestamp: " << LocalTimestampString() << L"\n";
            stream << L"process_id: " << GetCurrentProcessId() << L"\n";
            stream << L"thread_id: " << GetCurrentThreadId() << L"\n";
            stream << L"session_id: " << Log::SessionId() << L"\n";
            stream << L"report_base: " << base_name << L"\n";

            if (exception_pointers != nullptr && exception_pointers->ExceptionRecord != nullptr)
            {
                stream << L"exception_code: 0x" << HexValueString(exception_pointers->ExceptionRecord->ExceptionCode)
                       << L"\n";
                stream << L"exception_address: "
                       << PointerValueString(exception_pointers->ExceptionRecord->ExceptionAddress) << L"\n";
            }

            stream << L"minidump_written: " << (dump_written ? L"true" : L"false") << L"\n";
            stream << L"log_file: " << Log::LogFilePath() << L"\n";
            stream << L"diagnostics_directory: " << CrashDiagnostics::DiagnosticsDirectory() << L"\n";
            return stream.str();
        }

        [[nodiscard]] std::wstring BuildCaughtExceptionReportText(CaughtExceptionReportSpec const &report_spec)
        {
            std::wostringstream stream;
            stream << L"product: " << AppMetadata::ProductName() << L"\n";
            stream << L"type: caught_top_level_exception\n";
            stream << L"timestamp: " << LocalTimestampString() << L"\n";
            stream << L"process_id: " << GetCurrentProcessId() << L"\n";
            stream << L"thread_id: " << GetCurrentThreadId() << L"\n";
            stream << L"session_id: " << Log::SessionId() << L"\n";
            stream << L"origin: " << report_spec.origin << L"\n";
            stream << L"details: " << (report_spec.details.empty() ? L"<none>" : std::wstring(report_spec.details))
                   << L"\n";
            stream << L"log_file: " << Log::LogFilePath() << L"\n";
            stream << L"diagnostics_directory: " << CrashDiagnostics::DiagnosticsDirectory() << L"\n";
            return stream.str();
        }

        LONG WINAPI UnhandledExceptionFilterEntry(EXCEPTION_POINTERS *exception_pointers) noexcept
        {
            try
            {
                CAPTUREZY_LOG_ERROR(LogCategory::App, L"Unhandled exception filter invoked.");
                std::wstring const base_name = BuildDiagnosticBaseName(L"crash");
                bool const dump_written = WriteMiniDump(base_name, exception_pointers);
                std::wstring const report_text = BuildUnhandledExceptionReportText(base_name, exception_pointers,
                                                                                   dump_written);
                (void)WriteTextReport({.base_name = base_name, .contents = report_text});
            }
            catch (...)
            {
                OutputDebugStringW(L"CaptureZY: failed while writing unhandled exception diagnostics.\n");
            }

            return EXCEPTION_EXECUTE_HANDLER;
        }

        void TerminateHandler() noexcept
        {
            std::exception_ptr exception = std::current_exception();
            try
            {
                if (exception != nullptr)
                {
                    try
                    {
                        std::rethrow_exception(exception);
                    }
                    catch (std::exception const &caught_exception)
                    {
                        (void)CrashDiagnostics::WriteCaughtExceptionReport(
                            {.origin = "std::terminate", .details = caught_exception.what()});
                    }
                    catch (...)
                    {
                        (void)CrashDiagnostics::WriteCaughtExceptionReport(
                            {.origin = "std::terminate", .details = "unknown exception"});
                    }
                }
                else
                {
                    (void)CrashDiagnostics::WriteCaughtExceptionReport(
                        {.origin = "std::terminate", .details = "terminate without active exception"});
                }
            }
            catch (...)
            {
                OutputDebugStringW(L"CaptureZY: terminate handler fallback logging failed.\n");
            }

            std::abort();
        }
    } // namespace

    void CrashDiagnostics::Install() noexcept
    {
        try
        {
            EnsureDiagnosticsDirectory();
        }
        catch (...)
        {
            OutputDebugStringW(L"CaptureZY: failed to initialize diagnostics directory.\n");
        }

        SetUnhandledExceptionFilter(UnhandledExceptionFilterEntry);
        std::set_terminate(TerminateHandler);
    }

    std::wstring CrashDiagnostics::DiagnosticsDirectory()
    {
        EnsureDiagnosticsDirectory();
        return DiagnosticsDirectoryPath().wstring();
    }

    std::wstring CrashDiagnostics::WriteCaughtExceptionReport(CaughtExceptionInfo exception_info) noexcept
    {
        try
        {
            std::wstring const base_name = BuildDiagnosticBaseName(L"fatal");
            std::wstring const origin_text = exception_info.origin.empty() ? std::wstring(L"<unknown>")
                                                                           : WidenUtf8(exception_info.origin);
            std::wstring const details_text = exception_info.details.empty() ? std::wstring()
                                                                             : WidenUtf8(exception_info.details);
            std::filesystem::path const report_path = WriteTextReport(
                {.base_name = base_name,
                 .contents = BuildCaughtExceptionReportText({.origin = origin_text, .details = details_text})});
            return report_path.empty() ? std::wstring() : report_path.wstring();
        }
        catch (...)
        {
            return {};
        }
    }
} // namespace capturezy::core
