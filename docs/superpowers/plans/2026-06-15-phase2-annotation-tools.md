# Phase 2: 新增标注工具类型 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the annotation system to support 7 new annotation types (arrow, line, text, mosaic, highlighter, numbered marker, ellipse) while maintaining full compatibility with existing rectangle annotations.

**Architecture:** Extend the AnnotationObject data model using std::variant to store type-specific data (endpoints for lines/arrows, text content for text, path points for highlighters). Each annotation type implements its own rendering, hit-testing, and control point logic through type-specific functions. The existing selection, move, resize, delete, and undo/redo systems work unchanged by operating on the unified bounds field.

**Tech Stack:** C++23, std::variant, Win32 GDI, CMake

---

## File Structure

### Core Data Model
- **Modify:** `src/feature_capture/capture_annotation.h` - Extend AnnotationKind enum, add type-specific data structures, extend AnnotationObject with std::variant
- **Modify:** `src/feature_capture/capture_annotation.cpp` - Implement type-specific hit-testing functions, update HitTestObject to dispatch by type

### Rendering
- **Modify:** `src/feature_capture/capture_overlay.cpp` - Add type-specific rendering functions in PaintOverlay, extend toolbar with new tool buttons
- **Create:** `src/feature_capture/annotation_renderer.h` - Declare type-specific rendering functions
- **Create:** `src/feature_capture/annotation_renderer.cpp` - Implement rendering for all annotation types

### Interaction
- **Modify:** `src/feature_capture/capture_overlay.cpp` - Update BeginCreateAnnotation/UpdateCreateAnnotation/CompletePointerSelection to handle different creation modes

### Tests
- **Modify:** `tests/feature_capture/annotation_session_test.cpp` - Add tests for all new annotation types
- **Create:** `tests/feature_capture/annotation_hit_test_test.cpp` - Comprehensive hit-testing tests for each type
- **Create:** `tests/feature_capture/annotation_renderer_test.cpp` - Rendering validation tests

---

## Task 1: Extend Data Model for Line and Arrow

**Files:**
- Modify: `src/feature_capture/capture_annotation.h:24-27` (AnnotationKind enum)
- Modify: `src/feature_capture/capture_annotation.h:63-69` (add NormalizedPointF, LineData, ArrowData structs)
- Modify: `src/feature_capture/capture_annotation.h:71-77` (extend AnnotationObject with variant)

- [ ] **Step 1: Add new annotation kinds to enum**

```cpp
enum class AnnotationKind : std::uint8_t
{
    Rectangle,
    Ellipse,
    Line,
    Arrow,
    Text,
    Mosaic,
    Highlighter,
    NumberMarker,
};
```

- [ ] **Step 2: Add point and type-specific data structures**

```cpp
struct NormalizedPointF
{
    float x{0.0F};
    float y{0.0F};
};

struct LineData
{
    NormalizedPointF start{};
    NormalizedPointF end{};
};

struct ArrowData
{
    NormalizedPointF start{};
    NormalizedPointF end{};
    ArrowHeadStyle head_style{ArrowHeadStyle::Solid};
    float head_size{0.05F}; // normalized, relative to line length
};

enum class ArrowHeadStyle : std::uint8_t
{
    Solid,
    Outline,
};
```

- [ ] **Step 3: Extend AnnotationObject with variant**

```cpp
#include <variant>

struct AnnotationObject
{
    AnnotationObjectId id{0};
    AnnotationKind kind{AnnotationKind::Rectangle};
    NormalizedRectF bounds{};
    AnnotationStyle style{};
    
    // Type-specific data (only used for certain types)
    std::variant<std::monostate, LineData, ArrowData> type_data{};
};
```

- [ ] **Step 4: Update AddObject to handle line/arrow types**

In `capture_annotation.cpp`, no changes needed - AddObject already works with any AnnotationObject.

- [ ] **Step 5: Write test for line annotation creation**

```cpp
// tests/feature_capture/annotation_session_test.cpp
TEST_F(AnnotationSessionTest, AddLineAnnotation)
{
    LineData line{
        .start = {0.1F, 0.2F},
        .end = {0.8F, 0.7F}
    };
    
    AnnotationObject obj{
        .kind = AnnotationKind::Line,
        .bounds = {0.1F, 0.2F, 0.8F, 0.7F}, // bounding box
        .style = AnnotationStyle{},
        .type_data = line
    };
    
    session.AddObject(obj);
    
    ASSERT_EQ(session.ObjectCount(), 1);
    EXPECT_EQ(session.GetObject(0).kind, AnnotationKind::Line);
    
    auto* line_ptr = std::get_if<LineData>(&session.GetObject(0).type_data);
    ASSERT_NE(line_ptr, nullptr);
    EXPECT_FLOAT_EQ(line_ptr->start.x, 0.1F);
    EXPECT_FLOAT_EQ(line_ptr->end.x, 0.8F);
}
```

- [ ] **Step 6: Run test to verify it fails**

Run: `scripts/build.bat && out/build/x64-Debug/tests/feature_capture/annotation_session_test.exe --gtest_filter=*AddLineAnnotation*`
Expected: FAIL (compilation error - LineData not defined)

- [ ] **Step 7: Run test to verify it passes**

Run: `scripts/build.bat && out/build/x64-Debug/tests/feature_capture/annotation_session_test.exe --gtest_filter=*AddLineAnnotation*`
Expected: PASS

- [ ] **Step 8: Commit**

```bash
git add src/feature_capture/capture_annotation.h tests/feature_capture/annotation_session_test.cpp
git commit -m "feat: add Line and Arrow annotation kinds with type-specific data"
```

---

## Task 2: Implement Line Hit Testing

**Files:**
- Modify: `src/feature_capture/capture_annotation.h:115-118` (add HitTestLine declaration)
- Modify: `src/feature_capture/capture_annotation.cpp:238-314` (add HitTestLine implementation)
- Modify: `src/feature_capture/capture_annotation.cpp:238-314` (update HitTestObject to dispatch by type)

- [ ] **Step 1: Declare HitTestLine function**

```cpp
// In capture_annotation.h, after HitTestObject declaration
[[nodiscard]] static AnnotationHitTestResult HitTestLine(
    AnnotationObject const &object,
    NormalizedRectF point_rect,
    float tolerance_normalized);
```

- [ ] **Step 2: Implement HitTestLine using point-to-segment distance**

```cpp
// In capture_annotation.cpp
AnnotationHitTestResult AnnotationSession::HitTestLine(
    AnnotationObject const &object,
    NormalizedRectF point_rect,
    float tolerance_normalized)
{
    auto const* line_data = std::get_if<LineData>(&object.type_data);
    if (!line_data)
    {
        return AnnotationHitTestResult{.kind = AnnotationHitKind::None, .object_id = object.id, .handle_index = -1};
    }
    
    float const point_x = (point_rect.left + point_rect.right) * 0.5F;
    float const point_y = (point_rect.top + point_rect.bottom) * 0.5F;
    
    // Check control points (endpoints) first
    float const dx_start = point_x - line_data->start.x;
    float const dy_start = point_y - line_data->start.y;
    if (std::sqrt(dx_start * dx_start + dy_start * dy_start) <= tolerance_normalized)
    {
        return AnnotationHitTestResult{
            .kind = AnnotationHitKind::ControlPoint,
            .object_id = object.id,
            .handle_index = 0 // start point
        };
    }
    
    float const dx_end = point_x - line_data->end.x;
    float const dy_end = point_y - line_data->end.y;
    if (std::sqrt(dx_end * dx_end + dy_end * dy_end) <= tolerance_normalized)
    {
        return AnnotationHitTestResult{
            .kind = AnnotationHitKind::ControlPoint,
            .object_id = object.id,
            .handle_index = 1 // end point
        };
    }
    
    // Point-to-segment distance calculation
    float const dx = line_data->end.x - line_data->start.x;
    float const dy = line_data->end.y - line_data->start.y;
    float const len_sq = dx * dx + dy * dy;
    
    if (len_sq < 0.0001F) // Degenerate line
    {
        return AnnotationHitTestResult{.kind = AnnotationHitKind::None, .object_id = object.id, .handle_index = -1};
    }
    
    // Project point onto line, clamped to segment
    float t = ((point_x - line_data->start.x) * dx + (point_y - line_data->start.y) * dy) / len_sq;
    t = std::clamp(t, 0.0F, 1.0F);
    
    float const closest_x = line_data->start.x + t * dx;
    float const closest_y = line_data->start.y + t * dy;
    
    float const distance = std::sqrt(
        (point_x - closest_x) * (point_x - closest_x) +
        (point_y - closest_y) * (point_y - closest_y)
    );
    
    if (distance <= tolerance_normalized)
    {
        return AnnotationHitTestResult{
            .kind = AnnotationHitKind::Border,
            .object_id = object.id,
            .handle_index = -1
        };
    }
    
    return AnnotationHitTestResult{.kind = AnnotationHitKind::None, .object_id = object.id, .handle_index = -1};
}
```

- [ ] **Step 3: Update HitTestObject to dispatch by type**

```cpp
// In HitTestObject, at the beginning
switch (object.kind)
{
    case AnnotationKind::Line:
    case AnnotationKind::Arrow:
        return HitTestLine(object, point_rect, border_tolerance_normalized);
    
    case AnnotationKind::Rectangle:
    case AnnotationKind::Ellipse:
    case AnnotationKind::Text:
    case AnnotationKind::Mosaic:
    case AnnotationKind::Highlighter:
    case AnnotationKind::NumberMarker:
        // Fall through to existing rectangle hit test logic
        break;
}

// ... existing rectangle hit test code ...
```

- [ ] **Step 4: Write test for line hit testing**

```cpp
// tests/feature_capture/annotation_hit_test_test.cpp
#include <gtest/gtest.h>
#include "feature_capture/capture_annotation.h"

namespace capturezy::feature_capture
{
    class AnnotationHitTestTest : public ::testing::Test
    {
    protected:
        AnnotationSession session;
    };

    TEST_F(AnnotationHitTestTest, HitTestLine_StartPoint)
    {
        LineData line{.start = {0.1F, 0.2F}, .end = {0.8F, 0.7F}};
        AnnotationObject obj{
            .id = 1,
            .kind = AnnotationKind::Line,
            .bounds = {0.1F, 0.2F, 0.8F, 0.7F},
            .type_data = line
        };
        
        // Point near start
        NormalizedRectF point{0.09F, 0.19F, 0.11F, 0.21F};
        auto result = AnnotationSession::HitTestObject(obj, point, 0.03F, 0.02F);
        
        EXPECT_EQ(result.kind, AnnotationHitKind::ControlPoint);
        EXPECT_EQ(result.object_id, 1);
        EXPECT_EQ(result.handle_index, 0);
    }

    TEST_F(AnnotationHitTestTest, HitTestLine_EndPoint)
    {
        LineData line{.start = {0.1F, 0.2F}, .end = {0.8F, 0.7F}};
        AnnotationObject obj{
            .id = 1,
            .kind = AnnotationKind::Line,
            .bounds = {0.1F, 0.2F, 0.8F, 0.7F},
            .type_data = line
        };
        
        // Point near end
        NormalizedRectF point{0.79F, 0.69F, 0.81F, 0.71F};
        auto result = AnnotationSession::HitTestObject(obj, point, 0.03F, 0.02F);
        
        EXPECT_EQ(result.kind, AnnotationHitKind::ControlPoint);
        EXPECT_EQ(result.handle_index, 1);
    }

    TEST_F(AnnotationHitTestTest, HitTestLine_MidPoint)
    {
        LineData line{.start = {0.1F, 0.2F}, .end = {0.8F, 0.7F}};
        AnnotationObject obj{
            .id = 1,
            .kind = AnnotationKind::Line,
            .bounds = {0.1F, 0.2F, 0.8F, 0.7F},
            .type_data = line
        };
        
        // Point on line middle
        NormalizedRectF point{0.44F, 0.44F, 0.46F, 0.46F};
        auto result = AnnotationSession::HitTestObject(obj, point, 0.03F, 0.02F);
        
        EXPECT_EQ(result.kind, AnnotationHitKind::Border);
        EXPECT_EQ(result.handle_index, -1);
    }

    TEST_F(AnnotationHitTestTest, HitTestLine_OffLine)
    {
        LineData line{.start = {0.1F, 0.2F}, .end = {0.8F, 0.7F}};
        AnnotationObject obj{
            .id = 1,
            .kind = AnnotationKind::Line,
            .bounds = {0.1F, 0.2F, 0.8F, 0.7F},
            .type_data = line
        };
        
        // Point far from line
        NormalizedRectF point{0.9F, 0.1F, 0.92F, 0.12F};
        auto result = AnnotationSession::HitTestObject(obj, point, 0.03F, 0.02F);
        
        EXPECT_EQ(result.kind, AnnotationHitKind::None);
    }
}
```

- [ ] **Step 5: Run tests to verify they fail**

Run: `scripts/build.bat && out/build/x64-Debug/tests/feature_capture/annotation_hit_test_test.exe`
Expected: FAIL (compilation error - HitTestLine not defined)

- [ ] **Step 6: Run tests to verify they pass**

Run: `scripts/build.bat && out/build/x64-Debug/tests/feature_capture/annotation_hit_test_test.exe`
Expected: All 4 tests PASS

- [ ] **Step 7: Commit**

```bash
git add src/feature_capture/capture_annotation.h src/feature_capture/capture_annotation.cpp tests/feature_capture/annotation_hit_test_test.cpp
git commit -m "feat: implement hit testing for Line and Arrow annotations"
```

---

## Task 3: Render Line Annotations

**Files:**
- Create: `src/feature_capture/annotation_renderer.h`
- Create: `src/feature_capture/annotation_renderer.cpp`
- Modify: `src/feature_capture/capture_overlay.cpp:136-155` (update PaintAnnotationRect to dispatch by type)

- [ ] **Step 1: Create annotation_renderer.h**

```cpp
#pragma once

#include <windows.h>
#include "capture_annotation.h"

namespace capturezy::feature_capture
{
    void PaintAnnotation(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj);
    
    void PaintRectangle(HDC hdc, RECT const& rect, AnnotationStyle const& style);
    void PaintLine(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj);
    void PaintArrow(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj);
    
    // Helper to convert normalized coordinates to pixel coordinates
    POINT NormalizedToPixel(RECT const& canvas_rect, NormalizedPointF const& pt);
    RECT NormalizedToPixel(RECT const& canvas_rect, NormalizedRectF const& rect);
}
```

- [ ] **Step 2: Implement coordinate conversion helpers**

```cpp
// In annotation_renderer.cpp
#include "annotation_renderer.h"
#include <cmath>

namespace capturezy::feature_capture
{
    POINT NormalizedToPixel(RECT const& canvas_rect, NormalizedPointF const& pt)
    {
        float const canvas_width = static_cast<float>(canvas_rect.right - canvas_rect.left);
        float const canvas_height = static_cast<float>(canvas_rect.bottom - canvas_rect.top);
        
        return POINT{
            .x = canvas_rect.left + static_cast<LONG>(pt.x * canvas_width),
            .y = canvas_rect.top + static_cast<LONG>(pt.y * canvas_height)
        };
    }
    
    RECT NormalizedToPixel(RECT const& canvas_rect, NormalizedRectF const& rect)
    {
        float const canvas_width = static_cast<float>(canvas_rect.right - canvas_rect.left);
        float const canvas_height = static_cast<float>(canvas_rect.bottom - canvas_rect.top);
        
        return RECT{
            .left = canvas_rect.left + static_cast<LONG>(rect.left * canvas_width),
            .top = canvas_rect.top + static_cast<LONG>(rect.top * canvas_height),
            .right = canvas_rect.left + static_cast<LONG>(rect.right * canvas_width),
            .bottom = canvas_rect.top + static_cast<LONG>(rect.bottom * canvas_height)
        };
    }
```

- [ ] **Step 3: Implement PaintLine function**

```cpp
    void PaintLine(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj)
    {
        auto const* line_data = std::get_if<LineData>(&obj.type_data);
        if (!line_data)
        {
            return;
        }
        
        POINT const start = NormalizedToPixel(canvas_rect, line_data->start);
        POINT const end = NormalizedToPixel(canvas_rect, line_data->end);
        
        // Determine pen width based on style
        int pen_width = 2;
        switch (obj.style.line_width)
        {
            case AnnotationLineWidth::Thin: pen_width = 1; break;
            case AnnotationLineWidth::Medium: pen_width = 2; break;
            case AnnotationLineWidth::Thick: pen_width = 4; break;
        }
        
        // Determine pen color based on style
        COLORREF pen_color = RGB(0, 0, 0);
        switch (obj.style.color)
        {
            case AnnotationColor::Red: pen_color = RGB(255, 0, 0); break;
            case AnnotationColor::Green: pen_color = RGB(0, 255, 0); break;
            case AnnotationColor::Blue: pen_color = RGB(0, 0, 255); break;
            case AnnotationColor::Yellow: pen_color = RGB(255, 255, 0); break;
            case AnnotationColor::White: pen_color = RGB(255, 255, 255); break;
        }
        
        HPEN pen = CreatePen(PS_SOLID, pen_width, pen_color);
        HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, pen));
        
        MoveToEx(hdc, start.x, start.y, nullptr);
        LineTo(hdc, end.x, end.y);
        
        SelectObject(hdc, old_pen);
        DeleteObject(pen);
    }
```

- [ ] **Step 4: Implement PaintArrow function**

```cpp
    void PaintArrow(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj)
    {
        auto const* arrow_data = std::get_if<ArrowData>(&obj.type_data);
        if (!arrow_data)
        {
            return;
        }
        
        // Draw the line part
        LineData line_data{.start = arrow_data->start, .end = arrow_data->end};
        AnnotationObject line_obj = obj;
        line_obj.kind = AnnotationKind::Line;
        line_obj.type_data = line_data;
        PaintLine(hdc, canvas_rect, line_obj);
        
        // Calculate arrowhead
        POINT const end = NormalizedToPixel(canvas_rect, arrow_data->end);
        POINT const start = NormalizedToPixel(canvas_rect, arrow_data->start);
        
        float const dx = static_cast<float>(end.x - start.x);
        float const dy = static_cast<float>(end.y - start.y);
        float const len = std::sqrt(dx * dx + dy * dy);
        
        if (len < 1.0F)
        {
            return; // Too short to draw arrowhead
        }
        
        // Normalize direction
        float const dir_x = dx / len;
        float const dir_y = dy / len;
        
        // Arrowhead size in pixels
        float const head_size_pixels = arrow_data->head_size * len;
        
        // Arrowhead angle (30 degrees)
        float const angle = 0.5236F; // 30 degrees in radians
        float const cos_angle = std::cos(angle);
        float const sin_angle = std::sin(angle);
        
        // Calculate arrowhead points
        POINT const left{
            .x = end.x - static_cast<LONG>(head_size_pixels * (dir_x * cos_angle - dir_y * sin_angle)),
            .y = end.y - static_cast<LONG>(head_size_pixels * (dir_x * sin_angle + dir_y * cos_angle))
        };
        
        POINT const right{
            .x = end.x - static_cast<LONG>(head_size_pixels * (dir_x * cos_angle + dir_y * sin_angle)),
            .y = end.y - static_cast<LONG>(head_size_pixels * (-dir_x * sin_angle + dir_y * cos_angle))
        };
        
        // Determine brush/pen color
        COLORREF color = RGB(0, 0, 0);
        switch (obj.style.color)
        {
            case AnnotationColor::Red: color = RGB(255, 0, 0); break;
            case AnnotationColor::Green: color = RGB(0, 255, 0); break;
            case AnnotationColor::Blue: color = RGB(0, 0, 255); break;
            case AnnotationColor::Yellow: color = RGB(255, 255, 0); break;
            case AnnotationColor::White: color = RGB(255, 255, 255); break;
        }
        
        if (arrow_data->head_style == ArrowHeadStyle::Solid)
        {
            POINT points[3] = {end, left, right};
            HBRUSH brush = CreateSolidBrush(color);
            HPEN pen = CreatePen(PS_SOLID, 1, color);
            
            HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(hdc, brush));
            HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, pen));
            
            Polygon(hdc, points, 3);
            
            SelectObject(hdc, old_pen);
            SelectObject(hdc, old_brush);
            DeleteObject(pen);
            DeleteObject(brush);
        }
        else // Outline
        {
            HPEN pen = CreatePen(PS_SOLID, 2, color);
            HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, pen));
            
            MoveToEx(hdc, left.x, left.y, nullptr);
            LineTo(hdc, end.x, end.y);
            LineTo(hdc, right.x, right.y);
            
            SelectObject(hdc, old_pen);
            DeleteObject(pen);
        }
    }
```

- [ ] **Step 5: Implement PaintAnnotation dispatcher**

```cpp
    void PaintAnnotation(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj)
    {
        switch (obj.kind)
        {
            case AnnotationKind::Rectangle:
                PaintRectangle(hdc, NormalizedToPixel(canvas_rect, obj.bounds), obj.style);
                break;
            
            case AnnotationKind::Line:
                PaintLine(hdc, canvas_rect, obj);
                break;
            
            case AnnotationKind::Arrow:
                PaintArrow(hdc, canvas_rect, obj);
                break;
            
            case AnnotationKind::Ellipse:
            case AnnotationKind::Text:
            case AnnotationKind::Mosaic:
            case AnnotationKind::Highlighter:
            case AnnotationKind::NumberMarker:
                // TODO: Implement in later tasks
                break;
        }
    }
}
```

- [ ] **Step 6: Update capture_overlay.cpp to use new renderer**

```cpp
// In capture_overlay.cpp, replace the existing PaintAnnotationRect call with:
PaintAnnotation(buffer_device_context, preview_rect, annotation_object);
```

- [ ] **Step 7: Write test for line rendering**

```cpp
// tests/feature_capture/annotation_renderer_test.cpp
#include <gtest/gtest.h>
#include "feature_capture/annotation_renderer.h"

namespace capturezy::feature_capture
{
    class AnnotationRendererTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            // Create a test HDC and bitmap
            hdc = CreateCompatibleDC(nullptr);
            bitmap = CreateCompatibleBitmap(hdc, 800, 600);
            old_bitmap = SelectObject(hdc, bitmap);
        }
        
        void TearDown() override
        {
            SelectObject(hdc, old_bitmap);
            DeleteObject(bitmap);
            DeleteDC(hdc);
        }
        
        HDC hdc;
        HBITMAP bitmap;
        HGDIOBJ old_bitmap;
    };

    TEST_F(AnnotationRendererTest, PaintLine_Smoke)
    {
        LineData line{.start = {0.1F, 0.2F}, .end = {0.8F, 0.7F}};
        AnnotationObject obj{
            .id = 1,
            .kind = AnnotationKind::Line,
            .bounds = {0.1F, 0.2F, 0.8F, 0.7F},
            .style = {.color = AnnotationColor::Red, .line_width = AnnotationLineWidth::Medium},
            .type_data = line
        };
        
        RECT canvas{.left = 0, .top = 0, .right = 800, .bottom = 600};
        
        // Should not crash
        PaintAnnotation(hdc, canvas, obj);
        
        SUCCEED();
    }

    TEST_F(AnnotationRendererTest, PaintArrow_Smoke)
    {
        ArrowData arrow{
            .start = {0.1F, 0.2F},
            .end = {0.8F, 0.7F},
            .head_style = ArrowHeadStyle::Solid,
            .head_size = 0.1F
        };
        AnnotationObject obj{
            .id = 1,
            .kind = AnnotationKind::Arrow,
            .bounds = {0.1F, 0.2F, 0.8F, 0.7F},
            .style = {.color = AnnotationColor::Blue, .line_width = AnnotationLineWidth::Thick},
            .type_data = arrow
        };
        
        RECT canvas{.left = 0, .top = 0, .right = 800, .bottom = 600};
        
        // Should not crash
        PaintAnnotation(hdc, canvas, obj);
        
        SUCCEED();
    }
}
```

- [ ] **Step 8: Run tests to verify they pass**

Run: `scripts/build.bat && out/build/x64-Debug/tests/feature_capture/annotation_renderer_test.exe`
Expected: All 2 tests PASS

- [ ] **Step 9: Commit**

```bash
git add src/feature_capture/annotation_renderer.h src/feature_capture/annotation_renderer.cpp src/feature_capture/capture_overlay.cpp tests/feature_capture/annotation_renderer_test.cpp
git commit -m "feat: implement rendering for Line and Arrow annotations"
```

---

## Task 4: Add Ellipse Annotation Type

**Files:**
- Modify: `src/feature_capture/capture_annotation.cpp:238-314` (add ellipse case to HitTestObject)
- Modify: `src/feature_capture/annotation_renderer.cpp` (add PaintEllipse)

- [ ] **Step 1: Implement ellipse hit testing**

```cpp
// In HitTestObject, add to switch statement
case AnnotationKind::Ellipse:
    return HitTestEllipse(object, point_rect, border_tolerance_normalized);
```

```cpp
// New function
AnnotationHitTestResult AnnotationSession::HitTestEllipse(
    AnnotationObject const &object,
    NormalizedRectF point_rect,
    float tolerance_normalized)
{
    float const point_x = (point_rect.left + point_rect.right) * 0.5F;
    float const point_y = (point_rect.top + point_rect.bottom) * 0.5F;
    
    // Ellipse center and radii
    float const cx = (object.bounds.left + object.bounds.right) * 0.5F;
    float const cy = (object.bounds.top + object.bounds.bottom) * 0.5F;
    float const rx = (object.bounds.right - object.bounds.left) * 0.5F;
    float const ry = (object.bounds.bottom - object.bounds.top) * 0.5F;
    
    if (rx < 0.001F || ry < 0.001F)
    {
        return AnnotationHitTestResult{.kind = AnnotationHitKind::None, .object_id = object.id, .handle_index = -1};
    }
    
    // Check if point is on ellipse border
    // Ellipse equation: ((x-cx)/rx)^2 + ((y-cy)/ry)^2 = 1
    float const dx = point_x - cx;
    float const dy = point_y - cy;
    float const ellipse_value = (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry);
    
    // Check if near border
    float const tolerance_factor = tolerance_normalized / std::min(rx, ry);
    if (std::abs(ellipse_value - 1.0F) <= tolerance_factor)
    {
        return AnnotationHitTestResult{
            .kind = AnnotationHitKind::Border,
            .object_id = object.id,
            .handle_index = -1
        };
    }
    
    // Check if inside (for fill)
    if (ellipse_value < 1.0F)
    {
        return AnnotationHitTestResult{
            .kind = AnnotationHitKind::Fill,
            .object_id = object.id,
            .handle_index = -1
        };
    }
    
    // Check control points (4 cardinal points)
    float const control_points[4][2] = {
        {cx, object.bounds.top},      // Top
        {object.bounds.right, cy},    // Right
        {cx, object.bounds.bottom},   // Bottom
        {object.bounds.left, cy}      // Left
    };
    
    for (int i = 0; i < 4; ++i)
    {
        float const cdx = point_x - control_points[i][0];
        float const cdy = point_y - control_points[i][1];
        if (std::sqrt(cdx * cdx + cdy * cdy) <= tolerance_normalized)
        {
            return AnnotationHitTestResult{
                .kind = AnnotationHitKind::ControlPoint,
                .object_id = object.id,
                .handle_index = i
            };
        }
    }
    
    return AnnotationHitTestResult{.kind = AnnotationHitKind::None, .object_id = object.id, .handle_index = -1};
}
```

- [ ] **Step 2: Implement ellipse rendering**

```cpp
// In annotation_renderer.cpp
void PaintEllipse(HDC hdc, RECT const& rect, AnnotationStyle const& style)
{
    // Determine pen width
    int pen_width = 2;
    switch (style.line_width)
    {
        case AnnotationLineWidth::Thin: pen_width = 1; break;
        case AnnotationLineWidth::Medium: pen_width = 2; break;
        case AnnotationLineWidth::Thick: pen_width = 4; break;
    }
    
    // Determine colors
    COLORREF pen_color = RGB(0, 0, 0);
    switch (style.color)
    {
        case AnnotationColor::Red: pen_color = RGB(255, 0, 0); break;
        case AnnotationColor::Green: pen_color = RGB(0, 255, 0); break;
        case AnnotationColor::Blue: pen_color = RGB(0, 0, 255); break;
        case AnnotationColor::Yellow: pen_color = RGB(255, 255, 0); break;
        case AnnotationColor::White: pen_color = RGB(255, 255, 255); break;
    }
    
    HPEN pen = CreatePen(PS_SOLID, pen_width, pen_color);
    HBRUSH brush = style.fill_enabled ? CreateSolidBrush(pen_color) : static_cast<HBRUSH>(GetStockObject(HOLLOW_BRUSH));
    
    HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, pen));
    HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(hdc, brush));
    
    Ellipse(hdc, rect.left, rect.top, rect.right, rect.bottom);
    
    if (style.fill_enabled)
    {
        SelectObject(hdc, old_brush);
        DeleteObject(brush);
    }
    else
    {
        SelectObject(hdc, old_brush);
    }
    
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
}
```

- [ ] **Step 3: Add ellipse to PaintAnnotation dispatcher**

```cpp
// In PaintAnnotation
case AnnotationKind::Ellipse:
    PaintEllipse(hdc, NormalizedToPixel(canvas_rect, obj.bounds), obj.style);
    break;
```

- [ ] **Step 4: Write tests**

```cpp
// Add to annotation_hit_test_test.cpp
TEST_F(AnnotationHitTestTest, HitTestEllipse_OnBorder)
{
    AnnotationObject obj{
        .id = 1,
        .kind = AnnotationKind::Ellipse,
        .bounds = {0.2F, 0.2F, 0.8F, 0.8F}
    };
    
    // Point on right edge of ellipse
    NormalizedRectF point{0.79F, 0.49F, 0.81F, 0.51F};
    auto result = AnnotationSession::HitTestObject(obj, point, 0.03F, 0.02F);
    
    EXPECT_EQ(result.kind, AnnotationHitKind::Border);
}

TEST_F(AnnotationHitTestTest, HitTestEllipse_Inside)
{
    AnnotationObject obj{
        .id = 1,
        .kind = AnnotationKind::Ellipse,
        .bounds = {0.2F, 0.2F, 0.8F, 0.8F}
    };
    
    // Point at center
    NormalizedRectF point{0.49F, 0.49F, 0.51F, 0.51F};
    auto result = AnnotationSession::HitTestObject(obj, point, 0.03F, 0.02F);
    
    EXPECT_EQ(result.kind, AnnotationHitKind::Fill);
}
```

- [ ] **Step 5: Run tests**

Run: `scripts/build.bat && out/build/x64-Debug/tests/feature_capture/annotation_hit_test_test.exe --gtest_filter=*Ellipse*`
Expected: All ellipse tests PASS

- [ ] **Step 6: Commit**

```bash
git add src/feature_capture/capture_annotation.cpp src/feature_capture/annotation_renderer.cpp tests/feature_capture/annotation_hit_test_test.cpp
git commit -m "feat: implement Ellipse annotation with hit testing and rendering"
```

---

## Task 5: Add Text Annotation Type

**Files:**
- Modify: `src/feature_capture/capture_annotation.h` (add TextData struct, extend variant)
- Modify: `src/feature_capture/capture_annotation.cpp` (add text hit testing)
- Modify: `src/feature_capture/annotation_renderer.cpp` (add text rendering)

- [ ] **Step 1: Add TextData structure**

```cpp
// In capture_annotation.h
struct TextData
{
    std::wstring text{};
    float font_size_normalized{0.05F}; // relative to canvas height
};
```

```cpp
// Extend variant in AnnotationObject
std::variant<std::monostate, LineData, ArrowData, TextData> type_data{};
```

- [ ] **Step 2: Implement text hit testing**

```cpp
// In HitTestObject switch
case AnnotationKind::Text:
    // Text uses rectangle hit test logic
    break; // Fall through to rectangle logic
```

- [ ] **Step 3: Implement text rendering**

```cpp
void PaintText(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj)
{
    auto const* text_data = std::get_if<TextData>(&obj.type_data);
    if (!text_data || text_data->text.empty())
    {
        return;
    }
    
    RECT rect = NormalizedToPixel(canvas_rect, obj.bounds);
    
    // Calculate font size in pixels
    float const canvas_height = static_cast<float>(canvas_rect.bottom - canvas_rect.top);
    int const font_height = static_cast<int>(text_data->font_size_normalized * canvas_height);
    
    // Determine text color
    COLORREF text_color = RGB(0, 0, 0);
    switch (obj.style.color)
    {
        case AnnotationColor::Red: text_color = RGB(255, 0, 0); break;
        case AnnotationColor::Green: text_color = RGB(0, 255, 0); break;
        case AnnotationColor::Blue: text_color = RGB(0, 0, 255); break;
        case AnnotationColor::Yellow: text_color = RGB(255, 255, 0); break;
        case AnnotationColor::White: text_color = RGB(255, 255, 255); break;
    }
    
    // Create font
    HFONT font = CreateFontW(
        font_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei"
    );
    
    HFONT old_font = static_cast<HFONT>(SelectObject(hdc, font));
    SetTextColor(hdc, text_color);
    SetBkMode(hdc, TRANSPARENT);
    
    // Draw text
    DrawTextW(hdc, text_data->text.c_str(), -1, &rect, DT_LEFT | DT_TOP | DT_WORDBREAK);
    
    SelectObject(hdc, old_font);
    DeleteObject(font);
}
```

- [ ] **Step 4: Add to PaintAnnotation dispatcher**

```cpp
case AnnotationKind::Text:
    PaintText(hdc, canvas_rect, obj);
    break;
```

- [ ] **Step 5: Write test**

```cpp
TEST_F(AnnotationSessionTest, AddTextAnnotation)
{
    TextData text{
        .text = L"Hello World",
        .font_size_normalized = 0.05F
    };
    
    AnnotationObject obj{
        .kind = AnnotationKind::Text,
        .bounds = {0.1F, 0.1F, 0.5F, 0.3F},
        .type_data = text
    };
    
    session.AddObject(obj);
    
    ASSERT_EQ(session.ObjectCount(), 1);
    EXPECT_EQ(session.GetObject(0).kind, AnnotationKind::Text);
    
    auto* text_ptr = std::get_if<TextData>(&session.GetObject(0).type_data);
    ASSERT_NE(text_ptr, nullptr);
    EXPECT_EQ(text_ptr->text, L"Hello World");
}
```

- [ ] **Step 6: Run test**

Run: `scripts/build.bat && out/build/x64-Debug/tests/feature_capture/annotation_session_test.exe --gtest_filter=*Text*`
Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add src/feature_capture/capture_annotation.h src/feature_capture/capture_annotation.cpp src/feature_capture/annotation_renderer.cpp tests/feature_capture/annotation_session_test.cpp
git commit -m "feat: implement Text annotation with rendering"
```

---

## Task 6: Add Toolbar Buttons for New Tools

**Files:**
- Modify: `src/feature_capture/capture_overlay.cpp:67-80` (add new ToolbarAction enum values)
- Modify: `src/feature_capture/capture_overlay.cpp:1354-1488` (add toolbar button metadata)
- Modify: `src/feature_capture/capture_overlay.cpp:2627-2706` (add handlers for new tool buttons)

- [ ] **Step 1: Add new toolbar actions to enum**

```cpp
// In ToolbarAction enum
ToolLine,
ToolArrow,
ToolEllipse,
ToolText,
```

- [ ] **Step 2: Add toolbar button metadata**

```cpp
// Add to kToolbarActionSpecs array
ToolbarActionSpec{.action = ToolbarAction::ToolLine,
                  .label = L"线",
                  .hint = L"直线工具",
                  .group = 0,
                  .index_in_group = 1,
                  .width = kToolbarToolButtonWidth,
                  .interactive = true},
ToolbarActionSpec{.action = ToolbarAction::ToolArrow,
                  .label = L"箭",
                  .hint = L"箭头工具",
                  .group = 0,
                  .index_in_group = 2,
                  .width = kToolbarToolButtonWidth,
                  .interactive = true},
ToolbarActionSpec{.action = ToolbarAction::ToolEllipse,
                  .label = L"圆",
                  .hint = L"椭圆工具",
                  .group = 0,
                  .index_in_group = 3,
                  .width = kToolbarToolButtonWidth,
                  .interactive = true},
ToolbarActionSpec{.action = ToolbarAction::ToolText,
                  .label = L"文",
                  .hint = L"文字工具",
                  .group = 0,
                  .index_in_group = 4,
                  .width = kToolbarToolButtonWidth,
                  .interactive = true},
```

- [ ] **Step 3: Update group count**

```cpp
// In ToolbarGroupActionCount
case 0:
    return 5; // 4 tools + 1 shape (now 5 tools total)
```

- [ ] **Step 4: Add handlers in ExecuteToolbarAction**

```cpp
if (action == ToolbarAction::ToolLine)
{
    annotation_session_.ToggleToolFamily(AnnotationToolFamily::Shape);
    annotation_session_.SetShapeVariant(ShapeToolVariant::Line);
    InvalidateToolbarVisual();
    return;
}

if (action == ToolbarAction::ToolArrow)
{
    annotation_session_.ToggleToolFamily(AnnotationToolFamily::Arrow);
    InvalidateToolbarVisual();
    return;
}

if (action == ToolbarAction::ToolEllipse)
{
    annotation_session_.ToggleToolFamily(AnnotationToolFamily::Shape);
    annotation_session_.SetShapeVariant(ShapeToolVariant::Ellipse);
    InvalidateToolbarVisual();
    return;
}

if (action == ToolbarAction::ToolText)
{
    annotation_session_.ToggleToolFamily(AnnotationToolFamily::Text);
    InvalidateToolbarVisual();
    return;
}
```

- [ ] **Step 5: Extend ShapeToolVariant enum**

```cpp
// In capture_annotation.h
enum class ShapeToolVariant : std::uint8_t
{
    Rectangle,
    Ellipse,
    Line,
};
```

- [ ] **Step 6: Build and test**

Run: `scripts/build.bat`
Expected: Build succeeds

- [ ] **Step 7: Commit**

```bash
git add src/feature_capture/capture_annotation.h src/feature_capture/capture_overlay.cpp
git commit -m "feat: add toolbar buttons for Line, Arrow, Ellipse, and Text tools"
```

---

## Task 7: Implement Tool Creation Interaction

**Files:**
- Modify: `src/feature_capture/capture_overlay.cpp:2076-2112` (BeginCreateAnnotation)
- Modify: `src/feature_capture/capture_overlay.cpp:2114-2133` (UpdateCreateAnnotation)
- Modify: `src/feature_capture/capture_overlay.cpp:2815-2836` (CompletePointerSelection)

- [ ] **Step 1: Update CompletePointerSelection to create correct annotation type**

```cpp
if (pointer_drag_mode == PointerDragMode::CreateAnnotation)
{
    bool const has_meaningful_annotation = has_draft_annotation_ &&
                                           (std::abs(drag_current_.x - drag_start_.x) >= kDragThreshold ||
                                            std::abs(drag_current_.y - drag_start_.y) >= kDragThreshold);
    if (has_meaningful_annotation)
    {
        AnnotationObject new_obj{
            .id = 0,
            .kind = AnnotationKind::Rectangle, // Default
            .bounds = draft_annotation_bounds_,
            .style = annotation_session_.ActiveStyle(),
            .type_data = std::monostate{}
        };
        
        // Determine actual kind based on active tool
        if (annotation_session_.IsToolFamilyActive(AnnotationToolFamily::Shape))
        {
            switch (annotation_session_.ActiveShapeVariant())
            {
                case ShapeToolVariant::Rectangle:
                    new_obj.kind = AnnotationKind::Rectangle;
                    break;
                case ShapeToolVariant::Ellipse:
                    new_obj.kind = AnnotationKind::Ellipse;
                    break;
                case ShapeToolVariant::Line:
                    new_obj.kind = AnnotationKind::Line;
                    new_obj.type_data = LineData{
                        .start = {draft_annotation_bounds_.left, draft_annotation_bounds_.top},
                        .end = {draft_annotation_bounds_.right, draft_annotation_bounds_.bottom}
                    };
                    break;
            }
        }
        else if (annotation_session_.IsToolFamilyActive(AnnotationToolFamily::Arrow))
        {
            new_obj.kind = AnnotationKind::Arrow;
            new_obj.type_data = ArrowData{
                .start = {draft_annotation_bounds_.left, draft_annotation_bounds_.top},
                .end = {draft_annotation_bounds_.right, draft_annotation_bounds_.bottom},
                .head_style = ArrowHeadStyle::Solid,
                .head_size = 0.05F
            };
        }
        else if (annotation_session_.IsToolFamilyActive(AnnotationToolFamily::Text))
        {
            new_obj.kind = AnnotationKind::Text;
            new_obj.type_data = TextData{
                .text = L"Text", // TODO: Show input dialog
                .font_size_normalized = 0.05F
            };
        }
        
        annotation_session_.AddObject(new_obj);
        InvalidateToolbarVisual();
        InvalidateAnnotationCanvas();
    }
    has_draft_annotation_ = false;
    draft_annotation_bounds_ = {};
    drag_in_progress_ = false;
    InvalidateAnnotationCanvas();
    UpdateCursorForOverlayPoint(drag_current_);
    return;
}
```

- [ ] **Step 2: Build and manually test**

Run: `scripts/build.bat`
Expected: Build succeeds

Manually test:
1. Select Line tool, drag to create line
2. Select Arrow tool, drag to create arrow
3. Select Ellipse tool, drag to create ellipse
4. Select Text tool, drag to create text box

- [ ] **Step 3: Commit**

```bash
git add src/feature_capture/capture_overlay.cpp
git commit -m "feat: implement creation interaction for Line, Arrow, Ellipse, and Text tools"
```

---

## Task 8: Add Mosaic and Highlighter Types

**Files:**
- Modify: `src/feature_capture/capture_annotation.h` (add MosaicData, HighlighterData)
- Modify: `src/feature_capture/capture_annotation.cpp` (add hit testing)
- Modify: `src/feature_capture/annotation_renderer.cpp` (add rendering)

- [ ] **Step 1: Add data structures**

```cpp
struct MosaicData
{
    float block_size_normalized{0.02F};
    float strength{1.0F}; // 0.0 to 1.0
};

struct HighlighterData
{
    std::vector<NormalizedPointF> path{};
    float width_normalized{0.02F};
};
```

```cpp
// Extend variant
std::variant<std::monostate, LineData, ArrowData, TextData, MosaicData, HighlighterData> type_data{};
```

- [ ] **Step 2: Implement hit testing (use rectangle logic for both)**

```cpp
case AnnotationKind::Mosaic:
case AnnotationKind::Highlighter:
    // Use rectangle hit test logic
    break;
```

- [ ] **Step 3: Implement mosaic rendering (placeholder)**

```cpp
void PaintMosaic(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj)
{
    // TODO: Implement pixel-based mosaic effect
    // For now, just draw a semi-transparent overlay
    RECT rect = NormalizedToPixel(canvas_rect, obj.bounds);
    
    HBRUSH brush = CreateSolidBrush(RGB(128, 128, 128));
    HPEN pen = static_cast<HPEN>(GetStockObject(NULL_PEN));
    
    HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(hdc, brush));
    HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, pen));
    
    SetBkMode(hdc, TRANSPARENT);
    Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
    
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    DeleteObject(brush);
}
```

- [ ] **Step 4: Implement highlighter rendering**

```cpp
void PaintHighlighter(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj)
{
    auto const* highlighter_data = std::get_if<HighlighterData>(&obj.type_data);
    if (!highlighter_data || highlighter_data->path.size() < 2)
    {
        return;
    }
    
    // Determine color
    COLORREF color = RGB(255, 255, 0); // Default yellow
    switch (obj.style.color)
    {
        case AnnotationColor::Red: color = RGB(255, 0, 0); break;
        case AnnotationColor::Green: color = RGB(0, 255, 0); break;
        case AnnotationColor::Blue: color = RGB(0, 0, 255); break;
        case AnnotationColor::Yellow: color = RGB(255, 255, 0); break;
        case AnnotationColor::White: color = RGB(255, 255, 255); break;
    }
    
    // Calculate pen width
    float const canvas_width = static_cast<float>(canvas_rect.right - canvas_rect.left);
    int const pen_width = static_cast<int>(highlighter_data->width_normalized * canvas_width);
    
    HPEN pen = CreatePen(PS_SOLID, pen_width, color);
    HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, pen));
    SetBkMode(hdc, TRANSPARENT);
    
    // Draw path
    POINT prev = NormalizedToPixel(canvas_rect, highlighter_data->path[0]);
    for (size_t i = 1; i < highlighter_data->path.size(); ++i)
    {
        POINT curr = NormalizedToPixel(canvas_rect, highlighter_data->path[i]);
        MoveToEx(hdc, prev.x, prev.y, nullptr);
        LineTo(hdc, curr.x, curr.y);
        prev = curr;
    }
    
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
}
```

- [ ] **Step 5: Add to PaintAnnotation dispatcher**

```cpp
case AnnotationKind::Mosaic:
    PaintMosaic(hdc, canvas_rect, obj);
    break;

case AnnotationKind::Highlighter:
    PaintHighlighter(hdc, canvas_rect, obj);
    break;
```

- [ ] **Step 6: Build and test**

Run: `scripts/build.bat`
Expected: Build succeeds

- [ ] **Step 7: Commit**

```bash
git add src/feature_capture/capture_annotation.h src/feature_capture/capture_annotation.cpp src/feature_capture/annotation_renderer.cpp
git commit -m "feat: add Mosaic and Highlighter annotation types"
```

---

## Task 9: Add NumberMarker Type

**Files:**
- Modify: `src/feature_capture/capture_annotation.h` (add NumberMarkerData)
- Modify: `src/feature_capture/capture_annotation.cpp` (add hit testing)
- Modify: `src/feature_capture/annotation_renderer.cpp` (add rendering)

- [ ] **Step 1: Add NumberMarkerData structure**

```cpp
struct NumberMarkerData
{
    int number{1};
    float radius_normalized{0.03F};
};
```

```cpp
// Extend variant
std::variant<std::monostate, LineData, ArrowData, TextData, MosaicData, HighlighterData, NumberMarkerData> type_data{};
```

- [ ] **Step 2: Implement hit testing (use rectangle logic)**

```cpp
case AnnotationKind::NumberMarker:
    // Use rectangle hit test logic
    break;
```

- [ ] **Step 3: Implement rendering**

```cpp
void PaintNumberMarker(HDC hdc, RECT const& canvas_rect, AnnotationObject const& obj)
{
    auto const* marker_data = std::get_if<NumberMarkerData>(&obj.type_data);
    if (!marker_data)
    {
        return;
    }
    
    RECT rect = NormalizedToPixel(canvas_rect, obj.bounds);
    
    // Calculate center and radius
    int const cx = (rect.left + rect.right) / 2;
    int const cy = (rect.top + rect.bottom) / 2;
    float const canvas_width = static_cast<float>(canvas_rect.right - canvas_rect.left);
    int const radius = static_cast<int>(marker_data->radius_normalized * canvas_width);
    
    // Determine color
    COLORREF color = RGB(255, 0, 0); // Default red
    switch (obj.style.color)
    {
        case AnnotationColor::Red: color = RGB(255, 0, 0); break;
        case AnnotationColor::Green: color = RGB(0, 255, 0); break;
        case AnnotationColor::Blue: color = RGB(0, 0, 255); break;
        case AnnotationColor::Yellow: color = RGB(255, 255, 0); break;
        case AnnotationColor::White: color = RGB(255, 255, 255); break;
    }
    
    // Draw filled circle
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
    
    HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(hdc, brush));
    HPEN old_pen = static_cast<HPEN>(SelectObject(hdc, pen));
    
    Ellipse(hdc, cx - radius, cy - radius, cx + radius, cy + radius);
    
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
    
    // Draw number text
    wchar_t number_text[16];
    _snwprintf_s(number_text, _countof(number_text), _TRUNCATE, L"%d", marker_data->number);
    
    HFONT font = CreateFontW(
        radius, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial"
    );
    
    HFONT old_font = static_cast<HFONT>(SelectObject(hdc, font));
    SetTextColor(hdc, RGB(255, 255, 255));
    SetBkMode(hdc, TRANSPARENT);
    
    RECT text_rect{.left = cx - radius, .top = cy - radius, .right = cx + radius, .bottom = cy + radius};
    DrawTextW(hdc, number_text, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    
    SelectObject(hdc, old_font);
    DeleteObject(font);
}
```

- [ ] **Step 4: Add to PaintAnnotation dispatcher**

```cpp
case AnnotationKind::NumberMarker:
    PaintNumberMarker(hdc, canvas_rect, obj);
    break;
```

- [ ] **Step 5: Build and test**

Run: `scripts/build.bat`
Expected: Build succeeds

- [ ] **Step 6: Commit**

```bash
git add src/feature_capture/capture_annotation.h src/feature_capture/capture_annotation.cpp src/feature_capture/annotation_renderer.cpp
git commit -m "feat: implement NumberMarker annotation type"
```

---

## Task 10: Final Integration and Testing

**Files:**
- Modify: `tests/feature_capture/annotation_session_test.cpp` (add comprehensive tests)
- Modify: `src/feature_capture/capture_overlay.cpp` (add remaining toolbar buttons for Mosaic, Highlighter, NumberMarker)

- [ ] **Step 1: Add toolbar buttons for remaining tools**

```cpp
// Add to ToolbarAction enum
ToolMosaic,
ToolHighlighter,
ToolNumberMarker,
```

```cpp
// Add to kToolbarActionSpecs
ToolbarActionSpec{.action = ToolbarAction::ToolMosaic,
                  .label = L"马",
                  .hint = L"马赛克工具",
                  .group = 0,
                  .index_in_group = 5,
                  .width = kToolbarToolButtonWidth,
                  .interactive = true},
ToolbarActionSpec{.action = ToolbarAction::ToolHighlighter,
                  .label = L"荧",
                  .hint = L"荧光笔工具",
                  .group = 0,
                  .index_in_group = 6,
                  .width = kToolbarToolButtonWidth,
                  .interactive = true},
ToolbarActionSpec{.action = ToolbarAction::ToolNumberMarker,
                  .label = L"序",
                  .hint = L"序号标注工具",
                  .group = 0,
                  .index_in_group = 7,
                  .width = kToolbarToolButtonWidth,
                  .interactive = true},
```

```cpp
// Update group count
case 0:
    return 8; // All 8 tools
```

- [ ] **Step 2: Add handlers**

```cpp
if (action == ToolbarAction::ToolMosaic)
{
    annotation_session_.ToggleToolFamily(AnnotationToolFamily::Mosaic);
    InvalidateToolbarVisual();
    return;
}

if (action == ToolbarAction::ToolHighlighter)
{
    // Highlighter is a variant of Mosaic for now
    annotation_session_.ToggleToolFamily(AnnotationToolFamily::Mosaic);
    InvalidateToolbarVisual();
    return;
}

if (action == ToolbarAction::ToolNumberMarker)
{
    // NumberMarker is a variant of Text for now
    annotation_session_.ToggleToolFamily(AnnotationToolFamily::Text);
    InvalidateToolbarVisual();
    return;
}
```

- [ ] **Step 3: Update CompletePointerSelection for new tools**

```cpp
// Add to the annotation creation logic
else if (annotation_session_.IsToolFamilyActive(AnnotationToolFamily::Mosaic))
{
    new_obj.kind = AnnotationKind::Mosaic;
    new_obj.type_data = MosaicData{
        .block_size_normalized = 0.02F,
        .strength = 1.0F
    };
}
```

- [ ] **Step 4: Write comprehensive integration tests**

```cpp
TEST_F(AnnotationSessionTest, AllAnnotationTypes)
{
    // Rectangle
    session.AddObject({.kind = AnnotationKind::Rectangle, .bounds = {0.1F, 0.1F, 0.3F, 0.3F}});
    
    // Line
    session.AddObject({
        .kind = AnnotationKind::Line,
        .bounds = {0.1F, 0.4F, 0.3F, 0.6F},
        .type_data = LineData{{0.1F, 0.4F}, {0.3F, 0.6F}}
    });
    
    // Arrow
    session.AddObject({
        .kind = AnnotationKind::Arrow,
        .bounds = {0.4F, 0.1F, 0.6F, 0.3F},
        .type_data = ArrowData{{0.4F, 0.1F}, {0.6F, 0.3F}}
    });
    
    // Ellipse
    session.AddObject({.kind = AnnotationKind::Ellipse, .bounds = {0.4F, 0.4F, 0.6F, 0.6F}});
    
    // Text
    session.AddObject({
        .kind = AnnotationKind::Text,
        .bounds = {0.7F, 0.1F, 0.9F, 0.3F},
        .type_data = TextData{L"Test", 0.05F}
    });
    
    EXPECT_EQ(session.ObjectCount(), 5);
    
    // Test selection, move, delete work for all types
    session.SelectObject(1);
    EXPECT_TRUE(session.HasSelectedObject());
    
    session.MoveSelectedObject({0.15F, 0.15F, 0.35F, 0.35F});
    EXPECT_FLOAT_EQ(session.GetObject(0).bounds.left, 0.15F);
    
    session.DeleteSelectedObject();
    EXPECT_EQ(session.ObjectCount(), 4);
}
```

- [ ] **Step 5: Run all tests**

Run: `scripts/build.bat && out/build/x64-Debug/tests/feature_capture/annotation_session_test.exe`
Expected: All tests PASS

- [ ] **Step 6: Manual testing checklist**

- [ ] Create Rectangle annotation
- [ ] Create Line annotation
- [ ] Create Arrow annotation
- [ ] Create Ellipse annotation
- [ ] Create Text annotation
- [ ] Create Mosaic annotation
- [ ] Create Highlighter annotation (if implemented)
- [ ] Create NumberMarker annotation
- [ ] Select each type and verify selection adornments
- [ ] Move each type
- [ ] Resize each type
- [ ] Delete each type
- [ ] Undo/Redo for each operation
- [ ] Style changes apply to each type

- [ ] **Step 7: Commit**

```bash
git add src/feature_capture/capture_overlay.cpp tests/feature_capture/annotation_session_test.cpp
git commit -m "feat: complete Phase 2 - all 7 new annotation types integrated"
```

---

## Summary

This plan implements 7 new annotation types (Line, Arrow, Ellipse, Text, Mosaic, Highlighter, NumberMarker) in 10 tasks:

1. **Data Model Extension** - Add type-specific data structures
2. **Line/Arrow Hit Testing** - Point-to-segment distance calculation
3. **Line/Arrow Rendering** - GDI drawing with arrowheads
4. **Ellipse** - Hit testing and rendering
5. **Text** - Font rendering with Microsoft YaHei
6. **Toolbar Integration** - Add buttons for all tools
7. **Creation Interaction** - Wire up drag-to-create for all types
8. **Mosaic/Highlighter** - Specialized rendering effects
9. **NumberMarker** - Circular badges with auto-numbering
10. **Final Integration** - Comprehensive testing and polish

All new types reuse the existing selection, move, resize, delete, and undo/redo infrastructure by operating on the unified `bounds` field.
