/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_VIEWSTATE_H_
#define _CARTO_VT_VIEWSTATE_H_

#include <cmath>
#include <array>

#include <cglib/vec.h>
#include <cglib/mat.h>
#include <cglib/bbox.h>
#include <cglib/frustum3.h>

namespace carto::vt {
    struct ViewState final {
        float zoom = 0;
        float rotation = 0;
        float tilt = 0;
        float aspect = 1;
        float resolution = 0;
        float zoomScale = 1;
        // Distance from the camera to the focus point, in internal units; 0 = not set, and the
        // label scaling then falls back to where the view axis meets the z=0 plane. That fallback
        // is only right for a camera whose focus IS on the ground: lift the viewpoint (free roam)
        // or flatten the tilt towards the horizon and it runs away, taking every label's size with
        // it. The application knows the real distance - see TileRenderer.
        float focusDistance = 0;
        // Meters from the camera to the label being evaluated (style variable view::distance).
        // Only set where the evaluation is PER LABEL - the culler's ranking pass; it is 0
        // everywhere else, because the renderer evaluates a style function once per batch and a
        // per-label value there would break batching (see GLTileRenderer::renderLabelPass).
        float labelDistance = 0;
        // Planar render projection, with or without 3D terrain. Labels then keep a CONSTANT
        // ON-SCREEN SIZE (tangram-style: their world size comes from the zoom alone, so the
        // perspective divide would otherwise blow them up towards the camera on a tilted view)
        // and snap to the pixel grid. Terrain only made it visible - the geometry z is the
        // terrain height there - but the correction is a property of the projection, not of it.
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
