// The lowest sun altitude the shadow pass draws at (components/LightOptions.h): its default and its clamp,
// since a floor of 0 would stretch the light box to the horizon and above 30 hides the evening shadows.

#include "components/LightOptions.h"

#include "TestCheck.h"

using namespace massif;

void testShadowMinSunAltitude() {
    LightOptions options;
    TEST_CHECK(options.getShadowMinSunAltitude() == 15.0f, "the shadow floor defaults to 15 degrees, as it always was");
    options.setShadowMinSunAltitude(8.0f);
    TEST_CHECK(options.getShadowMinSunAltitude() == 8.0f, "a floor in range is kept");
    options.setShadowMinSunAltitude(0.0f);
    TEST_CHECK(options.getShadowMinSunAltitude() == 2.0f, "under 2 degrees it is raised to 2");
    options.setShadowMinSunAltitude(-10.0f);
    TEST_CHECK(options.getShadowMinSunAltitude() == 2.0f, "also for a negative one");
    options.setShadowMinSunAltitude(60.0f);
    TEST_CHECK(options.getShadowMinSunAltitude() == 30.0f, "over 30 degrees it is cut to 30");
}
