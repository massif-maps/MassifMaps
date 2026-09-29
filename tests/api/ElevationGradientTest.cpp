/*
 * Tests for the lit surfaces' normal source (all/native/terrain/ElevationGradient.h): the half float
 * conversion, the forward differences and their rects, and that sampling them half a texel back with
 * linear filtering is the quadratic stencil the fragment shader used to evaluate from nine taps.
 *
 * NOT covered here: the RG16F upload, the border patch and the shader's two fetches - device checks,
 * at the camera in docs/internals/rendering/08-lighting-sky-fog.md.
 */

#include "terrain/ElevationGradient.h"

#include <cmath>
#include <vector>

using namespace massif;

#include "TestCheck.h"

namespace {

    void testHalfRoundTrip() {
        const float values[] = { 0.0f, 1.0f, -1.0f, 0.5f, 0.001f, -0.037f, 12.25f, 1234.5f, -4321.0f };
        bool exactEnough = true;
        for (float v : values) {
            float back = ElevationGradient::fromHalf(ElevationGradient::toHalf(v));
            exactEnough = exactEnough && std::fabs(back - v) <= std::fabs(v) / 1024.0f + 1.0e-7f;
        }
        TEST_CHECK(exactEnough, "a half keeps 11 bits of a height difference, from a millimetre to kilometres");
        TEST_CHECK(ElevationGradient::fromHalf(ElevationGradient::toHalf(1.0e6f)) == 65504.0f, "past the half range the difference clamps to the largest finite half, not infinity");
        TEST_CHECK(std::fabs(ElevationGradient::fromHalf(ElevationGradient::toHalf(1.0e-5f)) - 1.0e-5f) < 1.0e-7f, "a subnormal difference survives");
    }

    void testForwardDifferences() {
        const int W = 4, H = 3;
        auto heightAt = [](int x, int y) { return static_cast<float>(x * x + 10 * y); };
        std::vector<std::uint16_t> full;
        ElevationGradient::encode(heightAt, W, H, 0, 0, W, H, full);
        auto r = [&](int x, int y) { return ElevationGradient::fromHalf(full[(y * W + x) * 2]); };
        auto g = [&](int x, int y) { return ElevationGradient::fromHalf(full[(y * W + x) * 2 + 1]); };
        TEST_CHECK(r(0, 0) == 1 && r(1, 1) == 3 && r(2, 2) == 5, "r is h(x + 1, y) - h(x, y)");
        TEST_CHECK(g(0, 0) == 10 && g(3, 1) == 10, "g is h(x, y + 1) - h(x, y)");
        TEST_CHECK(r(3, 0) == 0 && g(2, 2) == 0, "nothing past the last column or row");

        std::vector<std::uint16_t> rect;
        ElevationGradient::encode(heightAt, W, H, 1, 1, 3, 2, rect);
        bool same = rect.size() == 12;
        for (int y = 0; y < 2 && same; y++) {
            for (int x = 0; x < 3; x++) {
                same = same && rect[(y * 3 + x) * 2] == full[((y + 1) * W + x + 1) * 2] && rect[(y * 3 + x) * 2 + 1] == full[((y + 1) * W + x + 1) * 2 + 1];
            }
        }
        TEST_CHECK(same, "a rect, as a border patch writes it, is that slice of the whole texture");
    }

    void testHalfTexelBackIsQuadraticStencil() {
        // Along a row: texel c's stencil gradient is grad0 + curv * f for f in [-0.5, 0.5] from its centre.
        const float h[] = { 3.0f, 7.5f, 6.0f, 11.0f, 30.0f, 28.5f };
        const int N = 6;
        std::vector<std::uint16_t> gradient;
        auto heightAt = [&h](int x, int) { return h[x]; };
        ElevationGradient::encode(heightAt, N, 1, 0, 0, N, 1, gradient);
        double worst = 0;
        for (int c = 1; c + 1 < N; c++) {
            for (double f = -0.5; f <= 0.5; f += 0.125) {
                double grad0 = (h[c + 1] - h[c - 1]) * 0.5;
                double curv = h[c + 1] - 2.0 * h[c] + h[c - 1];
                double stencil = grad0 + curv * f;
                // Linear filtering at p - 0.5, p = c + 0.5 + f in texels: between the texel centres c - 0.5 and c + 0.5.
                double t = f + 0.5;
                double filtered = (1.0 - t) * ElevationGradient::fromHalf(gradient[(c - 1) * 2]) + t * ElevationGradient::fromHalf(gradient[c * 2]);
                worst = std::max(worst, std::fabs(filtered - stencil));
            }
        }
        TEST_CHECK(worst < 1.0e-6, "sampled half a texel back, the forward differences are the stencil's gradient");
    }

}

void testElevationGradient() {
    testHalfRoundTrip();
    testForwardDifferences();
    testHalfTexelBackIsQuadraticStencil();
}
