/*
 * Tests for the fog and sky OPTION objects and the facade paths that reach them: the clamps, the
 * enabled switch, the vertical range, and writing the sky type by its constant name.
 *
 * NOT covered here: resolveFog / resolveSky. They live in StyleEnvironment.cpp, which pulls in
 * TerrainOptions and with it ElevationManager, the tile grid and the bitmap decoders - far past
 * what tests/README.md allows the host link to carry. Their merge order and the sun-lighting of
 * the fog colour need a device check.
 *
 * Nothing here renders: the shader block, the horizon term and the ray reconstruction are checked
 * on a device, not in this suite.
 */

#include "api/Context.h"
#include "components/FogOptions.h"
#include "components/SkyOptions.h"
#include "graphics/Color.h"

#include <memory>
#include <string>

using namespace massif;
using namespace massif::api;

#include "TestCheck.h"

namespace {

    bool nearly(float value, float expected) {
        float delta = value - expected;
        return (delta < 0 ? -delta : delta) < 1.0e-5f;
    }

}

void testFogOptions() {
    auto fog = std::make_shared<FogOptions>();

    // Attaching a FogOptions must change nothing until a colour is set: the default is transparent
    // and a transparent fog is no fog, however positive the range is.
    TEST_CHECK(fog->getColor().getA() == 0, "the default fog colour is transparent");
    TEST_CHECK(fog->isEnabled(), "the fog is enabled by default");

    // Both ends of the vertical range are altitudes, so a negative one is meaningless.
    fog->setVerticalRangeStart(-500.0f);
    TEST_CHECK(nearly(fog->getVerticalRangeStart(), 0.0f), "a negative vertical range start clamps to 0");
    fog->setVerticalRangeEnd(-1.0f);
    TEST_CHECK(nearly(fog->getVerticalRangeEnd(), 0.0f), "a negative vertical range end clamps to 0");
    fog->setVerticalRangeStart(1800.0f);
    fog->setVerticalRangeEnd(3200.0f);
    TEST_CHECK(nearly(fog->getVerticalRangeStart(), 1800.0f) && nearly(fog->getVerticalRangeEnd(), 3200.0f),
               "the vertical range keeps the altitudes it was given");

    // Mapbox horizon-blend is a fraction, and the shader divides by it.
    fog->setHorizonBlend(4.0f);
    TEST_CHECK(nearly(fog->getHorizonBlend(), 1.0f), "horizon blend clamps to 1");
    fog->setHorizonBlend(-1.0f);
    TEST_CHECK(nearly(fog->getHorizonBlend(), 0.0f), "horizon blend clamps to 0");

    // Enabled is a real switch, not a value driven through zero: every other value survives it.
    fog->setColor(Color(180, 198, 216, 255));
    fog->setEnabled(false);
    fog->setEnabled(true);
    TEST_CHECK(fog->getColor().getARGB() == Color(180, 198, 216, 255).getARGB(),
               "toggling Enabled leaves the colour alone");
}

void testSkyOptions() {
    auto sky = std::make_shared<SkyOptions>();

    // The default is the scattering sky - what makes a map look right without being configured.
    TEST_CHECK(sky->getType() == SkyType::SKY_TYPE_ATMOSPHERE, "the default sky type is ATMOSPHERE");
    TEST_CHECK(sky->getQuality() == SkyQuality::SKY_QUALITY_MEDIUM, "the default sky quality is MEDIUM");

    // The tints multiply the scattering coefficients, so an opaque white is "no tint" rather than
    // "white sky" - the value an app that never touches them must get.
    TEST_CHECK(sky->getAtmosphereColor().getARGB() == Color(255, 255, 255, 255).getARGB(),
               "the atmosphere tint defaults to opaque white, i.e. no tint");
    TEST_CHECK(sky->getHaloColor().getARGB() == Color(255, 255, 255, 255).getARGB(),
               "the halo tint defaults to opaque white");

    // The shader divides by the luminance.
    sky->setAtmosphereLuminance(0.0f);
    TEST_CHECK(sky->getAtmosphereLuminance() > 0.0f, "the atmosphere luminance never reaches 0");
    sky->setAtmosphereSunIntensity(-5.0f);
    TEST_CHECK(nearly(sky->getAtmosphereSunIntensity(), 0.0f), "a negative sun intensity clamps to 0");
}

/**
 * The facade paths, which are generated from the %attribute macros: a property added to a .i must
 * appear here on the next build without anything being listed twice.
 */
void testFogSkyPaths() {
    auto context = std::make_shared<Context>();
    auto fog = std::make_shared<FogOptions>();
    Handle handle = NULL_HANDLE;
    TEST_CHECK(context->registerObject("options", "fog", fog, "massif::FogOptions", handle) == RESULT_OK,
               "the fog options register");
    auto sky = std::make_shared<SkyOptions>();
    Handle skyHandle = NULL_HANDLE;
    TEST_CHECK(context->registerObject("options", "sky", sky, "massif::SkyOptions", skyHandle) == RESULT_OK,
               "the sky options register");

    TEST_CHECK(context->setProperty(handle, "verticalRangeStart", PropertyValue::ofDouble(1200)) == RESULT_OK &&
               context->setProperty(handle, "verticalRangeEnd", PropertyValue::ofDouble(2600)) == RESULT_OK,
               "the vertical range is reachable as a facade property");
    TEST_CHECK(nearly(fog->getVerticalRangeStart(), 1200.0f) && nearly(fog->getVerticalRangeEnd(), 2600.0f),
               "a facade write reaches the option object");

    // The removed HorizonAngle must be an ERROR, not a silent no-op: an app that kept the old call
    // has to hear about it rather than wonder why nothing changed.
    TEST_CHECK(context->setProperty(handle, "horizonAngle", PropertyValue::ofDouble(5)) != RESULT_OK,
               "the removed horizonAngle property is rejected");

    // The sky type is an ENUM, and a spec, a URL query and a scripting binding all send its
    // constant NAME - strtoll would turn an unknown name into 0, which is a real sky type.
    TEST_CHECK(context->setProperty(skyHandle, "type", PropertyValue::ofString("SKY_TYPE_GRADIENT")) == RESULT_OK &&
               sky->getType() == SkyType::SKY_TYPE_GRADIENT,
               "the sky type is settable by its constant name");
    context->setProperty(skyHandle, "type", PropertyValue::ofString("SKY_TYPE_MARZIPAN"));
    TEST_CHECK(sky->getType() == SkyType::SKY_TYPE_GRADIENT,
               "an unknown constant name leaves the sky type alone rather than reading as 0");
}
