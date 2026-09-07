/*
 * Tests for what a zoom NUMBER means (all/native/graphics/ZoomConvention.h): the camera distance
 * it calibrates, and the nominal tile the level of detail is measured against.
 *
 * The case that matters is that the offset moves the camera and NOTHING ELSE. Scaling the level
 * of detail with it was the obvious reading and is wrong: the LOD budget is a screen-area rule, so
 * doubling it fetches a level coarser and draws it twice as large - which doubles every label and
 * line, the exact trap that raising TileDrawSize falls into. Measured both ways on the web build;
 * the A/B is in docs/maintenance/web-build.md. So the rule below exposes the camera tile only, and
 * the LOD deliberately reads getTileDrawSize() straight.
 */

#include "graphics/ZoomConvention.h"

#include <algorithm>
#include <cmath>

using namespace massif;

#include "TestCheck.h"

namespace {

    const double WORLD = 156543.0 * 256;   // stands in for Const::WORLD_SIZE; only ratios matter
    const double TAN_HALF_FOVY = std::tan(35.0 * M_PI / 180.0);

    bool nearly(double value, double expected) {
        return std::fabs(value - expected) < 1.0e-9 * std::max(1.0, std::fabs(expected));
    }

    void testNoOffsetIsTheSlippyMapConvention() {
        // At zoom 0 one tile of the world covers exactly the nominal tile on screen. Read the
        // other way: the screen is screenHeight / tilePixels tiles tall, which is the definition.
        double distance = ZoomConvention::zoom0Distance(1000, WORLD, 256, 0, TAN_HALF_FOVY, 1);
        double screenHeightInWorldUnits = 2 * distance * TAN_HALF_FOVY;
        TEST_CHECK(nearly(screenHeightInWorldUnits, WORLD * 1000 / 256),
                   "at zoom 0 the screen is screenHeight / tileDrawSize worlds tall");
        TEST_CHECK(nearly(ZoomConvention::cameraTileSize(256, 0), 256),
                   "and with no offset the camera tile is the tile draw size itself");
    }

    void testAnOffsetOfOneIsMaplibresConvention() {
        double base = ZoomConvention::zoom0Distance(1000, WORLD, 256, 0, TAN_HALF_FOVY, 1);
        double offset = ZoomConvention::zoom0Distance(1000, WORLD, 256, 1, TAN_HALF_FOVY, 1);
        TEST_CHECK(nearly(offset, base * 0.5),
                   "one level of offset halves the camera distance at the same zoom number");
        // Which is the same as saying: offset zoom 13 is the old zoom 14.
        TEST_CHECK(nearly(offset / std::pow(2.0, 13), base / std::pow(2.0, 14)),
                   "so zoom 13 with the offset is exactly zoom 14 without it");
    }

    void testTheOffsetIsTheCameraOnly() {
        // The rule offers a camera tile and no other size, so there is nothing for the LOD or the
        // drape to accidentally scale by. Both read Options::getTileDrawSize() directly.
        TEST_CHECK(nearly(ZoomConvention::cameraTileSize(256, 1), 512),
                   "an offset of 1 calibrates the camera on maplibre's 512 tile");
        // Which is the whole contract: at zoom 13 with the offset, the camera is where zoom 14 put
        // it, so the same screen-area LOD picks the same tiles and draws them the same size.
        double offsetAt13 = ZoomConvention::zoom0Distance(1000, WORLD, 256, 1, TAN_HALF_FOVY, 1)
                          / std::pow(2.0, 13);
        double plainAt14 = ZoomConvention::zoom0Distance(1000, WORLD, 256, 0, TAN_HALF_FOVY, 1)
                         / std::pow(2.0, 14);
        TEST_CHECK(nearly(offsetAt13, plainAt14),
                   "so an offset renumbers the zoom and changes nothing that is drawn");
    }

    void testTheRendererIsToldTheTileZoom() {
        // The renderer sizes by 2^(zoom - tileZoom), and the tiles picked at zoom 13 with an
        // offset of 1 are the ones zoom 14 picks without it. Hand it the reported zoom and that
        // exponent is off by one, which draws every label and line at double size - observed,
        // and the reason this function exists at all.
        TEST_CHECK(nearly(ZoomConvention::renderZoom(13, 1), 14),
                   "at zoom 13 with an offset of 1 the renderer is told 14, which is the tile zoom");
        TEST_CHECK(nearly(ZoomConvention::renderZoom(13, 0), 13),
                   "and with no offset it is told exactly what the app sees");
    }

    void testTheOffsetIsInterchangeableWithTheTileDrawSize() {
        // Both are a scale on the nominal tile, so a doubled draw size and an offset of 1 put the
        // camera in the same place. What differs is everything ELSE the draw size feeds - the
        // renderer's size basis above all - which is why the offset exists as its own knob.
        double byOffset = ZoomConvention::zoom0Distance(1000, WORLD, 256, 1, TAN_HALF_FOVY, 1);
        double byDrawSize = ZoomConvention::zoom0Distance(1000, WORLD, 512, 0, TAN_HALF_FOVY, 1);
        TEST_CHECK(nearly(byOffset, byDrawSize),
                   "an offset of 1 and a doubled tile draw size calibrate the same camera");
    }

    void testDisplayScalingDoesNotChangeTheConvention() {
        // A retina screen puts twice the pixels on the same tile, so the camera is twice as far in
        // pixel terms and the map looks identical. The convention is about points, not pixels.
        double onePx = ZoomConvention::zoom0Distance(1000, WORLD, 256, 0, TAN_HALF_FOVY, 1);
        double twoPx = ZoomConvention::zoom0Distance(2000, WORLD, 256, 0, TAN_HALF_FOVY, 2);
        TEST_CHECK(nearly(onePx, twoPx),
                   "twice the pixels at twice the density is the same map");
        TEST_CHECK(nearly(ZoomConvention::cameraTileSize(256, 1) * 2, 1024),
                   "the camera tile in pixels carries both the offset and the density");
    }

    void testDegenerateInputsDoNotDivideByZero() {
        TEST_CHECK(ZoomConvention::zoom0Distance(1000, WORLD, 0, 0, TAN_HALF_FOVY, 1) == 0,
                   "a zero tile draw size gives no distance rather than infinity");
        TEST_CHECK(ZoomConvention::zoom0Distance(1000, WORLD, 256, 0, 0, 1) == 0,
                   "and neither does a zero field of view");
    }

    void testAFractionalOffsetIsAllowed() {
        // Not a use case anyone has asked for, but the type is a float and half a level has to
        // mean half a level rather than round to something.
        double half = ZoomConvention::cameraTileSize(256, 0.5);
        TEST_CHECK(nearly(half * half, 256.0 * 512.0),
                   "half a level is the geometric mean of the two whole ones");
    }
}

void testZoomConvention() {
    testNoOffsetIsTheSlippyMapConvention();
    testAnOffsetOfOneIsMaplibresConvention();
    testTheOffsetIsTheCameraOnly();
    testTheRendererIsToldTheTileZoom();
    testTheOffsetIsInterchangeableWithTheTileDrawSize();
    testDisplayScalingDoesNotChangeTheConvention();
    testDegenerateInputsDoNotDivideByZero();
    testAFractionalOffsetIsAllowed();
}
