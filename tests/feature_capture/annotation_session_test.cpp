#include <cmath>
#include <iostream>
#include <variant>

#include "feature_capture/capture_annotation.h"

namespace capturezy::feature_capture
{
    namespace
    {
        bool Expect(bool condition, char const *message)
        {
            if (condition)
            {
                return true;
            }

            std::cerr << message << '\n';
            return false;
        }

        bool TestToggleToolFamily()
        {
            AnnotationSession session;
            session.Reset();
            session.ToggleToolFamily(AnnotationToolFamily::Shape);
            if (!Expect(session.ActiveToolFamily() == AnnotationToolFamily::Shape, "shape tool should activate"))
            {
                return false;
            }

            session.ToggleToolFamily(AnnotationToolFamily::Shape);
            return Expect(session.ActiveToolFamily() == AnnotationToolFamily::None,
                          "same tool family should toggle back to none");
        }

        bool TestSetShapeVariant()
        {
            AnnotationSession session;
            session.Reset();
            session.SetShapeVariant(ShapeToolVariant::Rectangle);
            return Expect(session.ActiveShapeVariant() == ShapeToolVariant::Rectangle,
                          "rectangle variant should remain selected");
        }

        bool TestAddUndoRedo()
        {
            AnnotationSession session;
            session.Reset();
            session.AddObject(AnnotationObject{
                .id = 0,
                .kind = AnnotationKind::Rectangle,
                .bounds =
                    NormalizedRectF{
                        .left = 0.1F,
                        .top = 0.2F,
                        .right = 0.7F,
                        .bottom = 0.8F,
                    },
                .style = {},
            });
            if (!Expect(session.Objects().size() == 1U, "adding an object should append to the session"))
            {
                return false;
            }
            if (!Expect(session.CanUndo(), "adding an object should enable undo"))
            {
                return false;
            }
            if (!Expect(!session.CanRedo(), "redo should stay empty until an undo occurs"))
            {
                return false;
            }

            if (!Expect(session.Undo(), "undo should succeed when history exists"))
            {
                return false;
            }
            if (!Expect(session.Objects().empty(), "undo should restore the previous snapshot"))
            {
                return false;
            }
            if (!Expect(session.CanRedo(), "undo should enable redo"))
            {
                return false;
            }

            if (!Expect(session.Redo(), "redo should succeed after undo"))
            {
                return false;
            }
            if (!Expect(session.Objects().size() == 1U, "redo should restore the annotation"))
            {
                return false;
            }

            session.AddObject(AnnotationObject{
                .id = 0,
                .kind = AnnotationKind::Rectangle,
                .bounds =
                    NormalizedRectF{
                        .left = 0.0F,
                        .top = 0.0F,
                        .right = 1.0F,
                        .bottom = 1.0F,
                    },
                .style = {},
            });
            return Expect(!session.CanRedo(), "adding a new object should clear redo history");
        }

        bool TestObjectSelection()
        {
            AnnotationSession session;
            session.Reset();
            session.AddObject(AnnotationObject{
                .id = 0,
                .kind = AnnotationKind::Rectangle,
                .bounds = {.left = 0.1F, .top = 0.1F, .right = 0.5F, .bottom = 0.5F},
                .style = {},
            });
            session.AddObject(AnnotationObject{
                .id = 0,
                .kind = AnnotationKind::Rectangle,
                .bounds = {.left = 0.6F, .top = 0.6F, .right = 0.9F, .bottom = 0.9F},
                .style = {},
            });

            if (!Expect(session.HasSelectedObject(), "adding an object should auto-select it"))
            {
                return false;
            }

            AnnotationObjectId const first_id = session.Objects()[0].id;
            AnnotationObjectId const second_id = session.Objects()[1].id;

            if (!Expect(session.SelectedObjectId().value() == second_id, "last added should be selected"))
            {
                return false;
            }

            session.SelectObject(first_id);
            if (!Expect(session.SelectedObjectId().value() == first_id, "select should switch selection"))
            {
                return false;
            }

            session.DeselectObject();
            if (!Expect(!session.HasSelectedObject(), "deselect should clear selection"))
            {
                return false;
            }

            session.SelectObject(first_id);
            session.Undo();
            if (!Expect(!session.HasSelectedObject(), "undo should clear selection"))
            {
                return false;
            }

            return true;
        }

        bool TestMoveObject()
        {
            AnnotationSession session;
            session.Reset();
            session.AddObject(AnnotationObject{
                .id = 0,
                .kind = AnnotationKind::Rectangle,
                .bounds = {.left = 0.1F, .top = 0.1F, .right = 0.5F, .bottom = 0.5F},
                .style = {},
            });

            AnnotationObjectId const obj_id = session.Objects()[0].id;
            session.SelectObject(obj_id);

            NormalizedRectF const moved_bounds{.left = 0.2F, .top = 0.2F, .right = 0.6F, .bottom = 0.6F};
            if (!Expect(session.MoveSelectedObject(moved_bounds), "move should succeed with selection"))
            {
                return false;
            }

            AnnotationObject const *selected = session.SelectedObject();
            if (!Expect(selected != nullptr, "selected object should exist after move"))
            {
                return false;
            }
            if (!Expect(std::abs(selected->bounds.left - 0.2F) < 0.001F, "move should update bounds"))
            {
                return false;
            }

            if (!Expect(session.CanUndo(), "move should push undo state"))
            {
                return false;
            }

            session.Undo();
            selected = session.SelectedObject();
            if (!Expect(selected == nullptr, "undo clears selection"))
            {
                return false;
            }

            return true;
        }

        bool TestResizeObject()
        {
            AnnotationSession session;
            session.Reset();
            session.AddObject(AnnotationObject{
                .id = 0,
                .kind = AnnotationKind::Rectangle,
                .bounds = {.left = 0.1F, .top = 0.1F, .right = 0.5F, .bottom = 0.5F},
                .style = {},
            });

            AnnotationObjectId const obj_id = session.Objects()[0].id;
            session.SelectObject(obj_id);

            NormalizedRectF const resized_bounds{.left = 0.05F, .top = 0.05F, .right = 0.8F, .bottom = 0.8F};
            if (!Expect(session.ResizeSelectedObject(resized_bounds), "resize should succeed with selection"))
            {
                return false;
            }

            AnnotationObject const *selected = session.SelectedObject();
            if (!Expect(selected != nullptr, "selected object should exist after resize"))
            {
                return false;
            }
            if (!Expect(std::abs(selected->bounds.right - 0.8F) < 0.001F, "resize should update bounds"))
            {
                return false;
            }

            return true;
        }

        bool TestDeleteObject()
        {
            AnnotationSession session;
            session.Reset();
            session.AddObject(AnnotationObject{
                .id = 0,
                .kind = AnnotationKind::Rectangle,
                .bounds = {.left = 0.1F, .top = 0.1F, .right = 0.5F, .bottom = 0.5F},
                .style = {},
            });
            session.AddObject(AnnotationObject{
                .id = 0,
                .kind = AnnotationKind::Rectangle,
                .bounds = {.left = 0.6F, .top = 0.6F, .right = 0.9F, .bottom = 0.9F},
                .style = {},
            });

            if (!Expect(session.Objects().size() == 2U, "should have two objects"))
            {
                return false;
            }

            session.SelectObject(session.Objects()[0].id);
            if (!Expect(session.DeleteSelectedObject(), "delete should succeed"))
            {
                return false;
            }

            if (!Expect(session.Objects().size() == 1U, "should have one object after delete"))
            {
                return false;
            }

            if (!Expect(!session.HasSelectedObject(), "delete should clear selection"))
            {
                return false;
            }

            if (!Expect(session.CanUndo(), "delete should push undo state"))
            {
                return false;
            }

            session.Undo();
            if (!Expect(session.Objects().size() == 2U, "undo should restore deleted object"))
            {
                return false;
            }

            session.DeselectObject();
            if (!Expect(!session.DeleteSelectedObject(), "delete without selection should fail"))
            {
                return false;
            }

            return true;
        }

        bool TestHitTestObject()
        {
            AnnotationObject obj{
                .id = 1,
                .kind = AnnotationKind::Rectangle,
                .bounds = {.left = 0.2F, .top = 0.2F, .right = 0.8F, .bottom = 0.8F},
                .style = {},
            };

            float const control_radius = 0.03F;
            float const border_tolerance = 0.01F;

            NormalizedRectF fill_point{.left = 0.49F, .top = 0.49F, .right = 0.51F, .bottom = 0.51F};
            AnnotationHitTestResult fill_result = AnnotationSession::HitTestObject(obj, fill_point, control_radius,
                                                                                   border_tolerance);
            if (!Expect(fill_result.kind == AnnotationHitKind::Fill, "center point should hit fill"))
            {
                return false;
            }

            NormalizedRectF corner_point{.left = 0.19F, .top = 0.19F, .right = 0.21F, .bottom = 0.21F};
            AnnotationHitTestResult corner_result = AnnotationSession::HitTestObject(obj, corner_point, control_radius,
                                                                                     border_tolerance);
            if (!Expect(corner_result.kind == AnnotationHitKind::ControlPoint, "corner should hit control point"))
            {
                return false;
            }
            if (!Expect(corner_result.handle_index == 0, "top-left corner should be handle index 0"))
            {
                return false;
            }

            NormalizedRectF edge_point{.left = 0.49F, .top = 0.19F, .right = 0.51F, .bottom = 0.21F};
            AnnotationHitTestResult edge_result = AnnotationSession::HitTestObject(obj, edge_point, control_radius,
                                                                                   border_tolerance);
            if (!Expect(edge_result.kind == AnnotationHitKind::EdgeMidpoint, "edge center should hit edge midpoint"))
            {
                return false;
            }

            NormalizedRectF border_point{.left = 0.19F, .top = 0.49F, .right = 0.22F, .bottom = 0.51F};
            AnnotationHitTestResult border_result = AnnotationSession::HitTestObject(obj, border_point, control_radius,
                                                                                     border_tolerance);
            bool const border_hit_is_acceptable = border_result.kind == AnnotationHitKind::Border ||
                                                  border_result.kind == AnnotationHitKind::ControlPoint ||
                                                  border_result.kind == AnnotationHitKind::EdgeMidpoint;
            if (!Expect(border_hit_is_acceptable, "near border should hit border, control point, or edge midpoint"))
            {
                return false;
            }

            NormalizedRectF outside_point{.left = 0.05F, .top = 0.05F, .right = 0.07F, .bottom = 0.07F};
            AnnotationHitTestResult outside_result = AnnotationSession::HitTestObject(obj, outside_point,
                                                                                      control_radius, border_tolerance);
            if (!Expect(outside_result.kind == AnnotationHitKind::None, "outside point should miss"))
            {
                return false;
            }

            return true;
        }

        bool TestActiveStyle()
        {
            AnnotationSession session;
            session.Reset();

            AnnotationStyle default_style = session.ActiveStyle();
            if (!Expect(default_style.color == AnnotationColor::Yellow, "default color should be yellow"))
            {
                return false;
            }

            AnnotationStyle new_style{
                .color = AnnotationColor::Red,
                .line_width = AnnotationLineWidth::Thick,
                .fill_enabled = true,
            };
            session.SetActiveStyle(new_style);
            AnnotationStyle retrieved = session.ActiveStyle();
            if (!Expect(retrieved.color == AnnotationColor::Red, "active style color should be red"))
            {
                return false;
            }
            if (!Expect(retrieved.line_width == AnnotationLineWidth::Thick, "active style line width should be thick"))
            {
                return false;
            }
            if (!Expect(retrieved.fill_enabled, "active style fill should be enabled"))
            {
                return false;
            }

            session.AddObject(AnnotationObject{
                .id = 0,
                .kind = AnnotationKind::Rectangle,
                .bounds = {.left = 0.1F, .top = 0.1F, .right = 0.5F, .bottom = 0.5F},
                .style = new_style,
            });

            AnnotationObject const &added = session.Objects()[0];
            if (!Expect(added.style.color == AnnotationColor::Red, "added object should carry style"))
            {
                return false;
            }

            return true;
        }

        bool TestAddLineAnnotation()
        {
            AnnotationSession session;
            session.Reset();

            LineData line{
                .start = {0.1F, 0.2F},
                .end = {0.8F, 0.7F},
            };

            AnnotationObject obj{
                .kind = AnnotationKind::Line,
                .bounds = {.left = 0.1F, .top = 0.2F, .right = 0.8F, .bottom = 0.7F},
                .style = {},
                .type_data = line,
            };

            session.AddObject(obj);

            if (!Expect(session.Objects().size() == 1U, "adding a line should append to the session"))
            {
                return false;
            }
            if (!Expect(session.Objects()[0].kind == AnnotationKind::Line, "object kind should be Line"))
            {
                return false;
            }

            auto const *line_ptr = std::get_if<LineData>(&session.Objects()[0].type_data);
            if (!Expect(line_ptr != nullptr, "type_data should hold LineData"))
            {
                return false;
            }
            if (!Expect(std::abs(line_ptr->start.x - 0.1F) < 0.001F, "line start x should match"))
            {
                return false;
            }
            if (!Expect(std::abs(line_ptr->end.x - 0.8F) < 0.001F, "line end x should match"))
            {
                return false;
            }

            return true;
        }
    } // namespace
} // namespace capturezy::feature_capture

int main()
{
    using namespace capturezy::feature_capture;

    if (!TestToggleToolFamily())
    {
        return 1;
    }
    if (!TestSetShapeVariant())
    {
        return 1;
    }
    if (!TestAddUndoRedo())
    {
        return 1;
    }
    if (!TestObjectSelection())
    {
        return 1;
    }
    if (!TestMoveObject())
    {
        return 1;
    }
    if (!TestResizeObject())
    {
        return 1;
    }
    if (!TestDeleteObject())
    {
        return 1;
    }
    if (!TestHitTestObject())
    {
        return 1;
    }
    if (!TestActiveStyle())
    {
        return 1;
    }
    if (!TestAddLineAnnotation())
    {
        return 1;
    }

    return 0;
}
