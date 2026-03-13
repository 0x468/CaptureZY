#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <windows.h>

#include "app/application.h"
#include "core/crash_diagnostics.h"

namespace
{
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

    try
    {
#ifdef CAPTUREZY_ENABLE_DIAGNOSTIC_SELF_TEST
        RunDiagnosticsSelfTest(lpCmdLine != nullptr ? std::wstring_view(lpCmdLine) : std::wstring_view{});
#endif
        capturezy::app::Application application(hInstance);
        return application.Run(nShowCmd);
    }
    catch (std::exception const &caught_exception)
    {
        std::wstring const report_path = capturezy::core::CrashDiagnostics::WriteCaughtExceptionReport(
            {.origin = "wWinMain", .details = caught_exception.what()});
        std::wstring message = L"程序发生未处理的 C++ 异常。";
        if (!report_path.empty())
        {
            message += L"\n\n诊断日志已写入：\n";
            message += report_path;
        }
        MessageBoxW(nullptr, message.c_str(), L"CaptureZY", MB_OK | MB_ICONERROR);
        return -1;
    }
    catch (...)
    {
        std::wstring const report_path = capturezy::core::CrashDiagnostics::WriteCaughtExceptionReport(
            {.origin = "wWinMain", .details = "unknown exception"});
        std::wstring message = L"程序发生未知未处理异常。";
        if (!report_path.empty())
        {
            message += L"\n\n诊断日志已写入：\n";
            message += report_path;
        }
        MessageBoxW(nullptr, message.c_str(), L"CaptureZY", MB_OK | MB_ICONERROR);
        return -1;
    }
}
