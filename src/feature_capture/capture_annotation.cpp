#include "feature_capture/capture_annotation.h"

namespace capturezy
{
    namespace feature_capture
    {
        void AnnotationSession::Reset() noexcept
        {
            active_tool_ = AnnotationTool::Arrow;
            objects_.clear();
            undo_depth_ = 0;
            redo_depth_ = 0;
        }

        void AnnotationSession::SetActiveTool(AnnotationTool tool) noexcept
        {
            active_tool_ = tool;
        }

        AnnotationTool AnnotationSession::ActiveTool() const noexcept
        {
            return active_tool_;
        }

        std::vector<AnnotationObject> const &AnnotationSession::Objects() const noexcept
        {
            return objects_;
        }

        bool AnnotationSession::CanUndo() const noexcept
        {
            return undo_depth_ > 0;
        }

        bool AnnotationSession::CanRedo() const noexcept
        {
            return redo_depth_ > 0;
        }

        bool AnnotationSession::Undo() noexcept
        {
            if (!CanUndo())
            {
                return false;
            }

            --undo_depth_;
            ++redo_depth_;
            return true;
        }

        bool AnnotationSession::Redo() noexcept
        {
            if (!CanRedo())
            {
                return false;
            }

            ++undo_depth_;
            --redo_depth_;
            return true;
        }

        void AnnotationSession::MarkEdited() noexcept
        {
            ++undo_depth_;
            redo_depth_ = 0;
        }
    } // namespace feature_capture
} // namespace capturezy
