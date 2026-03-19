#include "feature_pin/pin_manager.h"

#include <algorithm>
#include <format>
#include <utility>

#include "core/log.h"

namespace capturezy::feature_pin
{
    namespace
    {
        void LogOpenCountMessage(std::size_t open_count) noexcept
        {
            try
            {
                core::Log::Write(core::LogLevel::Info, core::LogCategory::Pin,
                                 std::format(L"Created pin window. open_count={}.", open_count));
            }
            catch (...)
            {
                core::Log::Write(core::LogLevel::Info, core::LogCategory::Pin, L"Created pin window.");
            }
        }

        void LogPrunedCountMessage(std::size_t previous_count, std::size_t current_count) noexcept
        {
            try
            {
                core::Log::Write(
                    core::LogLevel::Debug, core::LogCategory::Pin,
                    std::format(L"Pruned closed pins. before={}, after={}.", previous_count, current_count));
            }
            catch (...)
            {
                core::Log::Write(core::LogLevel::Debug, core::LogCategory::Pin, L"Pruned closed pins.");
            }
        }
    } // namespace

    PinManager::PinManager(HINSTANCE instance, core::AppSettings const &app_settings) noexcept
        : instance_(instance), app_settings_(&app_settings)
    {
    }

    void PinManager::SetInventoryChangedCallback(InventoryChangedCallback callback)
    {
        inventory_changed_callback_ = std::move(callback);
    }

    bool PinManager::CreatePin(feature_capture::CaptureResult capture_result)
    {
        if (!capture_result.IsValid())
        {
            CAPTUREZY_LOG_WARNING(core::LogCategory::Pin, L"Skip pin creation because capture result is invalid.");
            return false;
        }

        PruneClosedPins();

        auto pin_window = std::make_unique<PinWindow>(instance_, *app_settings_);
        pin_window->SetStateChangedCallback([this]() { NotifyInventoryChanged(); });
        if (!pin_window->Create(std::move(capture_result)))
        {
            CAPTUREZY_LOG_ERROR(core::LogCategory::Pin, L"Pin window creation failed.");
            return false;
        }

        pin_windows_.push_back(std::move(pin_window));
        LogOpenCountMessage(pin_windows_.size());
        NotifyInventoryChanged();
        return true;
    }

    void PinManager::ShowAll() noexcept
    {
        CAPTUREZY_LOG_DEBUG(core::LogCategory::Pin, L"Show all pins requested.");
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
        CAPTUREZY_LOG_DEBUG(core::LogCategory::Pin, L"Hide all pins requested.");
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
        CAPTUREZY_LOG_DEBUG(core::LogCategory::Pin, L"Close all pins requested.");
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
        std::size_t const previous_count = pin_windows_.size();
        std::erase_if(pin_windows_, [](std::unique_ptr<PinWindow> const &pin_window) {
            return pin_window == nullptr || !pin_window->IsOpen();
        });

        if (pin_windows_.size() != previous_count)
        {
            LogPrunedCountMessage(previous_count, pin_windows_.size());
        }
    }

    void PinManager::NotifyInventoryChanged() const
    {
        if (inventory_changed_callback_)
        {
            inventory_changed_callback_();
        }
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
