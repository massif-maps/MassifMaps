/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_ORBITPIVOT_H_
#define _MASSIF_ORBITPIVOT_H_

#include <cglib/vec.h>
#include <cglib/mat.h>

namespace massif {

    /**
     * Turns the camera, focus and up vector rigidly by `angle` radians about `axis` through `pivot`, so the pivot keeps
     * its screen position and the camera keeps its distance to the focus (the zoom). The focus as pivot is the plain tilt.
     */
    inline void orbitAboutPivot(const cglib::vec3<double>& pivot, const cglib::vec3<double>& axis, double angle, cglib::vec3<double>& camera, cglib::vec3<double>& focus, cglib::vec3<double>& up) {
        cglib::mat4x4<double> transform = cglib::rotate4_matrix(axis, angle);
        camera = pivot + cglib::transform_vector(camera - pivot, transform);
        focus = pivot + cglib::transform_vector(focus - pivot, transform);
        up = cglib::transform_vector(up, transform);
    }

}

#endif
