#include "feature_pin/pin_manager.h"

#include <algorithm>
#include <utility>

namespace capturezy::feature_pin
{
    PinManager::PinManager(HINSTANCE instance, core::AppSettings const &app_settings) noexcept
        : instance_(instance), app_settings_(&app_settings)
    {
    }

    bool PinManager::CreatePin(feature_capture::CaptureResult capture_result)
    {
        if (!capture_result.IsValid())
        {
            return false;
        }

        PruneClosedPins();

        auto pin_window = std::make_unique<PinWindow>(instance_, *app_settings_);
        if (!pin_window->Create(std::move(capture_result)))
        {
            return false;
        }

        pin_windows_.push_back(std::move(pin_window));
        return true;
    }

    void PinManager::ShowAll() noexcept
    {
        for (auto &pin_window : pin_windows_)
        {
            if (pin_window != nullptr)
            {
                pin_window->Show();
            }
        }
    }

    void PinManager::HideAll() noexcept
    {
        for (auto &pin_window : pin_windows_)
        {
            if (pin_window != nullptr)
            {
                pin_window->Hide();
            }
        }
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

    std::size_t PinManager::VisiblePinCount() noexcept
    {
        PruneClosedPins();

        return static_cast<std::size_t>(
            std::count_if(pin_windows_.cbegin(), pin_windows_.cend(), [](std::unique_ptr<PinWindow> const &pin_window) {
                return pin_window != nullptr && pin_window->IsVisible();
            }));
    }

    std::size_t PinManager::HiddenPinCount() noexcept
    {
        std::size_t const open_pin_count = OpenPinCount();
        std::size_t const visible_pin_count = VisiblePinCount();
        return open_pin_count >= visible_pin_count ? open_pin_count - visible_pin_count : 0;
    }
} // namespace capturezy::feature_pin
