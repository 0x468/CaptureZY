#pragma once

namespace capturezy::core
{
    struct Size
    {
        int width;
        int height;
    };

    class AppMetadata final
    {
      public:
        [[nodiscard]] static wchar_t const *ProductName() noexcept;
        [[nodiscard]] static wchar_t const *MainWindowClassName() noexcept;
        [[nodiscard]] static Size MainWindowSize() noexcept;
    };
} // namespace capturezy::core
