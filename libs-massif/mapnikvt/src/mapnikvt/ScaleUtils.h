/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_SCALEUTILS_H_
#define _MASSIF_MAPNIKVT_SCALEUTILS_H_

#include <cmath>

namespace massif::mvt {
    inline float zoom2ScaleDenominator(float zoom) {
        return 559082264.028f / std::pow(2.0f, zoom);
    }

    inline float scaleDenominator2Zoom(float scaleDenom) {
        return std::log(559082264.028f / scaleDenom) / std::log(2.0f);
    }
}

#endif
