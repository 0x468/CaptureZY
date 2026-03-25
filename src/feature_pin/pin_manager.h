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
        class MutationScope final
        {
          public:
            explicit MutationScope(PinManager &manager) noexcept;
            ~MutationScope() noexcept;

            MutationScope(MutationScope const &) = delete;
            MutationScope &operator=(MutationScope const &) = delete;
            MutationScope(MutationScope &&) = delete;
            MutationScope &operator=(MutationScope &&) = delete;

          private:
            PinManager *manager_{};
        };

        void RefreshCountCache() noexcept;
        void NotifyInventoryChanged() const;

        HINSTANCE instance_;
        core::AppSettings const *app_settings_;
        InventoryChangedCallback inventory_changed_callback_;
        std::vector<std::unique_ptr<PinWindow>> pin_windows_;
        std::size_t mutation_depth_{0};
        std::size_t cached_open_pin_count_{0};
        std::size_t cached_visible_pin_count_{0};
    };
} // namespace capturezy::feature_pin
