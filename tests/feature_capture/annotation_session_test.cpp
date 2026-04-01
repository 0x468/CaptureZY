#include <cmath>
#include <iostream>

#include "feature_capture/capture_annotation.h"
#include "feature_capture/capture_annotation_geometry.h"

namespace capturezy::feature_capture
{
    namespace
    {
        constexpr float kFloatTolerance = 0.0001F;

        bool Expect(bool condition, char const *message)
        {
            if (condition)
            {
                return true;
            }

            std::cerr << message << '\n';
            return false;
        }

        bool AreClose(float left, float right)
        {
            return std::fabs(left - right) <= kFloatTolerance;
        }

        bool RectEquals(RECT const &left, RECT const &right)
        {
            return left.left == right.left && left.top == right.top && left.right == right.right &&
                   left.bottom == right.bottom;
        }

        bool StyleEquals(AnnotationStyle const &left, AnnotationStyle const &right)
        {
            return left.stroke_color == right.stroke_color && AreClose(left.stroke_width, right.stroke_width) &&
                   left.fill_color == right.fill_color && left.fill_alpha == right.fill_alpha &&
                   left.has_fill == right.has_fill;
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
                .kind = AnnotationKind::Rectangle,
                .bounds =
                    RECT{
                        .left = 100,
                        .top = 200,
                        .right = 700,
                        .bottom = 800,
                    },
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
                .kind = AnnotationKind::Rectangle,
                .bounds =
                    RECT{
                        .left = 0,
                        .top = 0,
                        .right = 1920,
                        .bottom = 1080,
                    },
            });
            return Expect(!session.CanRedo(), "adding a new object should clear redo history");
        }

        bool TestAnnotationStyleDefaults()
        {
            AnnotationStyle const style{};
            if (!Expect(style.stroke_color == RGB(255, 214, 102), "default stroke color should match overlay frame"))
            {
                return false;
            }
            if (!Expect(AreClose(style.stroke_width, 2.0F), "default stroke width should match rectangle frame width"))
            {
                return false;
            }
            if (!Expect(style.fill_color == RGB(255, 214, 102), "default fill color should match overlay fill color"))
            {
                return false;
            }
            if (!Expect(style.fill_alpha == 28, "default fill alpha should match overlay fill alpha"))
            {
                return false;
            }
            return Expect(!style.has_fill, "default rectangle annotations should be hollow");
        }

        bool TestAddObjectKeepsStyleData()
        {
            AnnotationSession session;
            session.Reset();

            AnnotationStyle const expected_style{
                .stroke_color = RGB(10, 20, 30),
                .stroke_width = 3.5F,
                .fill_color = RGB(40, 50, 60),
                .fill_alpha = static_cast<BYTE>(96),
                .has_fill = false,
            };
            AnnotationObject const object{
                .kind = AnnotationKind::Rectangle,
                .bounds =
                    RECT{
                        .left = 240,
                        .top = 150,
                        .right = 960,
                        .bottom = 760,
                    },
                .style = expected_style,
            };

            session.AddObject(object);
            if (!Expect(session.Objects().size() == 1U, "adding style object should append once"))
            {
                return false;
            }
            if (!Expect(StyleEquals(session.Objects()[0].style, expected_style),
                        "style should survive object insertion"))
            {
                return false;
            }

            if (!Expect(session.Undo(), "undo should still succeed for styled add"))
            {
                return false;
            }
            if (!Expect(session.Redo(), "redo should restore styled object"))
            {
                return false;
            }
            return Expect(StyleEquals(session.Objects()[0].style, expected_style),
                          "style should survive undo/redo snapshots");
        }

        bool TestReplaceObjectPreservesHistoryAndScope()
        {
            AnnotationSession session;
            session.Reset();

            AnnotationObject const first{
                .kind = AnnotationKind::Rectangle,
                .bounds =
                    RECT{
                        .left = 0,
                        .top = 0,
                        .right = 300,
                        .bottom = 300,
                    },
                .style =
                    AnnotationStyle{
                        .stroke_color = RGB(70, 80, 90),
                        .stroke_width = 1.0F,
                        .fill_color = RGB(70, 80, 90),
                        .fill_alpha = static_cast<BYTE>(10),
                        .has_fill = false,
                    },
            };
            AnnotationObject const second{
                .kind = AnnotationKind::Rectangle,
                .bounds =
                    RECT{
                        .left = 400,
                        .top = 400,
                        .right = 800,
                        .bottom = 800,
                    },
                .style =
                    AnnotationStyle{
                        .stroke_color = RGB(100, 110, 120),
                        .stroke_width = 2.0F,
                        .fill_color = RGB(100, 110, 120),
                        .fill_alpha = static_cast<BYTE>(20),
                        .has_fill = false,
                    },
            };
            AnnotationObject const replacement{
                .kind = AnnotationKind::Rectangle,
                .bounds =
                    RECT{
                        .left = 450,
                        .top = 450,
                        .right = 900,
                        .bottom = 950,
                    },
                .style =
                    AnnotationStyle{
                        .stroke_color = RGB(200, 40, 20),
                        .stroke_width = 5.0F,
                        .fill_color = RGB(20, 40, 200),
                        .fill_alpha = static_cast<BYTE>(88),
                        .has_fill = true,
                    },
            };

            session.AddObject(first);
            session.AddObject(second);
            if (!Expect(session.ReplaceObject(1U, replacement), "replace should succeed for valid index"))
            {
                return false;
            }
            if (!Expect(session.Objects().size() == 2U, "replace should keep object count"))
            {
                return false;
            }
            if (!Expect(RectEquals(session.Objects()[0].bounds, first.bounds),
                        "replace should not mutate other objects"))
            {
                return false;
            }
            if (!Expect(StyleEquals(session.Objects()[0].style, first.style),
                        "replace should keep non-target styles intact"))
            {
                return false;
            }
            if (!Expect(RectEquals(session.Objects()[1].bounds, replacement.bounds),
                        "replace should update target bounds"))
            {
                return false;
            }
            if (!Expect(StyleEquals(session.Objects()[1].style, replacement.style),
                        "replace should update target style"))
            {
                return false;
            }

            if (!Expect(session.Undo(), "replace should participate in undo history"))
            {
                return false;
            }
            if (!Expect(RectEquals(session.Objects()[1].bounds, second.bounds),
                        "undo should restore previous object bounds"))
            {
                return false;
            }
            if (!Expect(StyleEquals(session.Objects()[1].style, second.style),
                        "undo should restore previous object style"))
            {
                return false;
            }

            if (!Expect(session.Redo(), "replace should participate in redo history"))
            {
                return false;
            }
            return Expect(StyleEquals(session.Objects()[1].style, replacement.style),
                          "redo should restore replacement style");
        }

        bool TestReplaceObjectClearsRedoAfterUndo()
        {
            AnnotationSession session;
            session.Reset();

            AnnotationObject const original{
                .kind = AnnotationKind::Rectangle,
                .bounds =
                    RECT{
                        .left = 100,
                        .top = 100,
                        .right = 300,
                        .bottom = 300,
                    },
                .style = AnnotationStyle{},
            };
            AnnotationObject const first_replacement{
                .kind = AnnotationKind::Rectangle,
                .bounds =
                    RECT{
                        .left = 400,
                        .top = 400,
                        .right = 600,
                        .bottom = 600,
                    },
                .style =
                    AnnotationStyle{
                        .stroke_color = RGB(10, 120, 210),
                        .stroke_width = 4.0F,
                        .fill_color = RGB(210, 120, 10),
                        .fill_alpha = static_cast<BYTE>(70),
                        .has_fill = true,
                    },
            };
            AnnotationObject const second_replacement{
                .kind = AnnotationKind::Rectangle,
                .bounds =
                    RECT{
                        .left = 500,
                        .top = 500,
                        .right = 900,
                        .bottom = 900,
                    },
                .style =
                    AnnotationStyle{
                        .stroke_color = RGB(220, 30, 30),
                        .stroke_width = 3.0F,
                        .fill_color = RGB(30, 220, 30),
                        .fill_alpha = static_cast<BYTE>(40),
                        .has_fill = false,
                    },
            };

            session.AddObject(original);
            if (!Expect(session.ReplaceObject(0U, first_replacement), "first replace should succeed"))
            {
                return false;
            }
            if (!Expect(session.Undo(), "undo should succeed after first replace"))
            {
                return false;
            }
            if (!Expect(session.CanRedo(), "undo should make redo available"))
            {
                return false;
            }

            if (!Expect(session.ReplaceObject(0U, second_replacement), "second replace should succeed"))
            {
                return false;
            }
            return Expect(!session.CanRedo(), "successful replace should clear redo history");
        }

        bool TestTranslateAnnotationBoundsWithinCanvas()
        {
            RECT const canvas{.left = 0, .top = 0, .right = 1920, .bottom = 1080};
            RECT const original{.left = 100, .top = 120, .right = 300, .bottom = 260};

            AnnotationTranslationResult const result = TranslateAnnotationBoundsWithinRect(original, canvas, 50, -20);
            if (!Expect(result.moved, "translation should report movement when delta fits inside canvas"))
            {
                return false;
            }

            return Expect(RectEquals(result.bounds, RECT{.left = 150, .top = 100, .right = 350, .bottom = 240}),
                          "translation should stay in canvas pixel space");
        }

        bool TestTranslateAnnotationBoundsClampsAtCanvasEdge()
        {
            RECT const canvas{.left = 0, .top = 0, .right = 1920, .bottom = 1080};
            RECT const original{.left = 1800, .top = 1020, .right = 1910, .bottom = 1070};

            AnnotationTranslationResult const result = TranslateAnnotationBoundsWithinRect(original, canvas, 30, 30);
            if (!Expect(result.moved, "translation should still report movement when delta is clamped"))
            {
                return false;
            }

            return Expect(RectEquals(result.bounds, RECT{.left = 1810, .top = 1030, .right = 1920, .bottom = 1080}),
                          "translation should clamp moved bounds to canvas edge");
        }

        bool TestTranslateAnnotationBoundsReportsNoMovementWhenFullyClamped()
        {
            RECT const canvas{.left = 0, .top = 0, .right = 1920, .bottom = 1080};
            RECT const original{.left = 1810, .top = 1030, .right = 1920, .bottom = 1080};

            AnnotationTranslationResult const result = TranslateAnnotationBoundsWithinRect(original, canvas, 30, 40);
            if (!Expect(!result.moved, "translation should report no movement when delta is fully clamped"))
            {
                return false;
            }

            return Expect(RectEquals(result.bounds, original),
                          "fully clamped translation should keep bounds unchanged");
        }
        bool TestReplaceObjectRejectsInvalidIndex()
        {
            AnnotationSession session;
            session.Reset();

            AnnotationObject const replacement{
                .kind = AnnotationKind::Rectangle,
                .bounds =
                    RECT{
                        .left = 0,
                        .top = 0,
                        .right = 1920,
                        .bottom = 1080,
                    },
                .style = AnnotationStyle{},
            };
            if (!Expect(!session.ReplaceObject(0U, replacement), "replace should fail when index is invalid"))
            {
                return false;
            }
            if (!Expect(session.Objects().empty(), "invalid replace should keep objects unchanged"))
            {
                return false;
            }
            return Expect(!session.CanUndo(), "invalid replace should not create history");
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
    if (!TestAnnotationStyleDefaults())
    {
        return 1;
    }
    if (!TestAddObjectKeepsStyleData())
    {
        return 1;
    }
    if (!TestReplaceObjectPreservesHistoryAndScope())
    {
        return 1;
    }
    if (!TestReplaceObjectClearsRedoAfterUndo())
    {
        return 1;
    }
    if (!TestTranslateAnnotationBoundsWithinCanvas())
    {
        return 1;
    }
    if (!TestTranslateAnnotationBoundsClampsAtCanvasEdge())
    {
        return 1;
    }
    if (!TestTranslateAnnotationBoundsReportsNoMovementWhenFullyClamped())
    {
        return 1;
    }
    if (!TestReplaceObjectRejectsInvalidIndex())
    {
        return 1;
    }

    return 0;
}
