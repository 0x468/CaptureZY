#include <windows.h>

#include "app/application.h"

// `wWinMain` 需要保持 Windows 约定签名，这里对 clang-tidy 的误报做局部抑制。
int WINAPI wWinMain(HINSTANCE hInstance, // NOLINT(bugprone-easily-swappable-parameters)
                    HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nShowCmd) // NOLINT(readability-non-const-parameter)
{
    (void)hPrevInstance;
    (void)lpCmdLine;

    capturezy::app::Application application(hInstance);
    return application.Run(nShowCmd);
}
