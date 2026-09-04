/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_SPANGEOMETRY_H_
#define _MASSIF_VT_SPANGEOMETRY_H_

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <cglib/vec.h>
#include <cglib/mat.h>

namespace massif::vt {
    /**
     * The pure geometry behind LineElevationMode::SPAN - a bridge deck or a tunnel bore laid
     * straight between its two portals instead of following the ground.
     *
     * Header-only and free of any renderer state so it can be tested on the host: every one of
     * these is silent when wrong (a deck sags, a label sits on the ground) rather than failing.
     */
    struct SpanGeometry final {
        /** How far inside the tile an end must be to count as the feature's own, in tile units. */
        static constexpr float TILE_CLIP_MARGIN = 0.002f;
        /** The narrowest a deck is ever matched at, in normalized world units (25 m). */
        static constexpr double MIN_MATCH_WIDTH = 25.0 / 40075017.0;
        /** ...growing with the span, because a long deck CURVES away from its own straight chord. */
        static constexpr double MATCH_CURVE_FRACTION = 0.02;
        /** cos of the angle two pieces may differ by and still be one structure (~25 degrees). */
        static constexpr double MIN_PARALLEL = 0.9;
        /** How much of its own pieces a chord must reach across to count as the whole structure. */
        static constexpr double MIN_CHORD_SPAN = 0.95;

        /**
         * Whether an end is the FEATURE's own or just where the tile cut it. Tested against the
         * tile, not the clip box: the source clips at its own buffer (mapbox: 1/64), so every cut
         * end lands well inside our 1/8 box and would read as a portal. The same point is inside
         * the NEIGHBOURING tile's copy, which is where its portal is seen.
         */
        static bool isPortal(const cglib::vec2<float>& p, float margin = TILE_CLIP_MARGIN) {
            return p(0) > margin && p(0) < 1.0f - margin
                && p(1) > margin && p(1) < 1.0f - margin;
        }

        /** Where a point falls along the chord, clamped to it: 0 at one portal, 1 at the other. */
        static double chordParam(const cglib::vec2<double>& pos, const cglib::vec2<double>& portal0, const cglib::vec2<double>& portal1) {
            cglib::vec2<double> chord = portal1 - portal0;
            double length2 = cglib::dot_product(chord, chord);
            if (length2 <= 0) {
                return 0;
            }
            return std::max(0.0, std::min(1.0, cglib::dot_product(pos - portal0, chord) / length2));
        }

        /** The deck height at that point - the whole purpose: straight, whatever the DEM does. */
        static double chordHeight(double height0, double height1, double t) {
            return height0 + (height1 - height0) * t;
        }

        /**
         * How far off the chord something may sit and still belong to it. A fixed radius is wrong:
         * Millau's deck curves on a ~20 km radius, putting its middle some 36 m off its own chord,
         * so a 25 m test missed exactly the labels that stand on the bridge.
         */
        static double matchAllowance(double chordLength) {
            return std::max(MIN_MATCH_WIDTH, chordLength * MATCH_CURVE_FRACTION);
        }

        /** Whether a point stands on the chord - within the allowance, and between the portals. */
        static bool isOnChord(const cglib::vec2<double>& pos, const cglib::vec2<double>& portal0, const cglib::vec2<double>& portal1) {
            cglib::vec2<double> chord = portal1 - portal0;
            double length2 = cglib::dot_product(chord, chord);
            if (length2 <= 0) {
                return false;
            }
            double t = cglib::dot_product(pos - portal0, chord) / length2;
            if (t < 0 || t > 1) {
                return false; // past an abutment: back on the ground
            }
            double allowance = matchAllowance(std::sqrt(length2));
            return cglib::norm(pos - (portal0 + chord * t)) <= allowance * allowance;
        }

        /**
         * The two vertices of a filled ring FARTHEST APART. A bed has no two ends, so its span is
         * its longest axis, which for a deck-shaped ring is exactly where it meets the ground.
         * Two passes - farthest from the centroid, then farthest from that - which is exact for a
         * long thin ring and never worse than the true diameter by more than its width.
         */
        static std::pair<cglib::vec2<float>, cglib::vec2<float>> farthestPair(const std::vector<cglib::vec2<float>>& ring) {
            if (ring.empty()) {
                return std::pair<cglib::vec2<float>, cglib::vec2<float>>();
            }
            cglib::vec2<float> centroid(0, 0);
            for (const cglib::vec2<float>& v : ring) {
                centroid = centroid + v * (1.0f / ring.size());
            }
            auto farthestFrom = [&ring](const cglib::vec2<float>& from) {
                const cglib::vec2<float>* best = &ring.front();
                float bestDist = -1;
                for (const cglib::vec2<float>& v : ring) {
                    float dist = cglib::norm(v - from);
                    if (dist > bestDist) {
                        bestDist = dist;
                        best = &v;
                    }
                }
                return *best;
            };
            cglib::vec2<float> p0 = farthestFrom(centroid);
            return std::make_pair(p0, farthestFrom(p0));
        }

        /**
         * Whether a chord actually spans the pieces it was collected from. Two portals found on the
         * SAME abutment - one structure's end seen in two neighbouring tiles - give a chord of a few
         * tens of metres over a kilometre of deck, and it passes every other test here. Both lengths
         * are SQUARED, as cglib::norm returns them.
         */
        static bool chordSpansGroup(double chordLength2, double groupDiameter2) {
            return chordLength2 >= groupDiameter2 * (MIN_CHORD_SPAN * MIN_CHORD_SPAN);
        }

        /**
         * Whether two pieces are the same structure. Only an end the tile CUT can continue into
         * another piece - a real portal ends the run - and the source's buffer makes neighbouring
         * copies overlap rather than touch, so this is proximity, not equality. The direction test
         * keeps a crossing structure out of the chain.
         */
        static bool piecesMeet(const cglib::vec2<double>& a0, const cglib::vec2<double>& a1, bool aPortal0, bool aPortal1,
                               const cglib::vec2<double>& b0, const cglib::vec2<double>& b1, bool bPortal0, bool bPortal1,
                               double tolerance2) {
            cglib::vec2<double> da = a1 - a0, db = b1 - b0;
            double lengthA2 = cglib::dot_product(da, da), lengthB2 = cglib::dot_product(db, db);
            if (lengthA2 <= 0 || lengthB2 <= 0) {
                return false;
            }
            double parallel = cglib::dot_product(da, db) / std::sqrt(lengthA2 * lengthB2);
            if (std::abs(parallel) < MIN_PARALLEL) {
                return false;
            }
            return (!aPortal0 && ((!bPortal0 && cglib::norm(a0 - b0) < tolerance2) || (!bPortal1 && cglib::norm(a0 - b1) < tolerance2)))
                || (!aPortal1 && ((!bPortal0 && cglib::norm(a1 - b0) < tolerance2) || (!bPortal1 && cglib::norm(a1 - b1) < tolerance2)));
        }

        /** How far past a cut end the point that names the next tile is placed, as a fraction of the tile. */
        static constexpr double CUT_STEP_FRACTION = 0.05;

        /**
         * A point just past the cut end of a piece, along the piece: the source's buffer puts the
         * cut itself INSIDE the neighbouring copy's overlap, so the end alone can name the wrong
         * tile. A twentieth of a tile at the piece's zoom clears any buffer a source uses.
         */
        static cglib::vec2<double> beyondCutEnd(const cglib::vec2<double>& end, const cglib::vec2<double>& other, int zoom) {
            cglib::vec2<double> dir = end - other;
            double length = std::sqrt(cglib::dot_product(dir, dir));
            if (!(length > 0)) {
                return end;
            }
            return end + dir * (CUT_STEP_FRACTION / (1 << zoom) / length);
        }

        /**
         * Drape bounds (u0, v0, u1, v1) grown by a margin on every side and clamped to the tile:
         * the deck has a width its two ends do not carry, and a line its stroke.
         */
        static cglib::vec4<float> expandBounds(const cglib::vec4<float>& bounds, float margin) {
            return cglib::vec4<float>(std::max(0.0f, bounds(0) - margin), std::max(0.0f, bounds(1) - margin),
                                      std::min(1.0f, bounds(2) + margin), std::min(1.0f, bounds(3) + margin));
        }

        /**
         * The sampling transform (offset.xy, scale.zw: uv' = uv * scale + offset) of a span drape
         * that was baked over `bounds` of its tile only: what mapped into the tile now maps into
         * the bounds' share of it.
         */
        static cglib::vec4<float> drapeTransformInBounds(const cglib::vec4<float>& transform, const cglib::vec4<float>& bounds) {
            float w = std::max(1.0e-6f, bounds(2) - bounds(0));
            float h = std::max(1.0e-6f, bounds(3) - bounds(1));
            return cglib::vec4<float>((transform(0) - bounds(0)) / w, (transform(1) - bounds(1)) / h, transform(2) / w, transform(3) / h);
        }

        /**
         * The clip-space zoom that puts `bounds` of a tile (uv, y up as the texture's) onto the
         * whole [-1, 1] square, for a bake that covers the bounds alone.
         */
        static cglib::mat4x4<float> clipZoomToBounds(const cglib::vec4<float>& bounds) {
            float w = std::max(1.0e-6f, bounds(2) - bounds(0));
            float h = std::max(1.0e-6f, bounds(3) - bounds(1));
            cglib::mat4x4<float> zoom = cglib::mat4x4<float>::identity();
            zoom(0, 0) = 1.0f / w;
            zoom(1, 1) = 1.0f / h;
            zoom(0, 3) = -(bounds(0) + bounds(2) - 1.0f) / w; // the bounds' centre, in clip units, to 0
            zoom(1, 3) = -(bounds(1) + bounds(3) - 1.0f) / h;
            return zoom;
        }
    };
}

#endif
