/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_LINEGRADIENT_H_
#define _MASSIF_MAPNIKVT_LINEGRADIENT_H_

#include "Value.h"
#include "vt/Color.h"
#include "vt/ColorRamp.h"

#include <functional>
#include <memory>

namespace massif::mvt {
    // The ramp a `line-gradient` stop list draws: `position:color` pairs (a leading `step;` holds each colour until the next
    // position; two colours at one position are a hard edge) or [position, color] arrays. Null when neither.
    std::shared_ptr<const vt::ColorRamp> buildLineGradient(const Value& value);

    // A ramp from the premultiplied colour at the centre of each of its texels, positions 0..1. Equal ramps are one object.
    std::shared_ptr<const vt::ColorRamp> buildLineGradient(const std::function<vt::Color(double)>& colorAt);

    inline vt::Color premultiplyColor(const vt::Color& color) {
        return vt::Color(color[0] * color[3], color[1] * color[3], color[2] * color[3], color[3]);
    }
}

#endif
