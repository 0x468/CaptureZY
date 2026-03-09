#include "feature_pin/pin_manager.h"

#include <algorithm>
#include <utility>

namespace capturezy::feature_pin
{
    PinManager::PinManager(HINSTANCE instance) noexcept : instance_(instance) {}

    bool PinManager::CreatePin(feature_capture::CaptureResult capture_result)
    {
        if (!capture_result.IsValid())
        {
            return false;
        }

        PruneClosedPins();

        auto pin_window = std::make_unique<PinWindow>(instance_);
        if (!pin_window->Create(std::move(capture_result)))
        {
            return false;
        }

        pin_windows_.push_back(std::move(pin_window));
        return true;
    }

    void PinManager::CloseAll() noexcept
    {
        for (auto &pin_window : pin_windows_)
        {
            if (pin_window != nullptr)
            {
                pin_window->Close();
            }
        }

        PruneClosedPins();
    }

    void PinManager::PruneClosedPins() noexcept
    {
        std::erase_if(pin_windows_, [](std::unique_ptr<PinWindow> const &pin_window) {
            return pin_window == nullptr || !pin_window->IsOpen();
        });
    }

    std::size_t PinManager::OpenPinCount() noexcept
    {
        PruneClosedPins();
        return pin_windows_.size();
    }
} // namespace capturezy::feature_pin
