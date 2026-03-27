#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// clang-format off
#include <windows.h>
// clang-format on

namespace capturezy::feature_capture
{
    enum class AnnotationTool : std::uint8_t
    {
        Arrow,
        Pen,
        Text,
        Mosaic,
    };

    enum class AnnotationKind : std::uint8_t
    {
        Placeholder,
    };

    struct AnnotationObject
    {
        AnnotationKind kind{AnnotationKind::Placeholder};
        RECT bounds{};
    };

    // 当前先只提供工具、对象列表与历史状态来源，具体标注绘制与快照回放后续再接入。
    class AnnotationSession final
    {
      public:
        void Reset() noexcept;
        void SetActiveTool(AnnotationTool tool) noexcept;
        [[nodiscard]] AnnotationTool ActiveTool() const noexcept;
        [[nodiscard]] std::vector<AnnotationObject> const &Objects() const noexcept;
        [[nodiscard]] bool CanUndo() const noexcept;
        [[nodiscard]] bool CanRedo() const noexcept;
        bool Undo() noexcept;
        bool Redo() noexcept;
        void MarkEdited() noexcept;

      private:
        AnnotationTool active_tool_{AnnotationTool::Arrow};
        std::vector<AnnotationObject> objects_;
        std::size_t undo_depth_{0};
        std::size_t redo_depth_{0};
    };
} // namespace capturezy::feature_capture
