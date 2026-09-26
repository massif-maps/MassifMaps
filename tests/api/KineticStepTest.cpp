// How much of its remaining delta a kinetic gesture spends in one frame
// (renderers/components/KineticStep.h): the distance must not depend on the frame rate.

#include "renderers/components/KineticStep.h"

#include "TestCheck.h"

#include <cmath>

using namespace massif;

namespace {
    double flingDistance(float slowdown, float deltaSeconds, double delta, double stopTolerance) {
        double travelled = 0;
        for (int frame = 0; frame < 100000 && delta >= stopTolerance; frame++) {
            double step = delta * kineticStepFraction(slowdown, deltaSeconds);
            travelled += step;
            delta -= step;
        }
        return travelled;
    }

    double flingSeconds(float slowdown, float deltaSeconds, double delta, double stopTolerance) {
        int frame = 0;
        for (; frame < 100000 && delta >= stopTolerance; frame++) {
            delta -= delta * kineticStepFraction(slowdown, deltaSeconds);
        }
        return frame * static_cast<double>(deltaSeconds);
    }
}

void testKineticStep() {
    const float slowdown = 0.99f; // KINETIC_PAN_SLOWDOWN
    const double clamp = 12.535;  // KINETIC_PAN_DELTA_CLAMP
    const double stop = 0.0878;   // KINETIC_PAN_STOP_TOLERANCE

    TEST_CHECK(kineticStepFraction(slowdown, 0.0f) == 0.0f, "a zero-length frame consumes no delta");
    TEST_CHECK(std::abs(kineticStepFraction(slowdown, 10.0f) - 1.0f) < 1.0e-6, "a very long frame consumes all of it");

    TEST_CHECK(kineticStepFraction(slowdown, 0.1f) > kineticStepFraction(slowdown, 1.0f / 60.0f),
               "a 10 fps frame consumes more of the delta than a 60 fps frame");

    double at60 = flingDistance(slowdown, 1.0f / 60.0f, clamp, stop);
    double at30 = flingDistance(slowdown, 1.0f / 30.0f, clamp, stop);
    double at10 = flingDistance(slowdown, 1.0f / 10.0f, clamp, stop);
    TEST_CHECK(std::abs(at30 - at60) < 0.1, "a fling covers the same ground at 30 fps as at 60");
    TEST_CHECK(std::abs(at10 - at60) < 0.1, "and the same at 10 fps");

    TEST_CHECK(at60 > clamp - 2.0 * stop && at60 < clamp, "a fling spends the delta it was handed");

    double seconds60 = flingSeconds(slowdown, 1.0f / 60.0f, clamp, stop);
    double seconds10 = flingSeconds(slowdown, 1.0f / 10.0f, clamp, stop);
    TEST_CHECK(std::abs(seconds10 - seconds60) < 0.15, "and lasts as long at 10 fps as at 60");
    TEST_CHECK(seconds60 > 0.9 && seconds60 < 1.3, "a fling runs for about a second");

    // The old per-frame stepping summed to factor/(1 - factor): the 60 fps value is what the constants were rescaled by.
    double oldAt60 = 1.0 * std::pow(1.0 - slowdown, 1.0 / 60.0) / (1.0 - std::pow(1.0 - slowdown, 1.0 / 60.0));
    double oldAt10 = 1.0 * std::pow(1.0 - slowdown, 1.0 / 10.0) / (1.0 - std::pow(1.0 - slowdown, 1.0 / 10.0));
    TEST_CHECK(std::abs(oldAt60 - 12.535) < 0.01, "the old 60 fps fling summed to the new clamp");
    TEST_CHECK(oldAt10 < oldAt60 / 4.0, "and the old 10 fps fling covered under a quarter of it");
}
