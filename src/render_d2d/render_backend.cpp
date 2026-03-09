#include "render_d2d/render_backend.h"

namespace capturezy::render_d2d
{
    wchar_t const *RenderBackend::DisplayName() noexcept
    {
        return L"Direct2D";
    }
} // namespace capturezy::render_d2d
