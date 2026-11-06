/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_COLORRAMP_H_
#define _MASSIF_VT_COLORRAMP_H_

#include <cstdint>
#include <vector>

namespace massif::vt {
    struct LineProgress {
        double start = 0;
        double end = 1;
    };

    // A colour along a line, 0..1, as premultiplied RGBA8 texels the renderer samples without filtering.
    struct ColorRamp {
        static constexpr int WIDTH = 2048;

        std::vector<std::uint8_t> texels; // WIDTH * 4
    };
}

#endif
