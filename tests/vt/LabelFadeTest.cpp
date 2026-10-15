// The label fade (vt/LabelFade.h). Not covered: that VectorTileLayer's default reaches GLTileRenderer
// through TileRenderer - both drag the renderer in, so that is a device check.

#include "LabelFade.h"

#include <algorithm>

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

    // Building occlusion: a new label behind a building is judged on its first frame, never eased down from clear.
    const float step = labelOpacityStep(dt, DEFAULT_LABEL_BLENDING_SPEED, false);
    {
        float opacity = 0.0f;
        float occlusion = 1.0f;
        bool judged = false;
        float brightest = 0.0f;
        for (int frame = 0; frame < 30; frame++) {
            opacity = std::min(1.0f, opacity + step);
            occlusion = easeLabelOcclusion(occlusion, 0.0f, step, judged);
            judged = true;
            brightest = std::max(brightest, opacity * occlusion);
        }
        TEST_CHECK(brightest == 0.0f, "a new label wholly behind a building is never drawn visible");
    }
    {
        float occlusion = 0.0f;
        for (int frame = 0; frame < 2; frame++) {
            occlusion = easeLabelOcclusion(occlusion, 1.0f, step, true);
        }
        TEST_CHECK(occlusion < 0.1f, "a verdict that reads clear for two frames barely lifts a hidden label");
        for (int frame = 0; frame < 12; frame++) {
            occlusion = easeLabelOcclusion(occlusion, 0.0f, step, true);
        }
        TEST_CHECK(occlusion == 0.0f, "covered again, it is fully hidden within a few frames");
    }
    {
        float occlusion = 1.0f;
        int frames = 0;
        while (occlusion > 0.0f && frames < 1000) {
            occlusion = easeLabelOcclusion(occlusion, 0.0f, step, true);
            frames++;
        }
        int risingFrames = 0;
        while (occlusion < 1.0f && risingFrames < 1000) {
            occlusion = easeLabelOcclusion(occlusion, 1.0f, step, true);
            risingFrames++;
        }
        TEST_CHECK(frames * 3 < risingFrames, "a label hides much faster than it shows");
        TEST_CHECK(frames < 40, "and a covered label is gone within a third of a second");
    }
    TEST_CHECK(easeLabelOcclusion(1.0f, 0.25f, 1.0f, true) == 0.25f, "a snapped placement takes the verdict outright");
    TEST_CHECK(easeLabelOcclusion(0.25f, 1.0f, step, false) == 1.0f, "an unjudged label takes the verdict outright, up too");

    // The verdict of a half-hidden label wobbles while the camera looks around: the drawn opacity must not.
    {
        float held = 0.5f;
        float occlusion = 0.6f;
        float lowest = 1.0f, highest = 0.0f;
        for (int frame = 0; frame < 240; frame++) {
            float verdict = 0.5f + ((frame * 7) % 5 - 2) * 0.05f; // +-0.1 around 0.5
            held = holdLabelOcclusionVerdict(held, verdict, true);
            occlusion = easeLabelOcclusion(occlusion, 0.25f + 0.75f * held, step, true);
            lowest = std::min(lowest, occlusion);
            highest = std::max(highest, occlusion);
        }
        TEST_CHECK(held == 0.5f, "a verdict wobbling under the dead band is ignored");
        TEST_CHECK(highest - lowest <= 0.4f && highest - lowest > 0.0f, "its label eases to the held verdict once");
        TEST_CHECK(holdLabelOcclusionVerdict(0.5f, 0.7f, true) == 0.7f, "a change over the dead band is taken");
        TEST_CHECK(holdLabelOcclusionVerdict(0.8f, 1.0f, true) == 1.0f && holdLabelOcclusionVerdict(0.2f, 0.0f, true) == 0.0f, "fully clear and fully hidden always are");
    }
    {
        // A verdict flipping between two halves of the range every frame: the opacity ripples by a few percent, not by half.
        float held = 0.25f;
        float occlusion = 0.4f;
        for (int frame = 0; frame < 300; frame++) {
            held = holdLabelOcclusionVerdict(held, frame % 2 ? 0.75f : 0.25f, true);
            occlusion = easeLabelOcclusion(occlusion, 0.25f + 0.75f * held, step, true);
        }
        float lowest = 1.0f, highest = 0.0f;
        for (int frame = 300; frame < 360; frame++) {
            held = holdLabelOcclusionVerdict(held, frame % 2 ? 0.75f : 0.25f, true);
            occlusion = easeLabelOcclusion(occlusion, 0.25f + 0.75f * held, step, true);
            lowest = std::min(lowest, occlusion);
            highest = std::max(highest, occlusion);
        }
        TEST_CHECK(highest - lowest < 0.1f, "a verdict flipping every frame ripples the opacity by under a tenth");
    }
}
