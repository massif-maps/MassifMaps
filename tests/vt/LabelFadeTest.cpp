// The label fade (vt/LabelFade.h). Not covered: that VectorTileLayer's default reaches GLTileRenderer
// through TileRenderer - both drag the renderer in, so that is a device check.

#include "LabelFade.h"

using namespace massif::vt;

#include "TestCheck.h"

namespace {
    int framesToFullOpacity(float dt, float speed) {
        float opacity = 0.0f;
        int frames = 0;
        while (opacity < 1.0f && frames < 1000) {
            opacity += labelOpacityStep(dt, speed, false);
            frames++;
        }
        return frames;
    }
}

void testLabelFade() {
    const float dt = 1.0f / 60.0f;
    const int fadeFrames = LABEL_FADE_DURATION_MS * 60 / 1000;

    int frames = framesToFullOpacity(dt, DEFAULT_LABEL_BLENDING_SPEED);
    TEST_CHECK(frames >= fadeFrames && frames <= fadeFrames + 1, "at the default speed a label fades in over 300 ms at 60 fps");
    // The layer default was 1.0 and overwrote the renderer's every frame: a 60-frame fade.
    TEST_CHECK(framesToFullOpacity(dt, 1.0f) > frames + 30, "the default is not the 1 s fade the layer used to push");

    TEST_CHECK(labelOpacityStep(dt, 0.0f, false) == 1.0f, "speed 0 disables blending");
    TEST_CHECK(labelOpacityStep(dt, -1.0f, false) == 1.0f, "a negative speed disables blending");
    TEST_CHECK(labelOpacityStep(dt, DEFAULT_LABEL_BLENDING_SPEED, true) == 1.0f, "a snapped placement commits without a fade");
    TEST_CHECK(labelOpacityStep(dt, 2.0f, false) == dt * 2.0f, "an explicit speed is applied as fades per second");
}
