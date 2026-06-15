#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace capturezy::feature_capture
{
    enum class AnnotationToolFamily : std::uint8_t
    {
        None,
        Shape,
        Arrow,
        Text,
        Mosaic,
    };

    enum class ShapeToolVariant : std::uint8_t
    {
        Rectangle,
    };

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

    enum class AnnotationHitKind : std::uint8_t
    {
        None,
        Fill,
        Border,
        EdgeMidpoint,
        ControlPoint,
    };

    using AnnotationObjectId = std::uint32_t;

    enum class AnnotationColor : std::uint8_t
    {
        Yellow,
        Red,
        Green,
        Blue,
        White,
    };

    enum class AnnotationLineWidth : std::uint8_t
    {
        Thin,
        Medium,
        Thick,
    };

    struct AnnotationStyle
    {
        AnnotationColor color{AnnotationColor::Yellow};
        AnnotationLineWidth line_width{AnnotationLineWidth::Medium};
        bool fill_enabled{false};
    };

    struct NormalizedRectF
    {
        float left{0.0F};
        float top{0.0F};
        float right{0.0F};
        float bottom{0.0F};
    };

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

    enum class ArrowHeadStyle : std::uint8_t
    {
        Solid,
        Outline,
    };

    struct ArrowData
    {
        NormalizedPointF start{};
        NormalizedPointF end{};
        ArrowHeadStyle head_style{ArrowHeadStyle::Solid};
        float head_size{0.05F}; // normalized, relative to line length
    };

    struct TextData
    {
        std::wstring content{};
        float font_size{16.0F}; // in pixels
    };

    struct AnnotationObject
    {
        AnnotationObjectId id{0};
        AnnotationKind kind{AnnotationKind::Rectangle};
        NormalizedRectF bounds{};
        AnnotationStyle style{};

        // Type-specific data (only used for certain types)
        std::variant<std::monostate, LineData, ArrowData, TextData> type_data{};
    };

    struct AnnotationHitTestResult
    {
        AnnotationHitKind kind{AnnotationHitKind::None};
        AnnotationObjectId object_id{0};
        int handle_index{-1};
    };

    // 当前先建立工具族、默认变体、历史栈和对象选中模型，二级下拉与更多样式后续再补。
    class AnnotationSession final
    {
      public:
        void Reset() noexcept;
        void ToggleToolFamily(AnnotationToolFamily family) noexcept;
        void SetShapeVariant(ShapeToolVariant variant) noexcept;
        [[nodiscard]] AnnotationToolFamily ActiveToolFamily() const noexcept;
        [[nodiscard]] ShapeToolVariant ActiveShapeVariant() const noexcept;
        [[nodiscard]] bool IsToolFamilyActive(AnnotationToolFamily family) const noexcept;
        [[nodiscard]] std::vector<AnnotationObject> const &Objects() const noexcept;
        [[nodiscard]] bool CanUndo() const noexcept;
        [[nodiscard]] bool CanRedo() const noexcept;
        void AddObject(AnnotationObject object);
        bool Undo();
        bool Redo();

        [[nodiscard]] std::optional<AnnotationObjectId> SelectedObjectId() const noexcept;
        [[nodiscard]] bool HasSelectedObject() const noexcept;
        [[nodiscard]] AnnotationObject const *SelectedObject() const noexcept;
        void SelectObject(AnnotationObjectId object_id) noexcept;
        void DeselectObject() noexcept;
        bool MoveSelectedObject(NormalizedRectF new_bounds);
        bool ResizeSelectedObject(NormalizedRectF new_bounds);
        bool DeleteSelectedObject();
        void SetSelectedObjectStyle(AnnotationStyle style);
        [[nodiscard]] AnnotationStyle ActiveStyle() const noexcept;
        void SetActiveStyle(AnnotationStyle style) noexcept;

        [[nodiscard]] static AnnotationHitTestResult HitTestObject(AnnotationObject const &object,
                                                                   NormalizedRectF point_rect,
                                                                   float control_point_radius_normalized,
                                                                   float border_tolerance_normalized);

        [[nodiscard]] static AnnotationHitTestResult HitTestLine(AnnotationObject const &object,
                                                                 NormalizedRectF point_rect,
                                                                 float control_point_radius_normalized,
                                                                 float border_tolerance_normalized);

      private:
        void PushUndoSnapshot() noexcept;

        AnnotationToolFamily active_tool_family_{AnnotationToolFamily::None};
        ShapeToolVariant active_shape_variant_{ShapeToolVariant::Rectangle};
        AnnotationStyle active_style_{};
        std::vector<AnnotationObject> objects_;
        std::vector<std::vector<AnnotationObject>> undo_stack_;
        std::vector<std::vector<AnnotationObject>> redo_stack_;
        std::optional<AnnotationObjectId> selected_object_id_{};
        AnnotationObjectId next_object_id_{1};
    };
} // namespace capturezy::feature_capture
