#pragma once

#include <cstdint>

namespace capturezy::core
{
    enum class AppMode : std::uint8_t
    {
        Idle,
        CapturePending,
        CaptureCompleted,
        CaptureSaved,
        CapturePinned,
    };

    class AppState final
    {
      public:
        void BeginCapture() noexcept;
        void CompleteCapture() noexcept;
        void CompleteCaptureSaved() noexcept;
        void CompleteCaptureAndPin() noexcept;
        void ReturnToIdle() noexcept;

        [[nodiscard]] AppMode Mode() const noexcept;
        [[nodiscard]] wchar_t const *WindowTitleSuffix() const noexcept;
        [[nodiscard]] wchar_t const *StatusText() const noexcept;

      private:
        AppMode mode_{AppMode::Idle};
    };
} // namespace capturezy::core
