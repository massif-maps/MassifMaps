/*
 * A style declares the TileDrawSize its zoom numbers are written for (Map `tile-draw-size`), and the
 * SDK shifts the zoom it reads by log2(app / declared). Massif is written for 512 and an app left on
 * the default 256 numbers the same ground a level higher, so without the shift every road width,
 * every zoom gate and every ramp fired a level early: roads ~1.7x too wide, water taps at z13.
 *
 * Pinned here: the shift itself, and that a view-zoom function reads it from the ViewState. The
 * decode-time half (TileReader adds it to the tile's style zoom) needs the symbolizers, see ../README.md.
 */

#include "TestCheck.h"

#include <mapnikvt/Expression.h>
#include <mapnikvt/ExpressionContext.h>
#include <mapnikvt/Map.h>
#include <mapnikvt/Properties.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace mvt = massif::mvt;
namespace vt = massif::vt;

namespace {
    bool near(float value, float expected) {
        return std::fabs(value - expected) < 1.0e-4f;
    }

    // linear([view::zoom], (14, 0), (15, 1))
    mvt::FloatFunctionProperty zoomRamp() {
        std::vector<mvt::Expression> values = { mvt::Value(14.0), mvt::Value(0.0), mvt::Value(15.0), mvt::Value(1.0) };
        mvt::FloatFunctionProperty prop(0.0f);
        prop.setExpression(std::make_shared<mvt::InterpolateExpression>(mvt::InterpolateExpression::Method::LINEAR,
            std::make_shared<mvt::VariableExpression>(std::string("view::zoom")), std::move(values)));
        return prop;
    }
}

void testZoomShift() {
    std::printf("  ZoomShift\n");

    mvt::Map::Settings undeclared;
    TEST_CHECK(near(undeclared.zoomShift(256.0f), 0.0f), "a style that declares nothing is read as it always was");

    mvt::Map::Settings massif;
    massif.tileDrawSize = 512.0f;
    TEST_CHECK(near(massif.zoomShift(512.0f), 0.0f), "an app drawing at the declared size reads the style unshifted");
    TEST_CHECK(near(massif.zoomShift(256.0f), -1.0f), "at the default 256 the same ground is a level lower in the style");
    TEST_CHECK(near(massif.zoomShift(1024.0f), 1.0f), "and a level higher at 1024");

    mvt::FloatFunctionProperty ramp = zoomRamp();
    vt::FloatFunction func = ramp.getFunction(mvt::ExpressionContext());
    vt::ViewState viewState;
    viewState.zoom = 15.0f;
    TEST_CHECK(near(func(viewState), 1.0f), "unshifted, z15 is the top of the ramp");
    viewState.styleZoomShift = massif.zoomShift(256.0f);
    TEST_CHECK(near(func(viewState), 0.0f), "shifted, the SDK's z15 at 256 reads the style's z14");
    viewState.zoom = 15.5f;
    TEST_CHECK(near(func(viewState), 0.5f), "and fractions carry through");
}
