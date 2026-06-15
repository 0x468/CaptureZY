#include "feature_capture/annotation_renderer.h"
#include <cmath>

namespace capturezy::feature_capture
{
    namespace
    {
        COLORREF GetColorRef(AnnotationColor color)
        {
            switch (color)
            {
            case AnnotationColor::Red:
                return RGB(255, 80, 80);
            case AnnotationColor::Green:
                return RGB(80, 255, 80);
            case AnnotationColor::Blue:
                return RGB(80, 160, 255);
            case AnnotationColor::White:
                return RGB(255, 255, 255);
            case AnnotationColor::Yellow:
            default:
                return RGB(255, 214, 102);
            }
        }

        int GetPenWidth(AnnotationLineWidth width)
        {
            switch (width)
            {
            case AnnotationLineWidth::Thin:
                return 1;
            case AnnotationLineWidth::Thick:
                return 4;
            case AnnotationLineWidth::Medium:
            default:
                return 2;
            }
        }
    } // namespace

    POINT NormalizedToPixel(RECT const& canvas_rect, NormalizedPointF const& pt)
    {
        float const canvas_width = static_cast<float>(canvas_rect.right - canvas_rect.left);
        float const canvas_height = static_cast<float>(canvas_rect.bottom - canvas_rect.top);

        return POINT{
            canvas_rect.left + static_cast<LONG>(pt.x * canvas_width),
            canvas_rect.top + static_cast<LONG>(pt.y * canvas_height)
        };
    }

    void PaintRectangle(HDC hdc, RECT const& rect, AnnotationStyle const& style)
    {
        COLORREF frame_color = GetColorRef(style.color);
        int line_width = GetPenWidth(style.line_width);

        HPEN frame_pen = CreatePen(PS_SOLID, line_width, frame_color);
        HGDIOBJ old_pen = SelectObject(hdc, frame_pen);
        HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
        SelectObject(hdc, old_brush);
        SelectObject(hdc, old_pen);
        DeleteObject(frame_pen);
    }

    void PaintLine(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj)
    {
        auto const* line_data = std::get_if<LineData>(&obj.type_data);
        if (line_data == nullptr)
        {
            return;
        }

        POINT start = NormalizedToPixel(canvas_rect, line_data->start);
        POINT end = NormalizedToPixel(canvas_rect, line_data->end);

        COLORREF color = GetColorRef(obj.style.color);
        int line_width = GetPenWidth(obj.style.line_width);

        HPEN pen = CreatePen(PS_SOLID, line_width, color);
        HGDIOBJ old_pen = SelectObject(hdc, pen);
        MoveToEx(hdc, start.x, start.y, nullptr);
        LineTo(hdc, end.x, end.y);
        SelectObject(hdc, old_pen);
        DeleteObject(pen);
    }

    void PaintArrow(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj)
    {
        auto const* arrow_data = std::get_if<ArrowData>(&obj.type_data);
        if (arrow_data == nullptr)
        {
            return;
        }

        POINT start = NormalizedToPixel(canvas_rect, arrow_data->start);
        POINT end = NormalizedToPixel(canvas_rect, arrow_data->end);

        COLORREF color = GetColorRef(obj.style.color);
        int line_width = GetPenWidth(obj.style.line_width);

        HPEN pen = CreatePen(PS_SOLID, line_width, color);
        HGDIOBJ old_pen = SelectObject(hdc, pen);

        // 绘制线段
        MoveToEx(hdc, start.x, start.y, nullptr);
        LineTo(hdc, end.x, end.y);

        // 计算箭头头部
        float dx = static_cast<float>(end.x - start.x);
        float dy = static_cast<float>(end.y - start.y);
        float line_length = std::sqrt(dx * dx + dy * dy);

        if (line_length > 1.0f)
        {
            float arrow_length = arrow_data->head_size * line_length;
            float angle = std::atan2(dy, dx);
            float const arrow_angle = 30.0f * 3.14159265f / 180.0f; // 30度

            POINT arrow_left{
                end.x - static_cast<LONG>(arrow_length * std::cos(angle - arrow_angle)),
                end.y - static_cast<LONG>(arrow_length * std::sin(angle - arrow_angle))
            };
            POINT arrow_right{
                end.x - static_cast<LONG>(arrow_length * std::cos(angle + arrow_angle)),
                end.y - static_cast<LONG>(arrow_length * std::sin(angle + arrow_angle))
            };

            if (arrow_data->head_style == ArrowHeadStyle::Solid)
            {
                HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
                POINT points[3] = {end, arrow_left, arrow_right};
                Polygon(hdc, points, 3);
                SelectObject(hdc, old_brush);
            }
            else
            {
                MoveToEx(hdc, end.x, end.y, nullptr);
                LineTo(hdc, arrow_left.x, arrow_left.y);
                MoveToEx(hdc, end.x, end.y, nullptr);
                LineTo(hdc, arrow_right.x, arrow_right.y);
            }
        }

        SelectObject(hdc, old_pen);
        DeleteObject(pen);
    }

    void PaintAnnotation(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj)
    {
        switch (obj.kind)
        {
        case AnnotationKind::Rectangle:
            PaintRectangle(hdc, canvas_rect, obj.style);
            break;
        case AnnotationKind::Line:
            PaintLine(hdc, canvas_rect, obj);
            break;
        case AnnotationKind::Arrow:
            PaintArrow(hdc, canvas_rect, obj);
            break;
        default:
            break;
        }
    }
}
