#include <windows.h>

namespace
{
    constexpr wchar_t const *kWindowClassName = L"CaptureZY.MainWindow";
    constexpr wchar_t const *kWindowTitle = L"CaptureZY";
    constexpr int kInitialWindowWidth = 1280;
    constexpr int kInitialWindowHeight = 768;

    LRESULT CALLBACK MainWindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
    {
        switch (message)
        {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(window, message, w_param, l_param);
        }
    }
} // namespace

// `wWinMain` 需要保持 Windows 约定签名，这里对 clang-tidy 的误报做局部抑制。
int WINAPI wWinMain(HINSTANCE hInstance, // NOLINT(bugprone-easily-swappable-parameters)
                    HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nShowCmd) // NOLINT(readability-non-const-parameter)
{
    (void)hPrevInstance;
    (void)lpCmdLine;

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = MainWindowProc;
    window_class.hInstance = hInstance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    window_class.lpszClassName = kWindowClassName;

    if (RegisterClassExW(&window_class) == 0)
    {
        return -1;
    }

    // 当前仅提供最小主窗口壳层，后续会替换为真正的应用启动流程。
    HWND window = CreateWindowExW(0, kWindowClassName, kWindowTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  kInitialWindowWidth, kInitialWindowHeight, nullptr, nullptr, hInstance, nullptr);

    if (window == nullptr)
    {
        return -1;
    }

    ShowWindow(window, nShowCmd);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
