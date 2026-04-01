#pragma once

// clang-format off
#include <windows.h>
// clang-format on

namespace capturezy::feature_capture
{
    struct AnnotationTranslationResult
    {
        RECT bounds{};
        bool moved{false};
    };

    // 前置条件：original_bounds 应完全位于 canvas_rect 内且两者均为非空矩形。
    [[nodiscard]] AnnotationTranslationResult TranslateAnnotationBoundsWithinRect(RECT const &original_bounds,
                                                                                  RECT canvas_rect, LONG delta_x,
                                                                                  LONG delta_y) noexcept;
} // namespace capturezy::feature_capture
