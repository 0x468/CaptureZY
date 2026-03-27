#pragma once

#include <cstddef>
#include <cstdint>
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
    };

    struct NormalizedRectF
    {
        float left{0.0F};
        float top{0.0F};
        float right{0.0F};
        float bottom{0.0F};
    };

    struct AnnotationObject
    {
        AnnotationKind kind{AnnotationKind::Rectangle};
        NormalizedRectF bounds{};
    };

    // 当前先建立工具族、默认变体和历史栈结构，二级下拉与更多样式后续再补。
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

      private:
        AnnotationToolFamily active_tool_family_{AnnotationToolFamily::None};
        ShapeToolVariant active_shape_variant_{ShapeToolVariant::Rectangle};
        std::vector<AnnotationObject> objects_;
        std::vector<std::vector<AnnotationObject>> undo_stack_;
        std::vector<std::vector<AnnotationObject>> redo_stack_;
    };
} // namespace capturezy::feature_capture
