/*
 * What a `line-gradient` value draws: the ramp baked from a stop list in a string, a JSON array or a
 * table parameter, or sampled from an expression of [mapnik::line_progress]. A texel is
 * premultiplied RGBA8, sampled without filtering, so these read the baked bytes.
 *
 * Not covered: what the renderer then does with a ramp (a device check).
 */

#include "TestCheck.h"

#include <mapnikvt/Expression.h>
#include <mapnikvt/ExpressionContext.h>
#include <mapnikvt/LineGradient.h>
#include <mapnikvt/Properties.h>

#include <array>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace mvt = massif::mvt;
namespace vt = massif::vt;

namespace {
    using Texel = std::array<int, 4>;

    Texel texelAt(const vt::ColorRamp& ramp, double position) {
        int i = static_cast<int>(position * vt::ColorRamp::WIDTH);
        return { ramp.texels[i * 4], ramp.texels[i * 4 + 1], ramp.texels[i * 4 + 2], ramp.texels[i * 4 + 3] };
    }

    bool near(const Texel& texel, const Texel& expected, int tolerance = 2) {
        for (int c = 0; c < 4; c++) {
            if (std::abs(texel[c] - expected[c]) > tolerance) {
                return false;
            }
        }
        return true;
    }

    std::shared_ptr<const vt::ColorRamp> fromText(const std::string& text) {
        return mvt::buildLineGradient(mvt::Value(text));
    }

    mvt::Value stop(double position, const std::string& color) {
        return mvt::Value(std::make_shared<const mvt::ValueArray>(std::vector<mvt::Value> { mvt::Value(position), mvt::Value(color) }));
    }
}

void testLineGradient() {
    auto smooth = fromText("0:#ff0000,1:#0000ff");
    TEST_CHECK(smooth && smooth->texels.size() == static_cast<std::size_t>(vt::ColorRamp::WIDTH) * 4, "a stop list bakes one texel per step of the ramp");
    TEST_CHECK(smooth && near(texelAt(*smooth, 0.0), { 255, 0, 0, 255 }) && near(texelAt(*smooth, 0.9999), { 0, 0, 255, 255 }), "the ends are the first and last stop");
    TEST_CHECK(smooth && near(texelAt(*smooth, 0.5), { 128, 0, 128, 255 }, 3), "between two stops it blends");

    auto held = fromText("step;0:#ff0000,0.5:#00ff00");
    TEST_CHECK(held && near(texelAt(*held, 0.3), { 255, 0, 0, 255 }) && near(texelAt(*held, 0.6), { 0, 255, 0, 255 }), "a step list holds each colour until the next stop");

    auto edge = fromText("0:#ff0000,0.5:#ff0000,0.5:#0000ff,1:#0000ff");
    TEST_CHECK(edge && near(texelAt(*edge, 0.45), { 255, 0, 0, 255 }) && near(texelAt(*edge, 0.55), { 0, 0, 255, 255 }), "two stops at one position are a hard edge");
    TEST_CHECK(edge && !near(texelAt(*edge, 0.4995), { 128, 0, 128, 255 }, 40) && !near(texelAt(*edge, 0.5005), { 128, 0, 128, 255 }, 40), "with nothing blended across it");

    auto before = fromText("0.25:#ff0000,0.75:#0000ff");
    TEST_CHECK(before && near(texelAt(*before, 0.0), { 255, 0, 0, 255 }) && near(texelAt(*before, 0.99), { 0, 0, 255, 255 }), "outside the stops the nearest one holds");

    auto unordered = fromText("1:#0000ff,0:#ff0000");
    TEST_CHECK(unordered && near(texelAt(*unordered, 0.0), { 255, 0, 0, 255 }), "stops may come in any order");

    auto faint = fromText("0:#ff000080,1:#ff000080");
    TEST_CHECK(faint && near(texelAt(*faint, 0.5), { 128, 0, 0, 128 }, 2), "a texel is premultiplied by its alpha");

    auto functionStop = fromText("0:rgba(255,0,0,1),1:rgb(0,0,255)");
    TEST_CHECK(functionStop && near(texelAt(*functionStop, 0.0), { 255, 0, 0, 255 }), "a colour with commas of its own is one stop");

    auto json = mvt::buildLineGradient(mvt::Value(std::string("[[0,\"#ff0000\"],[1,\"#0000ff\"]]")));
    TEST_CHECK(json == smooth, "the same stops as JSON are the same ramp");
    auto table = mvt::buildLineGradient(mvt::Value(std::make_shared<const mvt::ValueArray>(std::vector<mvt::Value> { stop(0, "#ff0000"), stop(1, "#0000ff") })));
    TEST_CHECK(table == smooth, "and as a table parameter");
    TEST_CHECK(fromText("0:#ff0000,1:#0000ff") == smooth, "equal ramps are one object, so a slot is shared");
    TEST_CHECK(fromText("0:#ff0000,1:#00ff00") != smooth, "different ramps are not");

    TEST_CHECK(!fromText("") && !fromText("0:#ff0000,abc") && !fromText("red") && !fromText("[1,2]") && !mvt::buildLineGradient(mvt::Value(3.0)) && !mvt::buildLineGradient(mvt::Value()), "anything that is not a stop list draws no gradient");

    // The tree CartoCSSMapnikTranslator makes for `linear([mapnik::line_progress], (0, #ff0000), (1, #0000ff))`.
    mvt::GradientProperty property;
    property.setExpression(std::make_shared<mvt::InterpolateExpression>(mvt::InterpolateExpression::Method::LINEAR, std::make_shared<mvt::VariableExpression>(std::string("mapnik::line_progress")),
        std::vector<mvt::Expression> { mvt::Value(0.0), mvt::Value(std::string("#ff0000")), mvt::Value(1.0), mvt::Value(std::string("#0000ff")) }));
    auto sampled = property.getValue(mvt::ExpressionContext());
    TEST_CHECK(sampled && near(texelAt(*sampled, 0.0), { 255, 0, 0, 255 }, 3) && near(texelAt(*sampled, 0.5), { 128, 0, 128, 255 }, 3) && near(texelAt(*sampled, 0.9999), { 0, 0, 255, 255 }, 3), "an expression of the line progress is sampled along the ramp");
    TEST_CHECK(sampled == property.getValue(mvt::ExpressionContext()), "and is built once, not per feature");

    mvt::GradientProperty unset;
    TEST_CHECK(!unset.getValue(mvt::ExpressionContext()), "a property no style sets has no ramp");
}
