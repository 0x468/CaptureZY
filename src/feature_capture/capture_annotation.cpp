#include "feature_capture/capture_annotation.h"

namespace capturezy::feature_capture
{
    void AnnotationSession::Reset() noexcept
    {
        active_tool_family_ = AnnotationToolFamily::None;
        active_shape_variant_ = ShapeToolVariant::Rectangle;
        objects_.clear();
        undo_stack_.clear();
        redo_stack_.clear();
    }

    void AnnotationSession::ToggleToolFamily(AnnotationToolFamily family) noexcept
    {
        active_tool_family_ = active_tool_family_ == family ? AnnotationToolFamily::None : family;
    }

    void AnnotationSession::SetShapeVariant(ShapeToolVariant variant) noexcept
    {
        active_shape_variant_ = variant;
    }

    AnnotationToolFamily AnnotationSession::ActiveToolFamily() const noexcept
    {
        return active_tool_family_;
    }

    ShapeToolVariant AnnotationSession::ActiveShapeVariant() const noexcept
    {
        return active_shape_variant_;
    }

    bool AnnotationSession::IsToolFamilyActive(AnnotationToolFamily family) const noexcept
    {
        return active_tool_family_ == family;
    }

    std::vector<AnnotationObject> const &AnnotationSession::Objects() const noexcept
    {
        return objects_;
    }

    bool AnnotationSession::CanUndo() const noexcept
    {
        return !undo_stack_.empty();
    }

    bool AnnotationSession::CanRedo() const noexcept
    {
        return !redo_stack_.empty();
    }

    void AnnotationSession::AddObject(AnnotationObject object)
    {
        undo_stack_.push_back(objects_);
        objects_.push_back(object);
        redo_stack_.clear();
    }

    bool AnnotationSession::Undo()
    {
        if (!CanUndo())
        {
            return false;
        }

        redo_stack_.push_back(objects_);
        objects_ = undo_stack_.back();
        undo_stack_.pop_back();
        return true;
    }

    bool AnnotationSession::Redo()
    {
        if (!CanRedo())
        {
            return false;
        }

        undo_stack_.push_back(objects_);
        objects_ = redo_stack_.back();
        redo_stack_.pop_back();
        return true;
    }
} // namespace capturezy::feature_capture
