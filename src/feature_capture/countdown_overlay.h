#pragma once

// clang-format off
#include <windows.h>
// clang-format on

#include <cstdint>

namespace capturezy::feature_capture
{
    /// 屏幕中央倒计时覆盖层，用于延迟截图模式。
    /// 倒计时结束时通过 WM_APP+21 通知宿主窗口。
    class CountdownOverlay final
    {
      public:
        static constexpr UINT CountdownCompleteMessage() noexcept
        {
            return WM_APP + 21;
        }

        explicit CountdownOverlay(HINSTANCE instance) noexcept;
        ~CountdownOverlay() noexcept;

        CountdownOverlay(CountdownOverlay const &) = delete;
        CountdownOverlay &operator=(CountdownOverlay const &) = delete;

        /// 显示倒计时覆盖层。@param owner_window 宿主窗口。
        /// @param countdown_seconds 倒计时秒数（3/5/10 等）。
        [[nodiscard]] bool Show(HWND owner_window, std::uint32_t countdown_seconds);

        /// 关闭倒计时覆盖层（取消模式）。
        void Close() noexcept;

        [[nodiscard]] bool IsVisible() const noexcept;

      private:
        static constexpr UINT_PTR kCountdownTimerId = 1;
        static constexpr int kCountdownTimerIntervalMs = 1000;

        [[nodiscard]] ATOM RegisterWindowClass() const;
        [[nodiscard]] LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

        void PaintOverlay() noexcept;

        HINSTANCE instance_;
        HWND overlay_window_{};
        HWND owner_window_{};
        std::uint32_t countdown_remaining_{0};
        std::uint32_t initial_countdown_{0};
    };
} // namespace capturezy::feature_capture
