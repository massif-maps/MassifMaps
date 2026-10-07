/*
 * A label's side of its icon read from `render::3d`, in the form mapbox2css writes a `case` anchor:
 * the Massif summits put their name above the icon on terrain and below it on a flat map.
 */

#include "TestCheck.h"

#include <mapnikvt/ExpressionContext.h>
#include <mapnikvt/ParserUtils.h>
#include <mapnikvt/Properties.h>

#include <string>

namespace mvt = massif::mvt;

void testRenderModeAlignment() {
    mvt::VerticalAlignmentProperty alignment("auto");
    alignment.setExpression(mvt::parseExpression("(([render::3d] = true) ? 'bottom' : 'top')", false));
    mvt::FloatProperty dy(0.0f);
    dy.setExpression(mvt::parseExpression("((([render::3d] = true) ? -0.75 : 0.75) * 12)", false));

    mvt::ExpressionContext flat;
    mvt::ExpressionContext terrain;
    terrain.setRender3D(true);

    TEST_CHECK(alignment.getValue(flat) == -1.0f, "flat: the text hangs from its top edge, under the icon");
    TEST_CHECK(dy.getValue(flat) == 9.0f, "flat: moved down");
    TEST_CHECK(alignment.getValue(terrain) == 1.0f, "terrain: the text stands on its bottom edge, over the icon");
    TEST_CHECK(dy.getValue(terrain) == -9.0f, "terrain: moved up");
}
