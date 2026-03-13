#pragma once

namespace capturezy::core
{
    class AppMetadata final
    {
      public:
        [[nodiscard]] static wchar_t const *ProductName() noexcept;
        [[nodiscard]] static wchar_t const *MainWindowClassName() noexcept;
    };
} // namespace capturezy::core
