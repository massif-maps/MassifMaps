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

#include "LabelDistance.h"

namespace massif::vt {
    struct ViewState final {
        float zoom = 0;
        float rotation = 0;
        float tilt = 0;
        float aspect = 1;
        float resolution = 0;
        // The viewport's REAL height in device pixels; 0 = not set.
        //
        // `resolution` above is the NORMALIZED screen - 2 * tileDrawSize * dpiScale - which is what
        // every style size is measured against, and it says nothing about how many pixels the
        // viewport actually has. The two only differ by a constant, so anything sized off the zoom
        // comes out at the size the style asked whatever the screen is; but a SCREEN-space object
        // (a callout label) sized off `resolution` alone ends up scaled by the ratio between them -
        // so the same label was a different size on a taller screen, and shrank when the device was
        // turned. See Label::calculateLabelScale.
        float deviceResolution = 0;
        float zoomScale = 1;
        // Distance from the camera to the focus point, in internal units; 0 = not set, and the label
        // scaling falls back to where the view axis meets z=0. That fallback is only right for a focus
        // ON the ground - lift the viewpoint or flatten the tilt and it runs away.
        float focusDistance = 0;
        // Metres from the camera to the label being evaluated (style variable view::distance). Only set
        // where the evaluation is PER LABEL - the culler's ranking pass - since the renderer evaluates a
        // style function once per batch and a per-label value there would break batching.
        float labelDistance = 0;
        // How far a label may be PLACED, in multiples of focusDistance; 0 places every label however
        // far it is. Options::setLabelViewDistance, and the same rule the culler applies.
        //
        // It has to be here as well as on the culler, and that is the whole point: the culler's cut
        // reads a label's PLACEMENT, and Label::updatePlacement refuses to place one past its own
        // copy of the rule - so an application that raised the culler's limit still had every distant
        // label rejected one step earlier, with nothing for the culler to reconsider. A panorama is
        // exactly that case: its focus sits a couple of kilometres in front of a low camera, and the
        // default five times that cut every summit past ~15 km.
        float labelViewDistance = LabelDistance::DEFAULT_VIEW_DISTANCE;

        // mapbox's ["measure-light", "brightness"]: how bright the scene light is, 0-1. A style reads it
        // as `view::brightness`, resolved per frame, so a label dims with the hour without a re-decode.
        float lightBrightness = 1.0f;
        // How much of the perspective divide a label keeps, 0-1. 0 cancels it, for the constant
        // on-screen size below; 0.5 is maplibre's clamp(0.5 + 0.5 * distance_ratio) - a distant
        // label shrinks, at half the rate the projection alone would shrink it.
        float labelPerspectiveScaling = 0.0f;
        bool planarProjection = false;
        cglib::mat4x4<double> projectionMatrix = cglib::mat4x4<double>::identity();
        cglib::mat4x4<double> cameraMatrix = cglib::mat4x4<double>::identity();
        // The view-projection and its INVERSE, resolved once with the rest of the frame's camera.
        // Label::setupCoordinateSystem snaps every point label's anchor to the screen pixel grid and
        // needs both to do it - and it ran the product and a 4x4 double inverse PER LABEL, per frame,
        // off values identical for the whole frame (3589 labels an interval on the Crosscall).
        cglib::mat4x4<double> viewProjMatrix = cglib::mat4x4<double>::identity();
        cglib::mat4x4<double> invViewProjMatrix = cglib::mat4x4<double>::identity();
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
            viewProjMatrix = projectionMatrix * cameraMatrix;
            invViewProjMatrix = cglib::inverse(viewProjMatrix);
            frustum = cglib::gl_projection_frustum(viewProjMatrix);
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
