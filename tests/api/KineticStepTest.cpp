/*
 * How much of its remaining delta a kinetic gesture spends in one frame
 * (renderers/components/KineticStep.h).
 *
 * The case this exists for: the pan fling decayed on a wall clock but stepped by the WHOLE
 * remaining delta every frame, so the ground it covered was proportional to the frame count -
 * ~13.4 units at 60 fps against ~2.7 at 10. Martin reported it as "launch the pan and it stops
 * during inertia, clearly hanging" at z18.2 in a dense city: the map was not hanging, the fling
 * was spending its wall-clock budget while starved of frames.
 *
 * NOT covered here: the feel of the rescaled constants, which is a device check.
 */

#include "renderers/components/KineticStep.h"

#include "TestCheck.h"

#include <cmath>

using namespace massif;

namespace {
    // The distance a fling covers: step by the fraction, keep the rest, until the stop tolerance.
    double flingDistance(float slowdown, float deltaSeconds, double delta, double stopTolerance) {
        double travelled = 0;
        for (int frame = 0; frame < 100000 && delta >= stopTolerance; frame++) {
            double step = delta * kineticStepFraction(slowdown, deltaSeconds);
            travelled += step;
            delta -= step;
        }
        return travelled;
    }

    // ... and how long it takes, in seconds.
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

    // A frame of no time spends nothing; a frame of forever spends it all.
    TEST_CHECK(kineticStepFraction(slowdown, 0.0f) == 0.0f, "a zero-length frame consumes no delta");
    TEST_CHECK(std::abs(kineticStepFraction(slowdown, 10.0f) - 1.0f) < 1.0e-6, "a very long frame consumes all of it");

    // A longer frame consumes more of the delta than a short one - that is the whole point.
    TEST_CHECK(kineticStepFraction(slowdown, 0.1f) > kineticStepFraction(slowdown, 1.0f / 60.0f),
               "a 10 fps frame consumes more of the delta than a 60 fps frame");

    // The invariant: the same fling covers the same ground however many frames it is drawn in.
    double at60 = flingDistance(slowdown, 1.0f / 60.0f, clamp, stop);
    double at30 = flingDistance(slowdown, 1.0f / 30.0f, clamp, stop);
    double at10 = flingDistance(slowdown, 1.0f / 10.0f, clamp, stop);
    TEST_CHECK(std::abs(at30 - at60) < 0.1, "a fling covers the same ground at 30 fps as at 60");
    TEST_CHECK(std::abs(at10 - at60) < 0.1, "and the same at 10 fps");

    // It covers nearly all of the delta it was given, whatever the frame rate.
    TEST_CHECK(at60 > clamp - 2.0 * stop && at60 < clamp, "a fling spends the delta it was handed");

    // Duration is unchanged by frame rate too - the decay was always a wall clock.
    double seconds60 = flingSeconds(slowdown, 1.0f / 60.0f, clamp, stop);
    double seconds10 = flingSeconds(slowdown, 1.0f / 10.0f, clamp, stop);
    TEST_CHECK(std::abs(seconds10 - seconds60) < 0.15, "and lasts as long at 10 fps as at 60");
    TEST_CHECK(seconds60 > 0.9 && seconds60 < 1.3, "a fling runs for about a second");

    // The old per-frame stepping, for contrast: delta * factor/(1 - factor) summed at 60 fps is
    // the 12.535 the constants were rescaled by, and it fell to a fifth of that at 10 fps.
    double oldAt60 = 1.0 * std::pow(1.0 - slowdown, 1.0 / 60.0) / (1.0 - std::pow(1.0 - slowdown, 1.0 / 60.0));
    double oldAt10 = 1.0 * std::pow(1.0 - slowdown, 1.0 / 10.0) / (1.0 - std::pow(1.0 - slowdown, 1.0 / 10.0));
    TEST_CHECK(std::abs(oldAt60 - 12.535) < 0.01, "the old 60 fps fling summed to the new clamp");
    TEST_CHECK(oldAt10 < oldAt60 / 4.0, "and the old 10 fps fling covered under a quarter of it");
}
