#include "feature_capture/capture_annotation_geometry.h"

#include <algorithm>

namespace capturezy::feature_capture
{
    namespace
    {
        [[nodiscard]] bool IsRectNonEmpty(RECT rect) noexcept
        {
            return rect.right > rect.left && rect.bottom > rect.top;
        }

        [[nodiscard]] LONG ClampDeltaToRange(LONG delta, LONG min_delta, LONG max_delta) noexcept
        {
            if (min_delta > max_delta)
            {
                return 0;
            }

            return std::clamp(delta, min_delta, max_delta);
        }
    } // namespace

    AnnotationTranslationResult TranslateAnnotationBoundsWithinRect(RECT const &original_bounds, RECT canvas_rect,
                                                                    LONG delta_x, LONG delta_y) noexcept
    {
        if (!IsRectNonEmpty(canvas_rect) || !IsRectNonEmpty(original_bounds))
        {
            return AnnotationTranslationResult{.bounds = original_bounds, .moved = false};
        }

        RECT translated_rect = original_bounds;
        LONG const min_delta_x = canvas_rect.left - translated_rect.left;
        LONG const max_delta_x = canvas_rect.right - translated_rect.right;
        LONG const min_delta_y = canvas_rect.top - translated_rect.top;
        LONG const max_delta_y = canvas_rect.bottom - translated_rect.bottom;
        LONG const clamped_delta_x = ClampDeltaToRange(delta_x, min_delta_x, max_delta_x);
        LONG const clamped_delta_y = ClampDeltaToRange(delta_y, min_delta_y, max_delta_y);
        OffsetRect(&translated_rect, clamped_delta_x, clamped_delta_y);

        return AnnotationTranslationResult{
            .bounds = translated_rect,
            .moved = clamped_delta_x != 0 || clamped_delta_y != 0,
        };
    }
} // namespace capturezy::feature_capture
