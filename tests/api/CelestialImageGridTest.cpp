// The bitmap-to-sky mapping of a CelestialImage (celestial/CelestialImageGrid.h), Stellarium's art
// transform. Not covered: CelestialImage itself and its drawing, which need the layer and GL.

#include "celestial/CelestialImageGrid.h"

#include "TestCheck.h"

#include <cmath>

using namespace massif;

namespace {
    cglib::vec3<double> direction(double azimuthDeg, double altitudeDeg) {
        double az = azimuthDeg * 3.14159265358979323846 / 180.0;
        double alt = altitudeDeg * 3.14159265358979323846 / 180.0;
        return cglib::vec3<double>(std::cos(alt) * std::sin(az), std::cos(alt) * std::cos(az), std::sin(alt));
    }

    bool same(const cglib::vec3<double>& a, const cglib::vec3<double>& b) {
        return cglib::norm(a - b) < 1.0e-9;
    }
}

void testCelestialImageGrid() {
    const cglib::vec3<double> stars[3] = { direction(10, 30), direction(40, 35), direction(15, 60) };
    std::vector<cglib::vec3<double> > grid;

    const cglib::vec2<double> corners[3] = { cglib::vec2<double>(0, 0), cglib::vec2<double>(1, 0), cglib::vec2<double>(0, 1) };
    TEST_CHECK(CelestialImageGrid::Build(corners, stars, 2, grid) && grid.size() == 9, "a 2x2 grid has 9 directions");
    TEST_CHECK(same(grid[0], stars[0]) && same(grid[2], stars[1]) && same(grid[6], stars[2]),
               "anchors on the bitmap corners land on their stars, row by row from the top");
    cglib::mat3x3<double> topRow = cglib::mat3x3<double>::zero();
    for (int i = 0; i < 3; i++) {
        for (int c = 0; c < 3; c++) {
            topRow(c, i) = grid[i](c);
        }
    }
    TEST_CHECK(std::abs(cglib::determinant(topRow)) < 1.0e-12, "a straight row of the bitmap is a great circle on the sky");
    TEST_CHECK(!same(grid[4], cglib::unit(stars[0] + stars[1] + stars[2])), "the centre is placed by the plane, not by averaging the stars");

    // Anchors inside the bitmap, as Stellarium's are: each must still land on its star.
    const cglib::vec2<double> inside[3] = { cglib::vec2<double>(0.25, 0.5), cglib::vec2<double>(0.75, 0.25), cglib::vec2<double>(0.5, 1.0) };
    TEST_CHECK(CelestialImageGrid::Build(inside, stars, 4, grid) && same(grid[2 * 5 + 1], stars[0]) && same(grid[1 * 5 + 3], stars[1]) && same(grid[4 * 5 + 2], stars[2]),
               "anchors inside the bitmap land on their stars too");
    bool unit = true;
    for (const cglib::vec3<double>& point : grid) {
        unit = unit && std::abs(cglib::norm(point) - 1.0) < 1.0e-12;
    }
    TEST_CHECK(unit, "every direction is a unit vector");

    const cglib::vec2<double> collinear[3] = { cglib::vec2<double>(0, 0), cglib::vec2<double>(0.5, 0.5), cglib::vec2<double>(1, 1) };
    TEST_CHECK(!CelestialImageGrid::Build(collinear, stars, 4, grid) && grid.empty(), "collinear anchors in the bitmap place nothing");

    const cglib::vec3<double> horizon[3] = { direction(0, 0), direction(90, 0), direction(200, 0) };
    TEST_CHECK(!CelestialImageGrid::Build(corners, horizon, 4, grid) && grid.empty(), "stars whose plane holds the observer place nothing");

    TEST_CHECK(!CelestialImageGrid::Build(corners, stars, 0, grid) && grid.empty(), "a grid without cells places nothing");
}
