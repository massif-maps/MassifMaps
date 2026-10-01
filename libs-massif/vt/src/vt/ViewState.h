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
        // Added to `zoom` where a style reads it (view::zoom): Map::Settings::zoomShift of the layer drawn.
        float styleZoomShift = 0;
        float rotation = 0;
        float tilt = 0;
        float aspect = 1;
        float resolution = 0;
        // The viewport's real height in device pixels, 0 = not set; `resolution` is the normalized
        // screen, so screen-space objects sized off it alone scale with the device. See Label::calculateLabelScale.
        float deviceResolution = 0;
        float zoomScale = 1;
        // Camera to focus point, internal units; 0 = not set, and label scaling falls back to where the
        // view axis meets z=0, which is only right for a focus on the ground.
        float focusDistance = 0;
        // Metres from the camera to the label (view::distance). Set only in the culler's per-label ranking
        // pass; the renderer evaluates once per batch.
        float labelDistance = 0;
        // How far a label may be placed, in multiples of focusDistance; 0 = no limit. Must match the
        // culler's: Label::updatePlacement applies its own copy first, so raising only the culler's does nothing.
        float labelViewDistance = LabelDistance::DEFAULT_VIEW_DISTANCE;

        // mapbox's ["measure-light", "brightness"], 0-1: `view::brightness`, resolved per frame without a re-decode.
        float lightBrightness = 1.0f;
        // How much of the perspective divide a label keeps, 0-1: 0 = constant screen size, 0.5 = maplibre's
        // 0.5 + 0.5 * distance_ratio (half the projection's shrink).
        float labelPerspectiveScaling = 0.0f;
        bool planarProjection = false;
        cglib::mat4x4<double> projectionMatrix = cglib::mat4x4<double>::identity();
        cglib::mat4x4<double> cameraMatrix = cglib::mat4x4<double>::identity();
        // Cached once per frame: Label::setupCoordinateSystem needs both for every point label.
        cglib::mat4x4<double> viewProjMatrix = cglib::mat4x4<double>::identity();
        cglib::mat4x4<double> invViewProjMatrix = cglib::mat4x4<double>::identity();
        cglib::vec3<double> origin = cglib::vec3<double>::zero();
        cglib::frustum3<double> frustum = cglib::gl_projection_frustum(cglib::mat4x4<double>::identity());
        // The frustum grown by labelPadding on every side, so a label is fully faded in by the time it scrolls in.
        cglib::frustum3<double> labelFrustum = cglib::gl_projection_frustum(cglib::mat4x4<double>::identity());
        // Screen pixels. maplibre pads a flat 100 (viewportPadding); ours scales by sin(tilt), since near
        // the horizon 100 pixels are kilometres of map and thousands of labels.
        float labelPadding = 0;
        std::array<cglib::vec3<float>, 3> orientation = { { cglib::vec3<float>(1, 0, 0), cglib::vec3<float>(0, 1, 0), cglib::vec3<float>(0, 0, 1) } };

        // maplibre's viewportPadding at top-down; a non-zero floor so a label still has room before the edge.
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

        // Tilt 90 is straight down, so sin(tilt) is the ground's foreshortening.
        // `paddingOverride` >= 0 (Options::getLabelPadding) replaces the rule.
        static float calculateLabelPadding(float tilt, float paddingOverride = -1.0f) {
            if (paddingOverride >= 0.0f) {
                return paddingOverride;
            }
            float scale = std::sin(std::max(0.0f, tilt) * 3.14159265358979f / 180.0f);
            return std::max(MIN_LABEL_PADDING, MAX_LABEL_PADDING * scale);
        }

        // The label frustum is derived from the padding, so both are set together.
        void setLabelPadding(float padding) {
            labelPadding = std::max(0.0f, padding);
            labelFrustum = cglib::gl_projection_frustum(paddedProjectionMatrix() * cameraMatrix);
        }

        // Shrinks clip x/y by viewport / (viewport + 2 * padding). Static: the tile culler needs the same band.
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
