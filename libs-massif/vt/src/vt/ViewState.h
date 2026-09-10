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
        // The frustum LABELS are placed against: the one above, grown by labelPadding screen pixels
        // on every side. Placing a label just outside the viewport is what lets it be at full opacity
        // by the time it scrolls in, instead of starting its fade at the edge - see labelPadding.
        cglib::frustum3<double> labelFrustum = cglib::gl_projection_frustum(cglib::mat4x4<double>::identity());
        // How far outside the viewport that reaches, in screen pixels. maplibre pads a flat 100
        // (CollisionIndex viewportPadding) and says so itself: "increases label stability, but it's
        // expensive". Ours is scaled by sin(tilt), because 100 screen pixels near the HORIZON are
        // kilometres of map and thousands of labels, while at top-down they are 100 pixels of map.
        // That keeps the band roughly constant in WORLD terms rather than in screen terms.
        float labelPadding = 0;
        std::array<cglib::vec3<float>, 3> orientation = { { cglib::vec3<float>(1, 0, 0), cglib::vec3<float>(0, 1, 0), cglib::vec3<float>(0, 0, 1) } };

        // maplibre's viewportPadding, at top-down. Below MIN_LABEL_PADDING the band is not worth the
        // labels it drags in, so it is the floor rather than 0: a label still needs somewhere to be
        // placed before it crosses the edge.
        static constexpr float MAX_LABEL_PADDING = 100.0f;
        static constexpr float MIN_LABEL_PADDING = 20.0f;

        ViewState() = default;

        explicit ViewState(const cglib::mat4x4<double>& projectionMatrix, const cglib::mat4x4<double>& cameraMatrix, float zoom, float rotation, float tilt, float aspect, float resolution) : zoom(zoom), rotation(rotation), tilt(tilt), aspect(aspect), resolution(resolution), zoomScale(std::pow(2.0f, -zoom)), projectionMatrix(projectionMatrix), cameraMatrix(cameraMatrix), origin(), frustum(), labelFrustum(), orientation() {
            cglib::mat4x4<double> invCameraMatrix = cglib::inverse(cameraMatrix);
            origin = cglib::proj_p(cglib::col_vector(invCameraMatrix, 3));
            frustum = cglib::gl_projection_frustum(projectionMatrix * cameraMatrix);
            labelPadding = calculateLabelPadding(tilt);
            labelFrustum = cglib::gl_projection_frustum(paddedProjectionMatrix() * cameraMatrix);
            for (int i = 0; i < 3; i++) {
                orientation[i] = cglib::vec3<float>::convert(cglib::proj_o(cglib::col_vector(invCameraMatrix, i)));
            }
        }

        // Tilt 90 is straight down and 0 is the horizon (graphics/ViewState.h), so sin(tilt) IS the
        // foreshortening of the ground plane - the factor the world size of a screen pixel grows by.
        static float calculateLabelPadding(float tilt) {
            float scale = std::sin(std::max(0.0f, tilt) * 3.14159265358979f / 180.0f);
            return std::max(MIN_LABEL_PADDING, MAX_LABEL_PADDING * scale);
        }

        // Clip space is [-1, 1] over the viewport, so fitting `padding` more pixels on each side is
        // a shrink of x and y by viewport / (viewport + 2 * padding). Static because the tile culler
        // needs the same band to decide which tiles must be there for those labels to exist.
        static cglib::mat4x4<double> paddedProjectionMatrix(const cglib::mat4x4<double>& projectionMatrix, float padding, float aspect, float resolution) {
            float width = resolution * aspect, height = resolution;
            if (!(padding > 0) || !(width > 0) || !(height > 0)) {
                return projectionMatrix;
            }
            cglib::mat4x4<double> scale = cglib::mat4x4<double>::identity();
            scale(0, 0) = width / (width + 2 * padding);
            scale(1, 1) = height / (height + 2 * padding);
            return scale * projectionMatrix;
        }

    private:
        cglib::mat4x4<double> paddedProjectionMatrix() const {
            return paddedProjectionMatrix(projectionMatrix, labelPadding, aspect, resolution);
        }
    };
}

#endif
