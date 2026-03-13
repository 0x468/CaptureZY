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
} // namespace capturezy::core
