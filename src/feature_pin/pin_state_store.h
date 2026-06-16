#pragma once

// clang-format off
#include <windows.h>
// clang-format on

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace capturezy::feature_pin
{
    struct PinState final
    {
        std::wstring image_path;
        std::int32_t position_x{0};
        std::int32_t position_y{0};
        std::int32_t scale_percent{100};
        std::int32_t opacity_percent{100};
        bool topmost{true};
        bool shadow_enabled{true};
        bool click_through{false};
        bool locked{false};
        bool visible{true};
    };

    class PinStateStore final
    {
      public:
        [[nodiscard]] static std::filesystem::path PinStateDirectory();
        [[nodiscard]] static std::filesystem::path PinStateFilePath();
        [[nodiscard]] static bool SavePinStates(std::vector<PinState> const &pin_states);
        [[nodiscard]] static std::vector<PinState> LoadPinStates();
        [[nodiscard]] static bool ClearPinStateDirectory();
    };
} // namespace capturezy::feature_pin
