#include <iostream>

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
                .kind = AnnotationKind::Rectangle,
                .bounds =
                    NormalizedRectF{
                        .left = 0.1F,
                        .top = 0.2F,
                        .right = 0.7F,
                        .bottom = 0.8F,
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
                    NormalizedRectF{
                        .left = 0.0F,
                        .top = 0.0F,
                        .right = 1.0F,
                        .bottom = 1.0F,
                    },
            });
            return Expect(!session.CanRedo(), "adding a new object should clear redo history");
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

    return 0;
}
