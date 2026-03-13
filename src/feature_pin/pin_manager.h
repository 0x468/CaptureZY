#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

#include "core/app_settings.h"
#include "feature_capture/capture_result.h"
#include "feature_pin/pin_window.h"

namespace capturezy::feature_pin
{
    class PinManager final
    {
      public:
        using InventoryChangedCallback = std::function<void()>;

        PinManager(HINSTANCE instance, core::AppSettings const &app_settings) noexcept;

        [[nodiscard]] bool CreatePin(feature_capture::CaptureResult capture_result);
        void SetInventoryChangedCallback(InventoryChangedCallback callback);
        void ShowAll() noexcept;
        void HideAll() noexcept;
        void CloseAll() noexcept;
        void PruneClosedPins() noexcept;
        [[nodiscard]] std::size_t OpenPinCount() noexcept;
        [[nodiscard]] std::size_t VisiblePinCount() noexcept;
        [[nodiscard]] std::size_t HiddenPinCount() noexcept;

      private:
        void NotifyInventoryChanged() const;

        HINSTANCE instance_;
        core::AppSettings const *app_settings_;
        InventoryChangedCallback inventory_changed_callback_{};
        std::vector<std::unique_ptr<PinWindow>> pin_windows_;
    };
} // namespace capturezy::feature_pin
