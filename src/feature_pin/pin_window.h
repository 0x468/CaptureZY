#pragma once

// clang-format off
#include <windows.h>
// clang-format on

#include <cstdint>
#include <functional>

#include "core/app_settings.h"
#include "feature_capture/capture_result.h"

namespace capturezy::feature_pin
{
    class PinWindow final
    {
      public:
        using StateChangedCallback = std::function<void()>;

        PinWindow(HINSTANCE instance, core::AppSettings const &app_settings) noexcept;
        ~PinWindow() noexcept;

        PinWindow(PinWindow const &) = delete;
        PinWindow &operator=(PinWindow const &) = delete;
        PinWindow(PinWindow &&) = delete;
        PinWindow &operator=(PinWindow &&) = delete;

        [[nodiscard]] bool Create(feature_capture::CaptureResult capture_result);
        void Close() noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;
        void SetStateChangedCallback(StateChangedCallback callback);
        void Show() noexcept;
        void Hide() noexcept;
        [[nodiscard]] bool IsVisible() const noexcept;

      private:
        [[nodiscard]] static RECT CalculateWindowRect(RECT anchor_rect, SIZE bitmap_size) noexcept;
        [[nodiscard]] SIZE CurrentClientSize() const noexcept;
        [[nodiscard]] ATOM RegisterWindowClass() const;
        [[nodiscard]] ATOM RegisterShadowWindowClass() const;
        [[nodiscard]] ATOM RegisterScaleOverlayClass() const;
        bool UpdateScale(short wheel_delta) noexcept;
        void SetTopmost(bool topmost) noexcept;
        void SetShadowEnabled(bool enabled) noexcept;
        void CopyToClipboard() const noexcept;
        void SaveToFile() const;
        void ShowContextMenu(POINT anchor_screen_point) noexcept;
        void PaintWindow() noexcept;
        void PaintScaleOverlay(HWND overlay_window) const;
        void ShowShadowWindow() noexcept;
        void HideShadowWindow() noexcept;
        void UpdateShadowWindowVisual() const noexcept;
        void UpdateShadowWindowPosition() const noexcept;
        void ShowScaleOverlay() noexcept;
        void HideScaleOverlay() noexcept;
        void UpdateScaleOverlayPosition() const noexcept;
        void ResetScaledBitmapCache() noexcept;
        void ResetPaintBuffer() noexcept;
        bool EnsureScaledBitmapCache(HDC device_context, SIZE target_size) noexcept;
        bool EnsurePaintBuffer(HDC device_context, SIZE target_size) noexcept;
        bool DrawScaledBitmap(HDC destination_device_context, SIZE target_size) noexcept;
        void FinishScaleInteraction() noexcept;
        void BeginDrag(POINT cursor_screen_point) noexcept;
        void UpdateDrag(POINT cursor_screen_point) noexcept;
        void EndDrag() noexcept;
        [[nodiscard]] LRESULT HandleCommand(WORD command_id);
        void ResetScaleToDefault() noexcept;
        void ShowContextMenuFromClientPoint(LPARAM l_param) noexcept;
        void HandleWindowPosChanged() noexcept;
        void CleanupDestroyedWindow() noexcept;
        [[nodiscard]] LRESULT HandleMessage(UINT message, WPARAM w_param, LPARAM l_param);

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
        static LRESULT CALLBACK ShadowWindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
        static LRESULT CALLBACK ScaleOverlayProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

        HINSTANCE instance_;
        core::AppSettings const *app_settings_;
        StateChangedCallback state_changed_callback_;
        HWND window_{};
        HWND shadow_window_{};
        HWND scale_overlay_window_{};
        feature_capture::CaptureResult capture_result_;
        feature_capture::CapturedBitmap scaled_bitmap_cache_;
        SIZE scaled_bitmap_cache_size_{};
        feature_capture::CapturedBitmap paint_buffer_;
        SIZE paint_buffer_size_{};
        POINT drag_offset_{};
        std::int32_t scale_percent_{100};
        bool topmost_{true};
        bool shadow_enabled_{true};
        bool dragging_{false};
        bool scale_interaction_active_{false};
    };
} // namespace capturezy::feature_pin
