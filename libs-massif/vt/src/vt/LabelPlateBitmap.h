/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_LABELPLATEBITMAP_H_
#define _MASSIF_VT_LABELPLATEBITMAP_H_

#include <cstdint>
#include <vector>
#include <algorithm>
#include <cmath>

namespace massif::vt {
    // Texels per style pixel a plate cell is drawn at. At one texel per pixel the cell is upscaled
    // by the display's pixel ratio before it reaches the screen, and linear filtering turned a 1 px
    // border into a soft 3 px smear and flattened the corner arcs.
    inline constexpr int PLATE_SUPERSAMPLE = 4;
    inline constexpr int MAX_PLATE_RADIUS = 32; // style pixels; beyond this the cell stops growing

    // The atlas cell a label's plate is nine-sliced from, in TEXELS. It spans the plate's OUTER shape,
    // border included, and carries both of the plate's shapes: r is the fill's coverage, a the
    // whole plate's, so the difference between them is the border ring. One cell is what lets one
    // quad draw both - with the border as a second quad behind the fill, the two carry the same
    // alpha and fill-over-border leaves border * a * (1 - a) showing through wherever alpha < 1:
    // the plate darkened as it faded in, and a translucent fill darkened permanently.
    struct PlateCell {
        int radiusTexels = 0;
        int borderTexels = 0;

        // A square, and never narrower than its own corners. The extra 2 is the transparent margin
        // appendPlate needs: it samples one texel inside the cell, because the atlas gutter around
        // it is transparent and linear filtering would bleed into it.
        int size() const { return std::max(4 * PLATE_SUPERSAMPLE, radiusTexels * 2 + 2) + 2; }
        // What the cell was built at, in style pixels - the geometry has to use these rather than
        // the style's own values, or the quad does not line up with what was drawn into the cell.
        float radius() const { return static_cast<float>(radiusTexels) / PLATE_SUPERSAMPLE; }
        float borderWidth() const { return static_cast<float>(borderTexels) / PLATE_SUPERSAMPLE; }
    };

    // 'radius' is the FILL's corner radius and both are style pixels. A border thinner than a texel
    // still gets one: rounded to nothing it left the cell with no ring at all, which drew the
    // border as a second FILLED plate behind the fill - the darkening this whole path exists to
    // avoid.
    inline PlateCell snapPlateCell(float radius, float borderWidth) {
        PlateCell cell;
        float border = std::max(0.0f, borderWidth);
        cell.radiusTexels = std::min(MAX_PLATE_RADIUS * PLATE_SUPERSAMPLE, std::max(0, static_cast<int>((std::max(0.0f, radius) + border) * PLATE_SUPERSAMPLE + 0.5f)));
        cell.borderTexels = std::min(cell.radiusTexels, border > 0.0f ? std::max(1, static_cast<int>(border * PLATE_SUPERSAMPLE + 0.5f)) : 0);
        return cell;
    }

    // Antialiased coverage of a rounded rectangle spanning [x0,x1] x [y0,y1] (texel centres,
    // inclusive) with corner radius r, sampled at (x,y) - the standard rounded-box signed distance,
    // ramped over one texel. Measuring the corner offsets alone instead collapses to zero coverage
    // at r = 0, which is the DEFAULT radius: a plate with no explicit radius drew nothing at all.
    inline float roundedRectCoverage(float x, float y, float x0, float y0, float x1, float y1, float r) {
        float hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
        r = std::min(r, std::min(hx, hy));
        float qx = std::abs(x - (x0 + x1) * 0.5f) - hx + r;
        float qy = std::abs(y - (y0 + y1) * 0.5f) - hy + r;
        float outside = std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) + std::max(qy, 0.0f) * std::max(qy, 0.0f));
        float d = outside + std::min(std::max(qx, qy), 0.0f) - r;
        return std::min(1.0f, std::max(0.0f, 0.5f - d));
    }

    // The cell's texels, row major, size() x size(). Bytes are R, G, B, A: rgb is the FILL's
    // coverage and a the whole plate's, so a plate with no border comes out premultiplied white
    // and needs no shader of its own (see labelFsh).
    inline std::vector<std::uint32_t> buildPlateBitmapData(const PlateCell& cell) {
        int size = cell.size();
        std::vector<std::uint32_t> data(static_cast<std::size_t>(size) * size);
        float r = static_cast<float>(cell.radiusTexels);
        float b = static_cast<float>(cell.borderTexels);
        float hi = static_cast<float>(size - 2); // the shape spans [1, size - 2]
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                float fx = static_cast<float>(x), fy = static_cast<float>(y);
                float outer = roundedRectCoverage(fx, fy, 1.0f, 1.0f, hi, hi, r);
                float inner = cell.borderTexels > 0
                    ? std::min(outer, roundedRectCoverage(fx, fy, 1.0f + b, 1.0f + b, hi - b, hi - b, std::max(0.0f, r - b)))
                    : outer;
                std::uint32_t a = static_cast<std::uint32_t>(outer * 255.0f + 0.5f);
                std::uint32_t f = static_cast<std::uint32_t>(inner * 255.0f + 0.5f);
                data[static_cast<std::size_t>(y) * size + x] = (a << 24) | (f << 16) | (f << 8) | f;
            }
        }
        return data;
    }
}

#endif
