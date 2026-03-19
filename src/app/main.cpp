#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <windows.h>

#include "app/application.h"
#include "core/crash_diagnostics.h"
#include "core/log.h"

namespace
{
    [[nodiscard]] std::wstring BuildStartupSummary()
    {
        std::wstring summary = L"Startup context: build=";
#ifdef _DEBUG
        summary += L"debug";
#else
        summary += L"release";
#endif
        summary += L", session_id=";
        summary += capturezy::core::Log::SessionId();
        summary += L", process_id=";
        summary += std::to_wstring(capturezy::core::Log::ProcessId());
        summary += L", log_file=";
        summary += capturezy::core::Log::LogFilePath();
        summary += L", diagnostics_dir=";
        summary += capturezy::core::CrashDiagnostics::DiagnosticsDirectory();
        return summary;
    }

#ifdef CAPTUREZY_ENABLE_DIAGNOSTIC_SELF_TEST
    void RunDiagnosticsSelfTest(std::wstring_view command_line)
    {
        if (command_line.contains(L"--diag-test-cpp-crash"))
        {
            throw std::runtime_error("requested diagnostic self-test");
        }

        if (command_line.contains(L"--diag-test-seh-crash"))
        {
            RaiseException(0xE0000001UL, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        }
    }
#endif
} // namespace

// `wWinMain` 需要保持 Windows 约定签名，这里对 clang-tidy 的误报做局部抑制。
int WINAPI wWinMain(HINSTANCE hInstance, // NOLINT(bugprone-easily-swappable-parameters)
                    HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nShowCmd) // NOLINT(readability-non-const-parameter)
{
    (void)hPrevInstance;
    (void)lpCmdLine;

    capturezy::core::CrashDiagnostics::Install();
    capturezy::core::Log::Initialize();
    CAPTUREZY_LOG_INFO(capturezy::core::LogCategory::App, L"Process startup.");
    CAPTUREZY_LOG_INFO(capturezy::core::LogCategory::App, BuildStartupSummary());

    try
    {
#ifdef CAPTUREZY_ENABLE_DIAGNOSTIC_SELF_TEST
        RunDiagnosticsSelfTest(lpCmdLine != nullptr ? std::wstring_view(lpCmdLine) : std::wstring_view{});
#endif
        capturezy::app::Application application(hInstance);
        int const exit_code = application.Run(nShowCmd);
        CAPTUREZY_LOG_INFO(capturezy::core::LogCategory::App,
                           std::wstring(L"Application exited with code ") + std::to_wstring(exit_code) + L".");
        capturezy::core::Log::Shutdown();
        return exit_code;
    }
    catch (std::exception const &caught_exception)
    {
        CAPTUREZY_LOG_ERROR(capturezy::core::LogCategory::App, caught_exception.what());
        std::wstring const report_path = capturezy::core::CrashDiagnostics::WriteCaughtExceptionReport(
            {.origin = "wWinMain", .details = caught_exception.what()});
        std::wstring message = L"程序发生未处理的 C++ 异常。";
        if (!report_path.empty())
        {
            message += L"\n\n诊断日志已写入：\n";
            message += report_path;
        }
        MessageBoxW(nullptr, message.c_str(), L"CaptureZY", MB_OK | MB_ICONERROR);
        capturezy::core::Log::Shutdown();
        return -1;
    }
    catch (...)
    {
        CAPTUREZY_LOG_ERROR(capturezy::core::LogCategory::App, L"Unknown top-level exception.");
        std::wstring const report_path = capturezy::core::CrashDiagnostics::WriteCaughtExceptionReport(
            {.origin = "wWinMain", .details = "unknown exception"});
        std::wstring message = L"程序发生未知未处理异常。";
        if (!report_path.empty())
        {
            message += L"\n\n诊断日志已写入：\n";
            message += report_path;
        }
        MessageBoxW(nullptr, message.c_str(), L"CaptureZY", MB_OK | MB_ICONERROR);
        capturezy::core::Log::Shutdown();
        return -1;
    }
}
