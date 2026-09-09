/*
 * The box a label plate covers (vt/Styles.h). Padding is measured from content, so a plate behind
 * two different labels comes out two different sizes; a style that needs ONE size - a map pin, a
 * rating chip - sets the size instead. Header-only maths, so it links without the renderer.
 *
 * NOT covered here: that the culler and the geometry both go through this. They call it from
 * Label.cpp with the same style, which is a code path rather than a value.
 */

#include "Styles.h"

#include "TestCheck.h"

#include <cmath>

using namespace massif::vt;

namespace {
    cglib::bbox2<float> box(float minX, float minY, float maxX, float maxY) {
        return cglib::bbox2<float>(cglib::vec2<float>(minX, minY), cglib::vec2<float>(maxX, maxY));
    }

    bool near(float value, float expected) {
        return std::abs(value - expected) < 0.001f;
    }
}

void testPlateBox() {
    LabelPlateStyle padded;
    padded.padding = cglib::vec2<float>(3, 2);

    // 1. No size: the content grown by the padding and the border, per axis.
    {
        cglib::bbox2<float> plate = calculatePlateBox(box(-10, -4, 10, 4), padded, 0.0f, 1.0f, 1.0f);
        TEST_CHECK(near(plate.min(0), -13) && near(plate.max(0), 13), "padding grows the box on x");
        TEST_CHECK(near(plate.min(1), -6) && near(plate.max(1), 6), "padding grows the box on y");

        cglib::bbox2<float> bordered = calculatePlateBox(box(-10, -4, 10, 4), padded, 2.0f, 1.0f, 1.0f);
        TEST_CHECK(near(bordered.min(0), -15) && near(bordered.max(0), 15), "the border grows it too - the quad is the outer shape");
    }

    // 2. A fixed size wins over the padding, and is the OUTER size: the same plate whatever it
    // sits behind, which is the whole point of asking for one.
    {
        LabelPlateStyle sized = padded;
        sized.size = cglib::vec2<float>(24, 24);

        cglib::bbox2<float> narrow = calculatePlateBox(box(-3, -4, 3, 4), sized, 2.0f, 1.0f, 1.0f);
        cglib::bbox2<float> wide = calculatePlateBox(box(-10, -4, 10, 4), sized, 2.0f, 1.0f, 1.0f);
        TEST_CHECK(near(narrow.max(0) - narrow.min(0), 24) && near(narrow.max(1) - narrow.min(1), 24), "a fixed size is the plate's size");
        TEST_CHECK(near(wide.max(0) - wide.min(0), 24), "wider content does not widen a sized plate");
        TEST_CHECK(near(narrow.min(0), -12) && near(narrow.max(0), 12), "a sized plate is centred on the content");
    }

    // 3. Per axis: a height alone pins the height and leaves the width on the padding, which is
    // what a chip of one height around text of any length needs.
    {
        LabelPlateStyle heightOnly = padded;
        heightOnly.size = cglib::vec2<float>(0, 20);
        cglib::bbox2<float> plate = calculatePlateBox(box(-10, -4, 10, 4), heightOnly, 0.0f, 1.0f, 1.0f);
        TEST_CHECK(near(plate.max(1) - plate.min(1), 20), "the axis with a size takes it");
        TEST_CHECK(near(plate.max(0) - plate.min(0), 26), "the other axis keeps its padding");
    }

    // 4. Content is in glyph units and the size in pixels, so the two scales apply to their own.
    {
        LabelPlateStyle sized;
        sized.size = cglib::vec2<float>(10, 10);
        cglib::bbox2<float> plate = calculatePlateBox(box(-1, -1, 1, 1), sized, 0.0f, 4.0f, 0.5f);
        TEST_CHECK(near(plate.max(0) - plate.min(0), 5), "a fixed size is a PIXEL size, converted like the padding");

        cglib::bbox2<float> offCentre = calculatePlateBox(box(0, 0, 2, 2), sized, 0.0f, 4.0f, 0.5f);
        TEST_CHECK(near(offCentre.min(0), 1.5f) && near(offCentre.max(0), 6.5f), "it centres on the scaled content, not on the origin");
    }
}
