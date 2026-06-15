#pragma once

#include <windows.h>
#include "capture_annotation.h"

namespace capturezy::feature_capture
{
    POINT NormalizedToPixel(RECT const& canvas_rect, NormalizedPointF const& pt);
    void PaintRectangle(HDC hdc, RECT const& rect, AnnotationStyle const& style);
    void PaintLine(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj);
    void PaintArrow(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj);
    void PaintEllipse(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj);
    void PaintText(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj);
    void PaintMosaic(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj, HBITMAP source_bitmap);
    void PaintAnnotation(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj, HBITMAP source_bitmap = nullptr);
}
