/*
 * InterpolateExpression, on the two things mapbox2css needs from it: a `step` whose values are not
 * interpolatable at all, and `exponential`'s input remap. Both were found from a converted Mapbox
 * Standard - a step over 'miter'/'round' was going through parseColor and taking the whole
 * line-join property down with it, and a linear reading of an exponential ramp drew every road
 * far too wide in the middle of its span.
 */

#include "TestCheck.h"

#include <mapnikvt/Expression.h>
#include <mapnikvt/ExpressionContext.h>
#include <mapnikvt/ValueConverter.h>

#include <cmath>
#include <string>
#include <vector>

namespace mvt = massif::mvt;
namespace vt = massif::vt;

namespace {
    using Method = mvt::InterpolateExpression::Method;

    mvt::Expression value(double v) { return mvt::Value(v); }
    mvt::Expression value(const char* v) { return mvt::Value(std::string(v)); }

    std::string asString(const mvt::Value& val) {
        return mvt::ValueConverter<std::string>::convert(val);
    }
}

void testInterpolateExpression() {
    std::printf("  InterpolateExpression\n");

    const mvt::ExpressionContext context;

    // 1. A step over values that are not numbers or colours. Mapbox Standard writes both line-join
    //    and line-cap this way; every one of them used to be dropped with "Color parsing failed".
    {
        mvt::InterpolateExpression join(Method::STEP, value(0.0),
            { value(0.0), value("miter"), value(14.0), value("round") });

        TEST_CHECK(asString(join.evaluate(0.0f, context)) == "miter", "a step below its second key takes the base");
        TEST_CHECK(asString(join.evaluate(13.9f, context)) == "miter", "and keeps it right up to that key");
        TEST_CHECK(asString(join.evaluate(14.0f, context)) == "round", "the key itself switches");
        TEST_CHECK(asString(join.evaluate(22.0f, context)) == "round", "and the last value holds above the last key");
    }

    // 2. A step over COLOUR strings must still be a colour curve - the fallback above must not
    //    swallow the case it was carved out of.
    {
        mvt::InterpolateExpression fill(Method::STEP, value(0.0),
            { value(0.0), value("#ff0000"), value(10.0), value("#0000ff") });

        auto colorAt = [&](float t) {
            return static_cast<unsigned int>(mvt::ValueConverter<long long>::convert(fill.evaluate(t, context)));
        };
        TEST_CHECK(colorAt(0.0f) == 0xffff0000u, "a step over colours still yields a colour");
        TEST_CHECK(colorAt(10.0f) == 0xff0000ffu, "and steps to the next one at its key");
    }

    // 3. exponential: mapbox's t = (b^(x-x0) - 1) / (b^(x1-x0) - 1) over the span, fed to a LINEAR
    //    curve. Read linearly instead, the road width below comes out at 7.0 rather than 4.96.
    {
        mvt::InterpolateExpression width(Method::EXPONENTIAL, value(0.0),
            { value(12.0), value(0.5), value(18.0), value(20.0) }, 1.5f);

        const float s = (std::pow(1.5f, 3.0f) - 1.0f) / (std::pow(1.5f, 6.0f) - 1.0f);
        const float expected = 0.5f + s * (20.0f - 0.5f);
        const float got = mvt::ValueConverter<float>::convert(width.evaluate(15.0f, context));

        TEST_CHECK(std::fabs(got - expected) < 1.0e-3f, "exponential remaps the input, not the curve");
        TEST_CHECK(std::fabs(mvt::ValueConverter<float>::convert(width.evaluate(12.0f, context)) - 0.5f) < 1.0e-3f,
            "and is exact on the key frames");
        TEST_CHECK(std::fabs(mvt::ValueConverter<float>::convert(width.evaluate(18.0f, context)) - 20.0f) < 1.0e-3f,
            "at both ends of the span");
    }

    // 4. base 1 is the linear case, which is what every non-exponential method carries.
    {
        mvt::InterpolateExpression linear(Method::EXPONENTIAL, value(0.0),
            { value(0.0), value(0.0), value(10.0), value(100.0) }, 1.0f);
        TEST_CHECK(std::fabs(mvt::ValueConverter<float>::convert(linear.evaluate(5.0f, context)) - 50.0f) < 1.0e-3f,
            "base 1 leaves the input alone");
    }
}
