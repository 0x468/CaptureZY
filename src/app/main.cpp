#include <windows.h>

namespace
{
    constexpr wchar_t kWindowClassName[] = L"CaptureZY.MainWindow";
    constexpr wchar_t kWindowTitle[] = L"CaptureZY";

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

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command)
{
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = MainWindowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kWindowClassName;

    if (RegisterClassExW(&window_class) == 0)
    {
        return -1;
    }

    // 当前仅提供最小主窗口壳层，后续会替换为真正的应用启动流程。
    HWND window = CreateWindowExW(0, kWindowClassName, kWindowTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                  1280, 768, nullptr, nullptr, instance, nullptr);

    if (window == nullptr)
    {
        return -1;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
