#include "feature_pin/pin_manager.h"

#include <algorithm>
#include <format>
#include <utility>

#include "core/log.h"
#include "feature_capture/screen_capture.h"
#include "feature_pin/pin_state_store.h"

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

    PinManager::MutationScope::MutationScope(PinManager &manager) noexcept : manager_(&manager)
    {
        ++manager_->mutation_depth_;
    }

    PinManager::MutationScope::~MutationScope() noexcept
    {
        if (manager_ == nullptr)
        {
            return;
        }

        if (manager_->mutation_depth_ > 0)
        {
            --manager_->mutation_depth_;
        }

        if (manager_->mutation_depth_ == 0)
        {
            manager_->RefreshCountCache();
        }
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

        MutationScope mutation_scope(*this);
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
        MutationScope mutation_scope(*this);
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
        MutationScope mutation_scope(*this);
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
        MutationScope mutation_scope(*this);
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
        if (mutation_depth_ > 1)
        {
            return;
        }

        std::size_t const previous_count = pin_windows_.size();
        std::erase_if(pin_windows_, [](std::unique_ptr<PinWindow> const &pin_window) {
            return pin_window == nullptr || !pin_window->IsOpen();
        });

        if (pin_windows_.size() != previous_count)
        {
            LogPrunedCountMessage(previous_count, pin_windows_.size());
        }
    }

    void PinManager::RefreshCountCache() noexcept
    {
        cached_open_pin_count_ = pin_windows_.size();
        cached_visible_pin_count_ = static_cast<std::size_t>(
            std::count_if(pin_windows_.cbegin(), pin_windows_.cend(), [](std::unique_ptr<PinWindow> const &pin_window) {
                return pin_window != nullptr && pin_window->IsVisible();
            }));
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
        if (mutation_depth_ > 0)
        {
            return cached_open_pin_count_;
        }

        PruneClosedPins();
        RefreshCountCache();
        return pin_windows_.size();
    }

    std::size_t PinManager::VisiblePinCount() noexcept
    {
        if (mutation_depth_ > 0)
        {
            return cached_visible_pin_count_;
        }

        PruneClosedPins();
        RefreshCountCache();
        return cached_visible_pin_count_;
    }

    std::size_t PinManager::HiddenPinCount() noexcept
    {
        if (mutation_depth_ > 0)
        {
            return cached_open_pin_count_ >= cached_visible_pin_count_
                       ? cached_open_pin_count_ - cached_visible_pin_count_
                       : 0;
        }

        std::size_t const open_pin_count = OpenPinCount();
        std::size_t const visible_pin_count = VisiblePinCount();
        return open_pin_count >= visible_pin_count ? open_pin_count - visible_pin_count : 0;
    }

    bool PinManager::SaveAllPinStates() noexcept
    {
        PruneClosedPins();

        if (pin_windows_.empty())
        {
            // 没有贴图时清空持久化目录
            (void)PinStateStore::ClearPinStateDirectory();
            return true;
        }

        std::filesystem::path const state_dir = PinStateStore::PinStateDirectory();
        std::filesystem::path const images_dir = state_dir / L"images";
        std::error_code error_code;
        std::filesystem::create_directories(images_dir, error_code);
        if (error_code)
        {
            CAPTUREZY_LOG_ERROR(core::LogCategory::Pin, L"Failed to create pin images directory.");
            return false;
        }

        std::vector<PinState> pin_states;
        for (std::size_t index = 0; index < pin_windows_.size(); ++index)
        {
            auto const &pin_window = pin_windows_[index];
            if (pin_window == nullptr || !pin_window->IsOpen())
            {
                continue;
            }

            PinState state = pin_window->CaptureState();

            // 保存位图到 PNG 文件
            std::wstring image_filename = std::format(L"pin_{}.png", static_cast<std::uint32_t>(index));
            std::filesystem::path image_path = images_dir / image_filename;

            feature_capture::CaptureResult const &capture_result = pin_window->GetCaptureResult();
            if (!feature_capture::ScreenCapture::SaveBitmapToPng(capture_result, image_path.wstring().c_str()))
            {
                CAPTUREZY_LOG_ERROR(core::LogCategory::Pin, L"Failed to save pin bitmap to file.");
                continue;
            }

            // 使用相对路径存储
            state.image_path = (std::filesystem::path(L"images") / image_filename).wstring();
            pin_states.push_back(std::move(state));
        }

        return PinStateStore::SavePinStates(pin_states);
    }

    bool PinManager::RestorePinStates() noexcept
    {
        std::vector<PinState> pin_states = PinStateStore::LoadPinStates();
        if (pin_states.empty())
        {
            CAPTUREZY_LOG_DEBUG(core::LogCategory::Pin, L"No pin states to restore.");
            return true;
        }

        std::filesystem::path const state_dir = PinStateStore::PinStateDirectory();

        for (auto const &state : pin_states)
        {
            // 从相对路径构建完整路径
            std::filesystem::path full_image_path = state_dir / state.image_path;
            feature_capture::LoadedBitmap loaded =
                feature_capture::ScreenCapture::LoadBitmapFromPng(full_image_path.wstring().c_str());
            if (!loaded.bitmap.IsValid())
            {
                CAPTUREZY_LOG_ERROR(core::LogCategory::Pin, L"Failed to load pin bitmap from file.");
                continue;
            }

            feature_capture::CaptureResult capture_result(
                std::move(loaded.bitmap), RECT{}, std::chrono::system_clock::now());

            auto pin_window = std::make_unique<PinWindow>(instance_, *app_settings_);
            pin_window->SetStateChangedCallback([this]() { NotifyInventoryChanged(); });
            if (!pin_window->Create(std::move(capture_result)))
            {
                CAPTUREZY_LOG_ERROR(core::LogCategory::Pin, L"Pin window creation from restored state failed.");
                continue;
            }

            // 应用恢复的状态
            if (state.scale_percent != 100)
            {
                pin_window->ApplyRestoredScale(state.scale_percent);
            }
            if (state.opacity_percent != 100)
            {
                pin_window->ApplyRestoredOpacity(state.opacity_percent);
            }
            if (!state.topmost)
            {
                pin_window->SetTopmost(false);
            }
            if (!state.shadow_enabled)
            {
                pin_window->SetShadowEnabled(false);
            }
            if (state.click_through)
            {
                pin_window->SetClickThrough(true);
            }
            if (state.locked)
            {
                pin_window->SetLocked(true);
            }

            // 设置位置
            pin_window->SetRestoredPosition(state.position_x, state.position_y);

            if (!state.visible)
            {
                pin_window->Hide();
            }

            pin_windows_.push_back(std::move(pin_window));
        }

        NotifyInventoryChanged();
        CAPTUREZY_LOG_INFO(core::LogCategory::Pin,
                           std::format(L"Restored {} pin windows.", static_cast<std::uint32_t>(pin_states.size())));

        // 恢复成功后清空持久化数据（避免下次启动重复恢复）
        (void)PinStateStore::ClearPinStateDirectory();
        return true;
    }
} // namespace capturezy::feature_pin
