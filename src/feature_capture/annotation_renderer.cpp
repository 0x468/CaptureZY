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

        RECT NormalizedRectToPixel(RECT const& canvas_rect, NormalizedRectF const& normalized)
        {
            float const canvas_width = static_cast<float>(canvas_rect.right - canvas_rect.left);
            float const canvas_height = static_cast<float>(canvas_rect.bottom - canvas_rect.top);

            return RECT{
                canvas_rect.left + static_cast<LONG>(normalized.left * canvas_width),
                canvas_rect.top + static_cast<LONG>(normalized.top * canvas_height),
                canvas_rect.left + static_cast<LONG>(normalized.right * canvas_width),
                canvas_rect.top + static_cast<LONG>(normalized.bottom * canvas_height)
            };
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

    void PaintEllipse(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj)
    {
        RECT const rect = NormalizedRectToPixel(canvas_rect, obj.bounds);

        int const pen_width = GetPenWidth(obj.style.line_width);
        COLORREF const pen_color = GetColorRef(obj.style.color);
        HPEN pen = CreatePen(PS_SOLID, pen_width, pen_color);

        HBRUSH brush = static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
        if (obj.style.fill_enabled)
        {
            brush = CreateSolidBrush(pen_color);
        }

        HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, pen));
        HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(hdc, brush));

        Ellipse(hdc, rect.left, rect.top, rect.right, rect.bottom);

        SelectObject(hdc, old_pen);
        SelectObject(hdc, old_brush);
        DeleteObject(pen);
        if (obj.style.fill_enabled)
        {
            DeleteObject(brush);
        }
    }

    void PaintText(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj)
    {
        auto const* text_data = std::get_if<TextData>(&obj.type_data);
        if (!text_data || text_data->content.empty())
        {
            return;
        }

        RECT rect = NormalizedRectToPixel(canvas_rect, obj.bounds);
        COLORREF const text_color = GetColorRef(obj.style.color);

        // Create font
        HFONT font = CreateFontW(
            static_cast<int>(text_data->font_size),
            0, 0, 0,
            FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI"
        );

        // Select font and color
        HFONT old_font = static_cast<HFONT>(SelectObject(hdc, font));
        COLORREF old_color = SetTextColor(hdc, text_color);
        int old_mode = SetBkMode(hdc, TRANSPARENT);

        // Draw text
        DrawTextW(hdc, text_data->content.c_str(), -1, &rect,
                  DT_LEFT | DT_TOP | DT_WORDBREAK);

        // Cleanup
        SetBkMode(hdc, old_mode);
        SetTextColor(hdc, old_color);
        SelectObject(hdc, old_font);
        DeleteObject(font);
    }

    void PaintMosaic(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj, HBITMAP source_bitmap)
    {
        auto const* mosaic_data = std::get_if<MosaicData>(&obj.type_data);
        if (!mosaic_data || mosaic_data->block_size <= 0)
        {
            return;
        }

        RECT const rect = NormalizedRectToPixel(canvas_rect, obj.bounds);
        int const block_size = mosaic_data->block_size;

        // 创建临时 DC 用于读取源图像
        HDC temp_dc = CreateCompatibleDC(hdc);
        HBITMAP old_bitmap = static_cast<HBITMAP>(SelectObject(temp_dc, source_bitmap));

        // 遍历每个马赛克块
        for (int y = rect.top; y < rect.bottom; y += block_size)
        {
            for (int x = rect.left; x < rect.right; x += block_size)
            {
                // 计算块的实际大小（边界处理）
                LONG block_width = std::min(static_cast<LONG>(block_size), rect.right - x);
                LONG block_height = std::min(static_cast<LONG>(block_size), rect.bottom - y);

                // 采样块中心点的颜色
                int sample_x = static_cast<int>(x + block_width / 2);
                int sample_y = static_cast<int>(y + block_height / 2);
                COLORREF color = GetPixel(temp_dc, sample_x, sample_y);

                // 用该颜色填充整个块
                HBRUSH brush = CreateSolidBrush(color);
                RECT block_rect = {x, y, x + static_cast<LONG>(block_width), y + static_cast<LONG>(block_height)};
                FillRect(hdc, &block_rect, brush);
                DeleteObject(brush);
            }
        }

        // 清理
        SelectObject(temp_dc, old_bitmap);
        DeleteDC(temp_dc);
    }

    void PaintHighlighter(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj)
    {
        auto const* highlighter_data = std::get_if<HighlighterData>(&obj.type_data);
        if (!highlighter_data || highlighter_data->path.size() < 2)
        {
            return;
        }

        COLORREF const pen_color = GetColorRef(obj.style.color);
        int const pen_width = static_cast<int>(highlighter_data->brush_width *
            static_cast<float>(canvas_rect.right - canvas_rect.left));

        // 使用半透明画笔（通过 AlphaBlend 或简单的半透明颜色）
        // GDI 不直接支持半透明，使用较浅的颜色模拟效果
        // 将颜色混合白色以模拟半透明
        BYTE r = GetRValue(pen_color);
        BYTE g = GetGValue(pen_color);
        BYTE b = GetBValue(pen_color);
        // 混合 50% 白色
        r = static_cast<BYTE>((r + 255) / 2);
        g = static_cast<BYTE>((g + 255) / 2);
        b = static_cast<BYTE>((b + 255) / 2);
        COLORREF highlight_color = RGB(r, g, b);

        HPEN pen = CreatePen(PS_SOLID, pen_width, highlight_color);
        HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, pen));

        // 绘制路径
        POINT first_pt = NormalizedToPixel(canvas_rect, highlighter_data->path[0]);
        MoveToEx(hdc, first_pt.x, first_pt.y, nullptr);

        for (size_t i = 1; i < highlighter_data->path.size(); ++i)
        {
            POINT pt = NormalizedToPixel(canvas_rect, highlighter_data->path[i]);
            LineTo(hdc, pt.x, pt.y);
        }

        SelectObject(hdc, old_pen);
        DeleteObject(pen);
    }

    void PaintNumberMarker(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj)
    {
        auto const* marker_data = std::get_if<NumberMarkerData>(&obj.type_data);
        if (!marker_data)
        {
            return;
        }

        // 计算 marker 中心点（来自 bounds 的中心）
        float const center_x = (obj.bounds.left + obj.bounds.right) * 0.5F;
        float const center_y = (obj.bounds.top + obj.bounds.bottom) * 0.5F;
        NormalizedPointF center{center_x, center_y};
        POINT center_px = NormalizedToPixel(canvas_rect, center);

        // 计算像素半径
        int const canvas_width = canvas_rect.right - canvas_rect.left;
        int const radius_px = static_cast<int>(marker_data->radius_normalized * static_cast<float>(canvas_width));
        if (radius_px <= 0)
        {
            return;
        }

        COLORREF const bg_color = GetColorRef(obj.style.color);
        COLORREF const text_color = RGB(255, 255, 255); // 白色数字

        // 绘制填充圆形背景
        HBRUSH bg_brush = CreateSolidBrush(bg_color);
        HPEN bg_pen = CreatePen(PS_SOLID, 1, bg_color); // 同色边框，避免默认黑框
        HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(hdc, bg_brush));
        HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, bg_pen));

        Ellipse(hdc,
                center_px.x - radius_px,
                center_px.y - radius_px,
                center_px.x + radius_px,
                center_px.y + radius_px);

        SelectObject(hdc, old_pen);
        SelectObject(hdc, old_brush);
        DeleteObject(bg_pen);
        DeleteObject(bg_brush);

        // 绘制数字文本
        std::wstring number_str = std::to_wstring(marker_data->number);
        HFONT font = CreateFontW(
            radius_px,               // 高度 = 半径大小，数字占满圆形
            0, 0, 0,
            FW_BOLD,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY,
            DEFAULT_PITCH,
            L"Segoe UI"
        );

        HFONT old_font = static_cast<HFONT>(SelectObject(hdc, font));
        COLORREF old_color = SetTextColor(hdc, text_color);
        int old_bk_mode = SetBkMode(hdc, TRANSPARENT);

        RECT text_rect = {
            center_px.x - radius_px,
            center_px.y - radius_px,
            center_px.x + radius_px,
            center_px.y + radius_px
        };
        DrawTextW(hdc, number_str.c_str(), -1, &text_rect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        SetBkMode(hdc, old_bk_mode);
        SetTextColor(hdc, old_color);
        SelectObject(hdc, old_font);
        DeleteObject(font);
    }

    void PaintAnnotation(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj, HBITMAP source_bitmap)
    {
        switch (obj.kind)
        {
        case AnnotationKind::Rectangle:
            PaintRectangle(hdc, canvas_rect, obj.style);
            break;
        case AnnotationKind::Ellipse:
            PaintEllipse(hdc, canvas_rect, obj);
            break;
        case AnnotationKind::Line:
            PaintLine(hdc, canvas_rect, obj);
            break;
        case AnnotationKind::Arrow:
            PaintArrow(hdc, canvas_rect, obj);
            break;
        case AnnotationKind::Text:
            PaintText(hdc, canvas_rect, obj);
            break;
        case AnnotationKind::Mosaic:
            if (source_bitmap)
            {
                PaintMosaic(hdc, canvas_rect, obj, source_bitmap);
            }
            break;
        case AnnotationKind::Highlighter:
            PaintHighlighter(hdc, canvas_rect, obj);
            break;
        case AnnotationKind::NumberMarker:
            PaintNumberMarker(hdc, canvas_rect, obj);
            break;
        default:
            break;
        }
    }
}
