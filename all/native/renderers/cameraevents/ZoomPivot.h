/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_ZOOMPIVOT_H_
#define _MASSIF_ZOOMPIVOT_H_

#include <cglib/vec.h>

#include <cmath>

namespace massif {

    /**
     * The horizontal shift of the focus that keeps `pivot` on its own screen ray while the camera's distance to
     * the focus scales by `scale`, with the focus height left alone (04-terrain.md, "The zoom pivot").
     * False when no such shift exists (the pivot level with or above the camera); the caller keeps its own rule.
     */
    inline bool pinnedZoomShift(const cglib::vec3<double>& focus, const cglib::vec3<double>& camera, const cglib::vec3<double>& pivot, double scale, cglib::vec3<double>& shift) {
        cglib::vec3<double> a = pivot - focus;
        cglib::vec3<double> b = camera - focus;
        double denominator = a(2) - b(2);
        if (!(std::abs(denominator) > 1.0e-9)) {
            return false;
        }
        double k = (a(2) - scale * b(2)) / denominator;
        if (!(k > 0)) {
            return false;
        }
        shift = a * (1.0 - k) + b * (k - scale);
        shift(2) = 0;
        return true;
    }

}

#endif
