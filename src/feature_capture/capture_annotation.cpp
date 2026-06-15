#include "feature_capture/capture_annotation.h"

#include <algorithm>
#include <cmath>

namespace capturezy::feature_capture
{
    void AnnotationSession::Reset() noexcept
    {
        active_tool_family_ = AnnotationToolFamily::None;
        active_shape_variant_ = ShapeToolVariant::Rectangle;
        active_style_ = {};
        objects_.clear();
        undo_stack_.clear();
        redo_stack_.clear();
        selected_object_id_.reset();
        next_object_id_ = 1;
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
        PushUndoSnapshot();
        object.id = next_object_id_++;
        objects_.push_back(object);
        redo_stack_.clear();
        selected_object_id_ = object.id;
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
        selected_object_id_.reset();
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
        selected_object_id_.reset();
        return true;
    }

    std::optional<AnnotationObjectId> AnnotationSession::SelectedObjectId() const noexcept
    {
        return selected_object_id_;
    }

    bool AnnotationSession::HasSelectedObject() const noexcept
    {
        return selected_object_id_.has_value();
    }

    AnnotationObject const *AnnotationSession::SelectedObject() const noexcept
    {
        if (!selected_object_id_.has_value())
        {
            return nullptr;
        }

        for (auto const &object : objects_)
        {
            if (object.id == selected_object_id_.value())
            {
                return &object;
            }
        }

        return nullptr;
    }

    void AnnotationSession::SelectObject(AnnotationObjectId object_id) noexcept
    {
        for (auto const &object : objects_)
        {
            if (object.id == object_id)
            {
                selected_object_id_ = object_id;
                return;
            }
        }

        selected_object_id_.reset();
    }

    void AnnotationSession::DeselectObject() noexcept
    {
        selected_object_id_.reset();
    }

    bool AnnotationSession::MoveSelectedObject(NormalizedRectF new_bounds)
    {
        if (!selected_object_id_.has_value())
        {
            return false;
        }

        for (auto &object : objects_)
        {
            if (object.id == selected_object_id_.value())
            {
                PushUndoSnapshot();
                object.bounds = new_bounds;
                redo_stack_.clear();
                return true;
            }
        }

        return false;
    }

    bool AnnotationSession::ResizeSelectedObject(NormalizedRectF new_bounds)
    {
        if (!selected_object_id_.has_value())
        {
            return false;
        }

        for (auto &object : objects_)
        {
            if (object.id == selected_object_id_.value())
            {
                PushUndoSnapshot();
                object.bounds = new_bounds;
                redo_stack_.clear();
                return true;
            }
        }

        return false;
    }

    bool AnnotationSession::DeleteSelectedObject()
    {
        if (!selected_object_id_.has_value())
        {
            return false;
        }

        AnnotationObjectId const target_id = selected_object_id_.value();
        auto it = std::find_if(objects_.begin(), objects_.end(),
                               [target_id](AnnotationObject const &obj) { return obj.id == target_id; });

        if (it == objects_.end())
        {
            return false;
        }

        PushUndoSnapshot();
        objects_.erase(it);
        redo_stack_.clear();
        selected_object_id_.reset();
        return true;
    }

    void AnnotationSession::SetSelectedObjectStyle(AnnotationStyle style)
    {
        if (!selected_object_id_.has_value())
        {
            return;
        }

        for (auto &object : objects_)
        {
            if (object.id == selected_object_id_.value())
            {
                PushUndoSnapshot();
                object.style = style;
                redo_stack_.clear();
                return;
            }
        }
    }

    AnnotationStyle AnnotationSession::ActiveStyle() const noexcept
    {
        return active_style_;
    }

    void AnnotationSession::SetActiveStyle(AnnotationStyle style) noexcept
    {
        active_style_ = style;
    }

    AnnotationHitTestResult AnnotationSession::HitTestObject(AnnotationObject const &object, NormalizedRectF point_rect,
                                                             float control_point_radius_normalized,
                                                             float border_tolerance_normalized)
    {
        if (object.kind == AnnotationKind::Line || object.kind == AnnotationKind::Arrow)
        {
            return HitTestLine(object, point_rect, control_point_radius_normalized, border_tolerance_normalized);
        }

        if (object.kind == AnnotationKind::Mosaic)
        {
            return HitTestRectangle(object, point_rect, control_point_radius_normalized, border_tolerance_normalized);
        }

        float const point_x = (point_rect.left + point_rect.right) * 0.5F;
        float const point_y = (point_rect.top + point_rect.bottom) * 0.5F;

        // 控制点检测优先于边界检测，控制点可能在对象边界之外。
        float const corners[4][2] = {
            {object.bounds.left, object.bounds.top},
            {object.bounds.right, object.bounds.top},
            {object.bounds.right, object.bounds.bottom},
            {object.bounds.left, object.bounds.bottom},
        };

        for (int i = 0; i < 4; ++i)
        {
            float const dx = point_x - corners[i][0];
            float const dy = point_y - corners[i][1];
            float const distance = std::sqrt((dx * dx) + (dy * dy));
            if (distance <= control_point_radius_normalized)
            {
                return AnnotationHitTestResult{
                    .kind = AnnotationHitKind::ControlPoint, .object_id = object.id, .handle_index = i};
            }
        }

        float const edge_midpoints[4][2] = {
            {(object.bounds.left + object.bounds.right) * 0.5F, object.bounds.top},
            {object.bounds.right, (object.bounds.top + object.bounds.bottom) * 0.5F},
            {(object.bounds.left + object.bounds.right) * 0.5F, object.bounds.bottom},
            {object.bounds.left, (object.bounds.top + object.bounds.bottom) * 0.5F},
        };

        for (int i = 0; i < 4; ++i)
        {
            float const dx = point_x - edge_midpoints[i][0];
            float const dy = point_y - edge_midpoints[i][1];
            float const distance = std::sqrt((dx * dx) + (dy * dy));
            if (distance <= control_point_radius_normalized)
            {
                return AnnotationHitTestResult{
                    .kind = AnnotationHitKind::EdgeMidpoint, .object_id = object.id, .handle_index = i + 4};
            }
        }

        bool const inside_bounds = point_x >= object.bounds.left && point_x <= object.bounds.right &&
                                   point_y >= object.bounds.top && point_y <= object.bounds.bottom;

        if (!inside_bounds)
        {
            return AnnotationHitTestResult{.kind = AnnotationHitKind::None, .object_id = object.id, .handle_index = -1};
        }

        // 检测是否在边中点附近：使用点到各边线段的最近距离而非仅到中心点距离。
        float const clamped_y = std::clamp(point_y, object.bounds.top, object.bounds.bottom);
        float const clamped_x = std::clamp(point_x, object.bounds.left, object.bounds.right);
        float const dist_to_left_edge = std::sqrt((point_x - object.bounds.left) * (point_x - object.bounds.left) +
                                                  (clamped_y - point_y) * (clamped_y - point_y));
        float const dist_to_right_edge = std::sqrt((point_x - object.bounds.right) * (point_x - object.bounds.right) +
                                                   (clamped_y - point_y) * (clamped_y - point_y));
        float const dist_to_top_edge = std::sqrt((clamped_x - point_x) * (clamped_x - point_x) +
                                                 (point_y - object.bounds.top) * (point_y - object.bounds.top));
        float const dist_to_bottom_edge = std::sqrt((clamped_x - point_x) * (clamped_x - point_x) +
                                                    (point_y - object.bounds.bottom) *
                                                        (point_y - object.bounds.bottom));

        float const min_edge_distance = std::min(
            {dist_to_left_edge, dist_to_right_edge, dist_to_top_edge, dist_to_bottom_edge});
        if (min_edge_distance <= border_tolerance_normalized)
        {
            return AnnotationHitTestResult{
                .kind = AnnotationHitKind::Border, .object_id = object.id, .handle_index = -1};
        }

        return AnnotationHitTestResult{.kind = AnnotationHitKind::Fill, .object_id = object.id, .handle_index = -1};
    }

    AnnotationHitTestResult AnnotationSession::HitTestLine(AnnotationObject const &object, NormalizedRectF point_rect,
                                                           float control_point_radius_normalized,
                                                           float border_tolerance_normalized)
    {
        float const point_x = (point_rect.left + point_rect.right) * 0.5F;
        float const point_y = (point_rect.top + point_rect.bottom) * 0.5F;

        // Extract start/end points from type_data.
        NormalizedPointF start{};
        NormalizedPointF end{};
        if (auto const *line = std::get_if<LineData>(&object.type_data))
        {
            start = line->start;
            end = line->end;
        }
        else if (auto const *arrow = std::get_if<ArrowData>(&object.type_data))
        {
            start = arrow->start;
            end = arrow->end;
        }
        else
        {
            return AnnotationHitTestResult{.kind = AnnotationHitKind::None, .object_id = object.id, .handle_index = -1};
        }

        // Control point detection: check distance to each endpoint.
        float const endpoints[2][2] = {
            {start.x, start.y},
            {end.x, end.y},
        };

        for (int i = 0; i < 2; ++i)
        {
            float const dx = point_x - endpoints[i][0];
            float const dy = point_y - endpoints[i][1];
            float const distance = std::sqrt((dx * dx) + (dy * dy));
            if (distance <= control_point_radius_normalized)
            {
                return AnnotationHitTestResult{
                    .kind = AnnotationHitKind::ControlPoint, .object_id = object.id, .handle_index = i};
            }
        }

        // Point-to-line-segment distance using projection.
        float const seg_dx = end.x - start.x;
        float const seg_dy = end.y - start.y;
        float const len_sq = (seg_dx * seg_dx) + (seg_dy * seg_dy);

        float distance = 0.0F;
        if (len_sq < 1e-12F)
        {
            // Degenerate line (start == end), just measure distance to the point.
            float const dx = point_x - start.x;
            float const dy = point_y - start.y;
            distance = std::sqrt((dx * dx) + (dy * dy));
        }
        else
        {
            float const t = std::clamp(
                ((point_x - start.x) * seg_dx + (point_y - start.y) * seg_dy) / len_sq, 0.0F, 1.0F);
            float const closest_x = start.x + t * seg_dx;
            float const closest_y = start.y + t * seg_dy;
            float const dx = point_x - closest_x;
            float const dy = point_y - closest_y;
            distance = std::sqrt((dx * dx) + (dy * dy));
        }

        if (distance <= border_tolerance_normalized)
        {
            return AnnotationHitTestResult{
                .kind = AnnotationHitKind::Border, .object_id = object.id, .handle_index = -1};
        }

        return AnnotationHitTestResult{.kind = AnnotationHitKind::None, .object_id = object.id, .handle_index = -1};
    }

    AnnotationHitTestResult AnnotationSession::HitTestRectangle(AnnotationObject const &object, NormalizedRectF point_rect,
                                                                float control_point_radius_normalized,
                                                                float border_tolerance_normalized)
    {
        float const point_x = (point_rect.left + point_rect.right) * 0.5F;
        float const point_y = (point_rect.top + point_rect.bottom) * 0.5F;

        // 控制点检测优先于边界检测
        float const corners[4][2] = {
            {object.bounds.left, object.bounds.top},
            {object.bounds.right, object.bounds.top},
            {object.bounds.right, object.bounds.bottom},
            {object.bounds.left, object.bounds.bottom},
        };

        for (int i = 0; i < 4; ++i)
        {
            float const dx = point_x - corners[i][0];
            float const dy = point_y - corners[i][1];
            float const distance = std::sqrt((dx * dx) + (dy * dy));
            if (distance <= control_point_radius_normalized)
            {
                return AnnotationHitTestResult{
                    .kind = AnnotationHitKind::ControlPoint, .object_id = object.id, .handle_index = i};
            }
        }

        bool const inside_bounds = point_x >= object.bounds.left && point_x <= object.bounds.right &&
                                   point_y >= object.bounds.top && point_y <= object.bounds.bottom;

        if (!inside_bounds)
        {
            return AnnotationHitTestResult{.kind = AnnotationHitKind::None, .object_id = object.id, .handle_index = -1};
        }

        // 检测是否在边界附近
        float const clamped_y = std::clamp(point_y, object.bounds.top, object.bounds.bottom);
        float const clamped_x = std::clamp(point_x, object.bounds.left, object.bounds.right);
        float const dist_to_left_edge = std::sqrt((point_x - object.bounds.left) * (point_x - object.bounds.left) +
                                                  (clamped_y - point_y) * (clamped_y - point_y));
        float const dist_to_right_edge = std::sqrt((point_x - object.bounds.right) * (point_x - object.bounds.right) +
                                                   (clamped_y - point_y) * (clamped_y - point_y));
        float const dist_to_top_edge = std::sqrt((clamped_x - point_x) * (clamped_x - point_x) +
                                                 (point_y - object.bounds.top) * (point_y - object.bounds.top));
        float const dist_to_bottom_edge = std::sqrt((clamped_x - point_x) * (clamped_x - point_x) +
                                                    (point_y - object.bounds.bottom) *
                                                        (point_y - object.bounds.bottom));

        float const min_edge_distance = std::min(
            {dist_to_left_edge, dist_to_right_edge, dist_to_top_edge, dist_to_bottom_edge});
        if (min_edge_distance <= border_tolerance_normalized)
        {
            return AnnotationHitTestResult{
                .kind = AnnotationHitKind::Border, .object_id = object.id, .handle_index = -1};
        }

        return AnnotationHitTestResult{.kind = AnnotationHitKind::Fill, .object_id = object.id, .handle_index = -1};
    }

    void AnnotationSession::PushUndoSnapshot() noexcept
    {
        undo_stack_.push_back(objects_);
    }
} // namespace capturezy::feature_capture
