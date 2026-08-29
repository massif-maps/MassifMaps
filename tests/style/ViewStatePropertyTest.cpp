/*
 * Map-block properties that follow the CAMERA, not the tile: building-height-scale, which mapbox
 * Standard ramps 0 at z15 to 1 at z15.3, and building-height-view-scale, the tilt ramp the converter
 * adds. The mechanism is shared by every Map float property.
 *
 * What is pinned here is that such a property stays an EXPRESSION over the ViewState:
 * CartoCSSMapLoader keeps it, VectorTileLayer::getStyleEnvironment evaluates it against the frame's
 * view state, and MapRenderer::collectStyleEnvironment calls that every frame. A property that
 * quietly collapsed to one number at load would still render - it would just never animate.
 */

#include "TestCheck.h"

#include <mapnikvt/Expression.h>
#include <mapnikvt/ExpressionContext.h>
#include <mapnikvt/Properties.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace mvt = massif::mvt;
namespace vt = massif::vt;

namespace {
    using Method = mvt::InterpolateExpression::Method;

    mvt::Expression variable(const char* name) {
        return std::make_shared<mvt::VariableExpression>(std::string(name));
    }

    // The tree CartoCSSMapnikTranslator produces for `linear([view::x], (k0, v0), (k1, v1))`.
    mvt::Expression ramp(mvt::Expression input, std::vector<double> keyframes) {
        std::vector<mvt::Expression> values;
        for (double value : keyframes) {
            values.emplace_back(mvt::Value(value));
        }
        return std::make_shared<mvt::InterpolateExpression>(Method::LINEAR, std::move(input), std::move(values));
    }

    // The SDK's zoom is mapbox's + 1, so a converted ramp reads ([view::zoom] - 1).
    mvt::Expression mapboxZoom() {
        return std::make_shared<mvt::BinaryExpression>(mvt::BinaryExpression::Op::SUB, variable("view::zoom"), mvt::Value(1.0));
    }

    mvt::FloatFunctionProperty property(mvt::Expression expr) {
        mvt::FloatFunctionProperty prop(1.0f);
        prop.setExpression(std::move(expr));
        return prop;
    }

    vt::ViewState view(float zoom, float tilt) {
        vt::ViewState viewState;
        viewState.zoom = zoom;
        viewState.tilt = tilt;
        return viewState;
    }

    // What VectorTileLayer::getStyleEnvironment does with one: build the function against the
    // (feature-less) context once, then evaluate it against this frame's view state.
    float evaluate(const mvt::FloatFunctionProperty& prop, const vt::ViewState& viewState) {
        return prop.getFunction(mvt::ExpressionContext())(viewState);
    }

    bool near(float value, float expected) {
        return std::fabs(value - expected) < 1.0e-4f;
    }
}

void testViewStateProperty() {
    std::printf("  ViewStateProperty\n");

    // 1. Mapbox Standard's own ramp, as the converter writes it. One property, a different number
    //    per zoom - and it must CLAMP past its last key, which is what mapbox's interpolate does
    //    and what cglib's fcurve does not do on its own.
    {
        mvt::FloatFunctionProperty heightScale = property(ramp(mapboxZoom(), { 15.0, 0.0, 15.3, 1.0 }));

        TEST_CHECK(near(evaluate(heightScale, view(16.0f, 0.0f)), 0.0f), "below the ramp the extrusion has no height");
        TEST_CHECK(near(evaluate(heightScale, view(16.15f, 0.0f)), 0.5f), "mid-ramp it is half grown");
        TEST_CHECK(near(evaluate(heightScale, view(16.3f, 0.0f)), 1.0f), "at the top of the ramp it is full height");
        TEST_CHECK(near(evaluate(heightScale, view(19.0f, 0.0f)), 1.0f), "and past it the ramp clamps, it does not extrapolate");
    }

    // 2. The camera ANGLE, which no tile can carry: 90 is straight down in this SDK, and the height
    //    is scaled away as the view flattens onto the map.
    {
        mvt::FloatFunctionProperty heightScale = property(ramp(variable("view::tilt"), { 45.0, 1.0, 90.0, 0.35 }));

        TEST_CHECK(near(evaluate(heightScale, view(17.0f, 45.0f)), 1.0f), "a tilted camera keeps the full height");
        TEST_CHECK(near(evaluate(heightScale, view(17.0f, 67.5f)), 0.675f), "half way to nadir it is half way down the ramp");
        TEST_CHECK(near(evaluate(heightScale, view(17.0f, 90.0f)), 0.35f), "looking straight down the extrusions are flattened");
        TEST_CHECK(near(evaluate(heightScale, view(17.0f, 20.0f)), 1.0f), "and below the ramp it clamps rather than growing them");
    }

    // 3. The converter emits the two SEPARATELY, and that split is load-bearing: the shadow caster
    //    follows building-height-scale (no building, no shadow) and ignores the view scale (a
    //    building flattened for a top-down camera is still there, so its shadow keeps its length).
    //    What the renderer multiplies for the DRAWN geometry is the product of the two.
    {
        mvt::FloatFunctionProperty heightScale = property(ramp(mapboxZoom(), { 15.0, 0.0, 15.3, 1.0 }));
        mvt::FloatFunctionProperty viewScale = property(ramp(variable("view::tilt"), { 45.0, 1.0, 90.0, 0.35 }));
        auto drawn = [&](float zoom, float tilt) {
            return evaluate(heightScale, view(zoom, tilt)) * evaluate(viewScale, view(zoom, tilt));
        };

        TEST_CHECK(near(drawn(16.0f, 45.0f), 0.0f), "under the zoom ramp nothing grows, whatever the tilt");
        TEST_CHECK(near(drawn(19.0f, 45.0f), 1.0f), "grown and tilted: full height");
        TEST_CHECK(near(drawn(19.0f, 90.0f), 0.35f), "grown and flat: the view scale alone");
        TEST_CHECK(near(drawn(16.15f, 90.0f), 0.175f), "half grown and flat: the two multiply");

        // The caster's own reading: it takes the first and never the second.
        TEST_CHECK(near(evaluate(heightScale, view(19.0f, 90.0f)), 1.0f), "a flattened building still casts its full shadow");
        TEST_CHECK(near(evaluate(heightScale, view(16.0f, 45.0f)), 0.0f), "a building that has not grown yet casts none");
    }

    // 4. The view state must live in the FUNCTION, not in the property: the renderer builds the
    //    function once and calls it per frame, so a property that needed rebuilding to answer a new
    //    tilt would never animate no matter how often getStyleEnvironment ran.
    {
        mvt::FloatFunctionProperty heightScale = property(ramp(variable("view::tilt"), { 45.0, 1.0, 90.0, 0.35 }));
        vt::FloatFunction func = heightScale.getFunction(mvt::ExpressionContext());
        TEST_CHECK(near(func(view(17.0f, 45.0f)), 1.0f) && near(func(view(17.0f, 90.0f)), 0.35f),
            "ONE function answers two camera angles");
        TEST_CHECK(heightScale.isDefined(), "and the property reports itself declared, so it overrides the app's own option");

        mvt::FloatFunctionProperty unset(1.0f);
        TEST_CHECK(!unset.isDefined(), "a property the style never mentions stays undefined - the app keeps its value");
    }
}
