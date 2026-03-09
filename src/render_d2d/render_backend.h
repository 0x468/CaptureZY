#pragma once

namespace capturezy::render_d2d
{
    class RenderBackend final
    {
      public:
        [[nodiscard]] static wchar_t const *DisplayName() noexcept;
    };
} // namespace capturezy::render_d2d
