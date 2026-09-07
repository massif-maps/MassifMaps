/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_VIEWSTATE_H_
#define _MASSIF_VT_VIEWSTATE_H_

#include <cmath>
#include <array>

#include <cglib/vec.h>
#include <cglib/mat.h>
#include <cglib/bbox.h>
#include <cglib/frustum3.h>

namespace massif::vt {
    struct ViewState final {
        float zoom = 0;
        float rotation = 0;
        float tilt = 0;
        float aspect = 1;
        float resolution = 0;
        float zoomScale = 1;
        // Distance from the camera to the focus point, in internal units; 0 = not set, and the label
        // scaling falls back to where the view axis meets z=0. That fallback is only right for a focus
        // ON the ground - lift the viewpoint or flatten the tilt and it runs away.
        float focusDistance = 0;
        // Metres from the camera to the label being evaluated (style variable view::distance). Only set
        // where the evaluation is PER LABEL - the culler's ranking pass - since the renderer evaluates a
        // style function once per batch and a per-label value there would break batching.
        float labelDistance = 0;
        // Planar render projection, with or without 3D terrain. Labels then keep a CONSTANT ON-SCREEN
        // SIZE (tangram-style) and snap to the pixel grid; terrain only made it visible.

        // mapbox's ["measure-light", "brightness"]: how bright the scene light is, 0-1. A style reads it
        // as `view::brightness`, resolved per frame, so a label dims with the hour without a re-decode.
        float lightBrightness = 1.0f;
        bool planarProjection = false;
        cglib::mat4x4<double> projectionMatrix = cglib::mat4x4<double>::identity();
        cglib::mat4x4<double> cameraMatrix = cglib::mat4x4<double>::identity();
        cglib::vec3<double> origin = cglib::vec3<double>::zero();
        cglib::frustum3<double> frustum = cglib::gl_projection_frustum(cglib::mat4x4<double>::identity());
        std::array<cglib::vec3<float>, 3> orientation = { { cglib::vec3<float>(1, 0, 0), cglib::vec3<float>(0, 1, 0), cglib::vec3<float>(0, 0, 1) } };

        ViewState() = default;

        explicit ViewState(const cglib::mat4x4<double>& projectionMatrix, const cglib::mat4x4<double>& cameraMatrix, float zoom, float rotation, float tilt, float aspect, float resolution) : zoom(zoom), rotation(rotation), tilt(tilt), aspect(aspect), resolution(resolution), zoomScale(std::pow(2.0f, -zoom)), projectionMatrix(projectionMatrix), cameraMatrix(cameraMatrix), origin(), frustum(), orientation() {
            cglib::mat4x4<double> invCameraMatrix = cglib::inverse(cameraMatrix);
            origin = cglib::proj_p(cglib::col_vector(invCameraMatrix, 3));
            frustum = cglib::gl_projection_frustum(projectionMatrix * cameraMatrix);
            for (int i = 0; i < 3; i++) {
                orientation[i] = cglib::vec3<float>::convert(cglib::proj_o(cglib::col_vector(invCameraMatrix, i)));
            }
        }
    };
}

#endif
