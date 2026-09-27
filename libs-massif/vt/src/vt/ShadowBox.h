/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_SHADOWBOX_H_
#define _MASSIF_VT_SHADOWBOX_H_

#include <algorithm>
#include <cmath>

namespace massif::vt {
    /**
     * A cascade's light-space box: its sphere, padded, the centre rounded to a lattice finer than the padding,
     * so the box repeats bit-for-bit while the camera moves inside it (08-lighting-sky-fog.md).
     */
    struct ShadowBox {
        double left = 0, right = 0, bottom = 0, top = 0;
        double centerDepth = 0, halfSize = 0;

        static ShadowBox fit(double centerX, double centerY, double centerDepth, double sphereRadius, double padding, int mapSize) {
            ShadowBox box;
            double halfSize = sphereRadius * (1.0 + padding);
            double sizeStep = std::pow(2.0, std::floor(std::log2(halfSize)) - 3.0); // eighths of a power of two
            box.halfSize = std::ceil(halfSize / sizeStep) * sizeStep;
            double texel = 2.0 * box.halfSize / std::max(1, mapSize);
            double lattice = std::max(texel, std::floor(2.0 * box.halfSize * padding / (1.0 + padding) / texel) * texel);
            auto snap = [lattice](double value) { return std::round(value / lattice) * lattice; };
            box.left = snap(centerX) - box.halfSize;
            box.right = box.left + 2.0 * box.halfSize;
            box.bottom = snap(centerY) - box.halfSize;
            box.top = box.bottom + 2.0 * box.halfSize;
            box.centerDepth = snap(centerDepth);
            return box;
        }
    };
}

#endif
