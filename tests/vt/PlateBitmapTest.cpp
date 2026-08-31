/*
 * The label plate cell: what a text-background's fill and border are drawn from (see
 * vt/LabelPlateBitmap.h). Header-only maths, so it links without vt, freetype or a renderer.
 *
 * NOT covered here: the draw itself. That the fill and the border are mixed in ONE fragment - the
 * point of carrying both shapes in one cell - is labelFsh's, and is device-verified. The same goes
 * for the glyph halo, which is GLSL only: it is the same rule (the ink is punched out of the halo)
 * with the coverages coming from the distance field rather than from a cell.
 */

#include "LabelPlateBitmap.h"

#include "TestCheck.h"

using namespace massif::vt;

namespace {
    int fillCoverage(const std::vector<std::uint32_t>& data, int size, int x, int y) {
        return static_cast<int>(data[static_cast<std::size_t>(y) * size + x] & 0xff); // r
    }

    int plateCoverage(const std::vector<std::uint32_t>& data, int size, int x, int y) {
        return static_cast<int>((data[static_cast<std::size_t>(y) * size + x] >> 24) & 0xff); // a
    }
}

void testPlateBitmap() {
    // A border thinner than one texel still has to BE a border. Rounded to nothing, the cell comes
    // out filled, the border draws as a second plate behind the fill, and the fill goes dark over
    // it wherever the label is mid-fade.
    TEST_CHECK(snapPlateCell(4.0f, 0.1f).borderTexels >= 1, "a sub-texel border still gets a texel of its own");
    TEST_CHECK(snapPlateCell(4.0f, 0.0f).borderTexels == 0, "no border asked for, no border in the cell");
    // The cell spans the OUTER shape, so the geometry it hands back is the fill plus the border.
    TEST_CHECK(std::abs(snapPlateCell(6.0f, 2.0f).radius() - 8.0f) < 0.26f, "the cell's radius covers fill radius + border");
    TEST_CHECK(std::abs(snapPlateCell(6.0f, 2.0f).borderWidth() - 2.0f) < 0.26f, "the border it hands back is the one that was asked for");
    // A style may ask for anything; the cell stops growing, and the border never eats more than it.
    PlateCell huge = snapPlateCell(1000.0f, 500.0f);
    TEST_CHECK(huge.radiusTexels == MAX_PLATE_RADIUS * PLATE_SUPERSAMPLE, "an oversized radius is clamped to the cell's limit");
    TEST_CHECK(huge.borderTexels <= huge.radiusTexels, "the border stays inside the cell it is punched out of");

    // A plate with no border is premultiplied white: the fill's coverage IS the plate's, which is
    // what lets it keep the plain bitmap draw path.
    PlateCell plain = snapPlateCell(4.0f, 0.0f);
    std::vector<std::uint32_t> plainData = buildPlateBitmapData(plain);
    int plainSize = plain.size();
    bool plainMatches = true;
    for (int y = 0; y < plainSize; y++) {
        for (int x = 0; x < plainSize; x++) {
            plainMatches = plainMatches && fillCoverage(plainData, plainSize, x, y) == plateCoverage(plainData, plainSize, x, y);
        }
    }
    TEST_CHECK(plainMatches, "a borderless plate is premultiplied white - fill coverage equals the plate's");

    PlateCell cell = snapPlateCell(6.0f, 2.0f);
    std::vector<std::uint32_t> data = buildPlateBitmapData(cell);
    int size = cell.size();

    // The shader draws the border as (plate - fill), so a fill that reached past the plate's own
    // shape would come out as a NEGATIVE ring.
    bool inside = true;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            inside = inside && fillCoverage(data, size, x, y) <= plateCoverage(data, size, x, y);
        }
    }
    TEST_CHECK(inside, "the fill never covers more than the plate does");

    // The ring itself: a band where the plate is solid and the fill is absent. Without it the two
    // colours overlap, and an overlap is exactly what darkens while the label fades in.
    int mid = size / 2;
    int ringTexels = 0;
    for (int x = 0; x < size; x++) {
        if (plateCoverage(data, size, x, mid) == 255 && fillCoverage(data, size, x, mid) == 0) {
            ringTexels++;
        }
    }
    TEST_CHECK(ringTexels > 0, "a plate with a border has a ring the fill does not reach into");
    // Both sides of the scanline, each one border wide - within the texel the antialias ramp costs.
    TEST_CHECK(std::abs(ringTexels - 2 * cell.borderTexels) <= 2, "the ring is as thick as the border asked for, on both sides");

    TEST_CHECK(fillCoverage(data, size, mid, mid) == 255 && plateCoverage(data, size, mid, mid) == 255, "the middle of the plate is solid fill");

    // appendPlate samples one texel inside the cell, because the atlas gutter around it is
    // transparent and linear filtering bleeds into it. That texel has to be the shape's own edge.
    bool marginClear = true;
    for (int i = 0; i < size; i++) {
        marginClear = marginClear && plateCoverage(data, size, i, 0) == 0 && plateCoverage(data, size, i, size - 1) == 0
            && plateCoverage(data, size, 0, i) == 0 && plateCoverage(data, size, size - 1, i) == 0;
    }
    TEST_CHECK(marginClear, "the cell keeps a transparent one-texel margin on every side");

    // A plate whose fill is transparent still draws its border: the cell carries both shapes
    // whatever the colours are, and the colours are the batch's business.
    TEST_CHECK(buildPlateBitmapData(snapPlateCell(0.0f, 1.0f)).size() > 0, "a plate with no corner radius still builds a cell");
    PlateCell square = snapPlateCell(0.0f, 1.0f);
    std::vector<std::uint32_t> squareData = buildPlateBitmapData(square);
    TEST_CHECK(plateCoverage(squareData, square.size(), square.size() / 2, square.size() / 2) == 255, "a plate with no corner radius is not empty");
}
