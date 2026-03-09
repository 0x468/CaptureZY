#include "core/app_state.h"

namespace capturezy::core
{
    void AppState::BeginCapture() noexcept
    {
        mode_ = AppMode::CapturePending;
    }

    void AppState::CompleteCapture() noexcept
    {
        mode_ = AppMode::CaptureCompleted;
    }

    void AppState::CompleteCaptureSaved() noexcept
    {
        mode_ = AppMode::CaptureSaved;
    }

    void AppState::CompleteCaptureAndPin() noexcept
    {
        mode_ = AppMode::CapturePinned;
    }

    void AppState::ReturnToIdle() noexcept
    {
        mode_ = AppMode::Idle;
    }

    AppMode AppState::Mode() const noexcept
    {
        return mode_;
    }

    wchar_t const *AppState::WindowTitleSuffix() const noexcept
    {
        switch (mode_)
        {
        case AppMode::CapturePinned:
            return L" - 已复制并贴图";

        case AppMode::CaptureSaved:
            return L" - 已保存截图";

        case AppMode::CaptureCompleted:
            return L" - 已复制截图";

        case AppMode::CapturePending:
            return L" - 准备截图";

        case AppMode::Idle:
        default:
            return L"";
        }
    }

    wchar_t const *AppState::StatusText() const noexcept
    {
        switch (mode_)
        {
        case AppMode::CapturePinned:
            return L"选区截图已复制到剪贴板，并打开贴图窗口";

        case AppMode::CaptureSaved:
            return L"选区截图已保存为 PNG 文件";

        case AppMode::CaptureCompleted:
            return L"选区截图已复制到剪贴板";

        case AppMode::CapturePending:
            return L"拖拽选择区域，松开左键完成，Esc 取消";

        case AppMode::Idle:
        default:
            return L"CaptureZY 启动骨架已就绪";
        }
    }
} // namespace capturezy::core
