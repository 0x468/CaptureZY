#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "feature_capture/capture_result.h"
#include "feature_pin/pin_window.h"

namespace capturezy::feature_pin
{
    class PinManager final
    {
      public:
        explicit PinManager(HINSTANCE instance) noexcept;

        [[nodiscard]] bool CreatePin(feature_capture::CaptureResult capture_result);
        void CloseAll() noexcept;
        void PruneClosedPins() noexcept;
        [[nodiscard]] std::size_t OpenPinCount() noexcept;

      private:
        HINSTANCE instance_;
        std::vector<std::unique_ptr<PinWindow>> pin_windows_{};
    };
} // namespace capturezy::feature_pin
