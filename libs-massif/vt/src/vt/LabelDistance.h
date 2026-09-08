/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_LABELDISTANCE_H_
#define _MASSIF_VT_LABELDISTANCE_H_

namespace massif::vt {

    /**
     * How far a label may be from the camera before it is not worth placing.
     *
     * A label glyph is SCREEN-SPACE: it does not shrink with distance the way the geometry it names
     * does, so a pitched view fills its horizon band with full-size labels for features kilometres
     * away. Geometry needs no such cut and does not get one - mapbox and maplibre both apply this
     * inside the symbol collision index alone, never to fills, lines or extrusions.
     *
     * Ported from maplibre's `CollisionIndex.perspectiveRatioCutoff` and mapbox's
     * `minPerspectiveRatio` (both `symbol/collision_index.ts`).
     */
    struct LabelDistance {
        /**
         * mapbox's ratio: 1 at the map centre, falling toward 0.5 with distance. Expressed against
         * the CAMERA-TO-CENTRE distance rather than in metres, so one cutoff holds at every zoom -
         * a metre cap would cut a city block at z18 and nothing at all at z10.
         */
        static float perspectiveRatio(double cameraToCenter, double distance) {
            if (!(distance > 0) || !(cameraToCenter > 0)) {
                return 1.0f; // no view to measure against: place it, as before
            }
            return static_cast<float>(0.5 + 0.5 * (cameraToCenter / distance));
        }

        /**
         * maplibre's 0.6, which cuts past 5x the camera-to-centre distance. mapbox uses 0.55 (10x)
         * and explains the choice: at 10x "the label would be drawn at 10% the size of the features
         * around it". The tighter of the two, since our horizon band is what this is for.
         */
        static constexpr float PERSPECTIVE_RATIO_CUTOFF = 0.6f;

        /** The distance the cutoff corresponds to, for a caller that wants to reason in world units. */
        static double cutoffDistance(double cameraToCenter, float cutoff) {
            if (!(cutoff > 0.5f)) {
                return 0; // 0.5 is the limit of the ratio: nothing is ever cut
            }
            return cameraToCenter * 0.5 / (cutoff - 0.5);
        }
    };

}

#endif
