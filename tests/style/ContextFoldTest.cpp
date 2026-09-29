/*
 * foldContextExpressions (mapnikvt/ExpressionUtils.h): a style function that reads both the view and
 * a feature field is built once per feature and evaluated every frame. Folding the feature half at
 * build time must not change a single value - only what the per-frame evaluation redoes. Pinned: the
 * same numbers as the unfolded tree, the stops of a ramp becoming constants (so its curve is built
 * once), and the view variables and live style parameters staying variables.
 */

#include "TestCheck.h"

#include <mapnikvt/ExpressionContext.h>
#include <mapnikvt/ExpressionUtils.h>
#include <mapnikvt/Feature.h>
#include <mapnikvt/Predicate.h>
#include <mapnikvt/Properties.h>

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mvt = massif::mvt;
namespace vt = massif::vt;

namespace {
    mvt::Expression variable(const char* name) {
        return std::make_shared<mvt::VariableExpression>(std::string(name));
    }

    mvt::ExpressionContext featureContext(const char* cls) {
        mvt::ExpressionContext context;
        context.setFeatureData(std::make_shared<mvt::FeatureData>(
            1, mvt::FeatureData::GeometryType::LINE_GEOMETRY, std::vector<std::pair<std::string, mvt::Value>> { { "class", mvt::Value(std::string(cls)) } }));
        return context;
    }

    // linear([view::brightness], (0.3, ([class] = 'pedestrian') ? 9 : 6.5), (0.4, 12)): Standard's shape.
    mvt::Expression brightnessRamp() {
        mvt::Predicate isPedestrian = std::make_shared<mvt::ComparisonPredicate>(mvt::ComparisonPredicate::Op::EQ, variable("class"), mvt::Value(std::string("pedestrian")));
        mvt::Expression size = std::make_shared<mvt::TertiaryExpression>(mvt::TertiaryExpression::Op::CONDITIONAL, isPedestrian, mvt::Value(9.0), mvt::Value(6.5));
        std::vector<mvt::Expression> keyFrames { mvt::Value(0.3), size, mvt::Value(0.4), mvt::Value(12.0) };
        return std::make_shared<mvt::InterpolateExpression>(mvt::InterpolateExpression::Method::LINEAR, variable("view::brightness"), std::move(keyFrames));
    }

    vt::ViewState lit(float brightness) {
        vt::ViewState viewState;
        viewState.lightBrightness = brightness;
        return viewState;
    }

    bool near(double value, double expected) {
        return std::fabs(value - expected) < 1.0e-4;
    }
}

void testContextFold() {
    std::printf("  ContextFold\n");

    mvt::ExpressionContext pedestrian = featureContext("pedestrian");
    mvt::ExpressionContext street = featureContext("street");
    mvt::Expression ramp = brightnessRamp();
    mvt::Expression folded = mvt::foldContextExpressions(ramp, pedestrian);

    auto interp = std::get_if<std::shared_ptr<mvt::InterpolateExpression>>(&folded);
    bool constantStops = interp != nullptr;
    if (interp) {
        for (const mvt::Expression& keyFrame : (*interp)->getKeyFrames()) {
            constantStops = constantStops && std::holds_alternative<mvt::Value>(keyFrame);
        }
    }
    TEST_CHECK(constantStops, "a stop that reads a feature field becomes that feature's constant");
    TEST_CHECK(interp && std::holds_alternative<std::shared_ptr<mvt::VariableExpression>>((*interp)->getTimeExpression()), "the view variable driving the ramp stays a variable");

    bool same = true;
    for (float b : { 0.2f, 0.3f, 0.35f, 0.4f, 0.5f }) {
        vt::ViewState viewState = lit(b);
        double unfolded = mvt::ValueConverter<double>::convert(std::visit(mvt::ExpressionEvaluator(pedestrian, &viewState), ramp));
        double evaluated = mvt::ValueConverter<double>::convert(std::visit(mvt::ExpressionEvaluator(pedestrian, &viewState), folded));
        same = same && near(unfolded, evaluated);
    }
    TEST_CHECK(same, "the folded ramp gives the unfolded one's value at every brightness");

    mvt::FloatFunctionProperty prop(0.0f);
    prop.setExpression(ramp);
    TEST_CHECK(near(prop.getFunction(pedestrian)(lit(0.35f)), 10.5) && near(prop.getFunction(street)(lit(0.35f)), 9.25), "each feature's function keeps its own stops");

    mvt::ExpressionContext pedestrianAgain = featureContext("pedestrian");
    TEST_CHECK(prop.getFunction(pedestrian).function() == prop.getFunction(pedestrianAgain).function() && prop.getFunction(pedestrian).function() != prop.getFunction(street).function(),
               "features that fold alike share one function, so the renderer evaluates it once a frame");

    mvt::Expression param = std::make_shared<mvt::BinaryExpression>(mvt::BinaryExpression::Op::MUL, variable("param::width"), variable("class_width"));
    mvt::Expression paramFolded = mvt::foldContextExpressions(param, pedestrian);
    TEST_CHECK(std::holds_alternative<std::shared_ptr<mvt::BinaryExpression>>(paramFolded), "a style parameter stays live: it may change after the decode");
}
