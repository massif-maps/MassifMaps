/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_LABELDISTANCE_H_
#define _MASSIF_VT_LABELDISTANCE_H_

namespace massif::vt {

    /**
     * How far a label may be from the camera before it is not worth placing: glyphs are screen-space,
     * so a pitched view would fill its horizon with full-size labels. Labels only, never geometry.
     * Ported from maplibre `perspectiveRatioCutoff` / mapbox `minPerspectiveRatio` (symbol/collision_index.ts).
     */
    struct LabelDistance {
        /**
         * mapbox's ratio: 1 at the map centre, falling toward 0.5 with distance. Relative to the
         * camera-to-centre distance, not metres, so one cutoff holds at every zoom.
         */
        static float perspectiveRatio(double cameraToCenter, double distance) {
            if (!(distance > 0) || !(cameraToCenter > 0)) {
                return 1.0f; // no view to measure against: place it
            }
            return static_cast<float>(0.5 + 0.5 * (cameraToCenter / distance));
        }

        /** maplibre's 0.6 (cut past 5x the camera-to-centre distance), the tighter of maplibre and mapbox (0.55, 10x). */
        static constexpr float PERSPECTIVE_RATIO_CUTOFF = 0.6f;

        /** PERSPECTIVE_RATIO_CUTOFF in multiples of the camera-to-centre distance. */
        static constexpr double DEFAULT_VIEW_DISTANCE = 5.0;

        /** Whether a label is past `viewDistance` multiples of the camera-to-centre distance; 0 = never. */
        static bool isTooFar(double cameraToCenter, double distance, double viewDistance) {
            if (!(viewDistance > 0) || !(cameraToCenter > 0)) {
                return false;
            }
            return distance > cameraToCenter * viewDistance;
        }

        /** The distance the cutoff corresponds to, in the units of cameraToCenter. */
        static double cutoffDistance(double cameraToCenter, float cutoff) {
            if (!(cutoff > 0.5f)) {
                return 0; // 0.5 is the limit of the ratio: nothing is ever cut
            }
            return cameraToCenter * 0.5 / (cutoff - 0.5);
        }
    };

}

#endif
