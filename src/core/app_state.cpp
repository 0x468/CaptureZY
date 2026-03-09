#include "core/app_state.h"

namespace capturezy::core
{
    void AppState::BeginCapture() noexcept
    {
        mode_ = AppMode::CapturePending;
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
        case AppMode::CapturePending:
            return L"截图入口已触发，下一步接入覆盖层与选区逻辑";

        case AppMode::Idle:
        default:
            return L"CaptureZY 启动骨架已就绪";
        }
    }
} // namespace capturezy::core
