/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_SPANGEOMETRY_H_
#define _MASSIF_VT_SPANGEOMETRY_H_

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

#include <cglib/vec.h>
#include <cglib/mat.h>

namespace massif::vt {
    /**
     * Geometry behind LineElevationMode::SPAN: a deck or bore laid straight between its two portals.
     * Free of renderer state so the host tests cover it; every function here fails silently when wrong.
     */
    struct SpanGeometry final {
        /** How far inside the tile an end must be to count as the feature's own, in tile units. */
        static constexpr float TILE_CLIP_MARGIN = 0.002f;
        /** The narrowest a deck is ever matched at, in normalized world units (25 m). */
        static constexpr double MIN_MATCH_WIDTH = 25.0 / 40075017.0;
        /** ...growing with the span, because a long deck curves away from its own straight chord. */
        static constexpr double MATCH_CURVE_FRACTION = 0.02;
        /** cos of the angle two pieces may differ by and still be one structure (~25 degrees). */
        static constexpr double MIN_PARALLEL = 0.9;
        /** How much of its own pieces a chord must reach across to count as the whole structure. */
        static constexpr double MIN_CHORD_SPAN = 0.95;
        /**
         * How far off the other piece's line a cut end may sit and still continue it, in normalized world
         * units (25 m). The tile-scaled meet radius alone chains parallel neighbouring bridges into one group.
         */
        static constexpr double MEET_LATERAL_TOLERANCE = 25.0 / 40075017.0;
        /**
         * cos of the angle a deck polygon's chord may make with the road it adopts (~45 degrees): a short
         * wide deck's corner-to-corner chord runs diagonally, past MIN_PARALLEL; a road crossing under stays out.
         */
        static constexpr double ADOPT_MIN_PARALLEL = 0.7;

        /**
         * Whether an end is the feature's own or just where the tile cut it. Tested against the tile, not
         * the clip box: sources clip at their own buffer (mapbox 1/64), well inside our 1/8 box.
         */
        static bool isPortal(const cglib::vec2<float>& p, float margin = TILE_CLIP_MARGIN) {
            return p(0) > margin && p(0) < 1.0f - margin
                && p(1) > margin && p(1) < 1.0f - margin;
        }

        /**
         * Where a point falls along the chord, unclamped: 0 at one portal, 1 at the other. The shader cuts
         * the deck at the portals by this, since a skewed deck ring reaches past its road's portal.
         */
        static double chordParamRaw(const cglib::vec2<double>& pos, const cglib::vec2<double>& portal0, const cglib::vec2<double>& portal1) {
            cglib::vec2<double> chord = portal1 - portal0;
            double length2 = cglib::dot_product(chord, chord);
            if (length2 <= 0) {
                return 0;
            }
            return cglib::dot_product(pos - portal0, chord) / length2;
        }

        /** The same, clamped to the chord - the height past a portal is the portal's. */
        static double chordParam(const cglib::vec2<double>& pos, const cglib::vec2<double>& portal0, const cglib::vec2<double>& portal1) {
            return std::max(0.0, std::min(1.0, chordParamRaw(pos, portal0, portal1)));
        }

        /** The deck height at that point: straight, whatever the DEM does. */
        static double chordHeight(double height0, double height1, double t) {
            return height0 + (height1 - height0) * t;
        }

        /** How far off the chord something may sit and still belong to it; grows with length since long decks curve. */
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
         * The two vertices of a filled ring farthest apart: a deck-shaped ring's span is its longest axis.
         * Two passes (from the centroid, then from that); exact for a long thin ring, off by at most its width.
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

        /** How much of a ring's length, from either end, counts as that end (two passes). */
        static constexpr float END_FRACTION_COARSE = 0.35f;
        static constexpr float END_FRACTION = 0.15f;

        /**
         * The centres of a ring's two ends (vertex mean within END_FRACTION along the long axis): a corner
         * chord ends beside the road, the end centres on it. Two passes, as the first axis is the diagonal.
         */
        static std::pair<cglib::vec2<float>, cglib::vec2<float>> endCentres(const std::vector<cglib::vec2<float>>& ring) {
            std::pair<cglib::vec2<float>, cglib::vec2<float>> ends = farthestPair(ring);
            for (float fraction : { END_FRACTION_COARSE, END_FRACTION }) {
                cglib::vec2<float> axis = ends.second - ends.first;
                float length2 = cglib::dot_product(axis, axis);
                if (length2 <= 0) {
                    return ends;
                }
                cglib::vec2<float> sum0(0, 0), sum1(0, 0);
                int count0 = 0, count1 = 0;
                for (const cglib::vec2<float>& v : ring) {
                    float t = cglib::dot_product(v - ends.first, axis) / length2;
                    if (t <= fraction) {
                        sum0 = sum0 + v;
                        count0++;
                    } else if (t >= 1.0f - fraction) {
                        sum1 = sum1 + v;
                        count1++;
                    }
                }
                if (count0 > 0) {
                    ends.first = sum0 * (1.0f / count0);
                }
                if (count1 > 0) {
                    ends.second = sum1 * (1.0f / count1);
                }
            }
            return ends;
        }

        /**
         * Whether a chord spans the pieces it was collected from, rejecting two portals found on the same
         * abutment in neighbouring tiles. Both lengths are squared, as cglib::norm returns them.
         */
        static bool chordSpansGroup(double chordLength2, double groupDiameter2) {
            return chordLength2 >= groupDiameter2 * (MIN_CHORD_SPAN * MIN_CHORD_SPAN);
        }

        /**
         * Whether a chord lies on another (both portals on it): the same deck clipped differently by two
         * source tiles, which must resolve to one chord - the longer - or it steps where the source changes.
         */
        static bool chordLiesOn(const cglib::vec2<double>& p0, const cglib::vec2<double>& p1, const cglib::vec2<double>& portal0, const cglib::vec2<double>& portal1) {
            return isOnChord(p0, portal0, portal1) && isOnChord(p1, portal0, portal1);
        }

        /**
         * How much of a chord runs along another, as a fraction of the shorter, or 0 when not the same road
         * (parallel within ADOPT_MIN_PARALLEL, midpoint within the allowance, overlap at least half the shorter).
         */
        static double chordOverlap(const cglib::vec2<double>& a0, const cglib::vec2<double>& a1, const cglib::vec2<double>& b0, const cglib::vec2<double>& b1) {
            cglib::vec2<double> da = a1 - a0, db = b1 - b0;
            double lengthA2 = cglib::dot_product(da, da), lengthB2 = cglib::dot_product(db, db);
            if (lengthA2 <= 0 || lengthB2 <= 0) {
                return 0;
            }
            if (std::abs(cglib::dot_product(da, db)) < ADOPT_MIN_PARALLEL * std::sqrt(lengthA2 * lengthB2)) {
                return 0;
            }
            cglib::vec2<double> mid = (a0 + a1) * 0.5;
            double tm = cglib::dot_product(mid - b0, db) / lengthB2;
            double allowance = matchAllowance(std::sqrt(lengthB2));
            if (cglib::norm(mid - (b0 + db * tm)) > allowance * allowance) {
                return 0;
            }
            double t0 = cglib::dot_product(a0 - b0, db) / lengthB2;
            double t1 = cglib::dot_product(a1 - b0, db) / lengthB2;
            double overlap = (std::min(std::max(t0, t1), 1.0) - std::max(std::min(t0, t1), 0.0)) * std::sqrt(lengthB2);
            double shorter = std::sqrt(std::min(lengthA2, lengthB2));
            return overlap >= 0.5 * shorter ? overlap / shorter : 0;
        }

        /** Whether a point is one of the chord's two portals, within the match allowance. */
        static bool isChordEnd(const cglib::vec2<double>& pos, const cglib::vec2<double>& portal0, const cglib::vec2<double>& portal1) {
            double allowance = matchAllowance(cglib::length(portal1 - portal0));
            return cglib::norm(pos - portal0) <= allowance * allowance || cglib::norm(pos - portal1) <= allowance * allowance;
        }

        /**
         * The cached chord a stranded piece borrows, or `end`; independent of cache order. A piece with a
         * portal of its own takes the shortest chord ending there (its own feature's); one cut at both
         * ends takes the longest (the whole structure's). See docs/internals/rendering/17-bridges.md.
         */
        template <typename It, typename Get>
        static It borrowChord(const cglib::vec2<double>& e0, bool portal0, const cglib::vec2<double>& e1, bool portal1, It begin, It end, Get get) {
            cglib::vec2<double> middle = (e0 + e1) * 0.5;
            It best = end;
            bool bestOwn = false;
            double bestLength2 = 0;
            for (It it = begin; it != end; it++) {
                const auto& chord = get(it);
                // ...or overlapping it by half: a deck cut by a small tile can have its midpoint past the road chord's end.
                if (!isOnChord(middle, chord.portal0, chord.portal1) && chordOverlap(e0, e1, chord.portal0, chord.portal1) <= 0) {
                    continue;
                }
                bool own = (portal0 && isChordEnd(e0, chord.portal0, chord.portal1)) || (portal1 && isChordEnd(e1, chord.portal0, chord.portal1));
                double length2 = cglib::norm(chord.portal1 - chord.portal0);
                bool better = own ? (length2 < bestLength2) : (length2 > bestLength2);
                if (best == end || (own && !bestOwn) || (own == bestOwn && better)) {
                    best = it;
                    bestOwn = own;
                    bestLength2 = length2;
                }
            }
            return best;
        }

        /** The same over a plain range of chords, `*it` being the chord. */
        template <typename It>
        static It borrowChord(const cglib::vec2<double>& e0, bool portal0, const cglib::vec2<double>& e1, bool portal1, It begin, It end) {
            return borrowChord(e0, portal0, e1, portal1, begin, end, [](It it) -> const auto& { return *it; });
        }

        /**
         * Whether two pieces are the same structure: only cut ends continue, by proximity since source
         * buffers make copies overlap; the direction test keeps a crossing structure out of the chain.
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
            // Squared distance of a point from the infinite line through the other piece.
            auto offLine2 = [](const cglib::vec2<double>& p, const cglib::vec2<double>& l0, const cglib::vec2<double>& dl, double length2) {
                double t = cglib::dot_product(p - l0, dl) / length2;
                return cglib::norm(p - (l0 + dl * t));
            };
            constexpr double lateral2 = MEET_LATERAL_TOLERANCE * MEET_LATERAL_TOLERANCE;
            auto endsMeet = [&](const cglib::vec2<double>& a, const cglib::vec2<double>& b) {
                return cglib::norm(a - b) < tolerance2 && offLine2(a, b0, db, lengthB2) <= lateral2 && offLine2(b, a0, da, lengthA2) <= lateral2;
            };
            return (!aPortal0 && ((!bPortal0 && endsMeet(a0, b0)) || (!bPortal1 && endsMeet(a0, b1))))
                || (!aPortal1 && ((!bPortal0 && endsMeet(a1, b0)) || (!bPortal1 && endsMeet(a1, b1))));
        }

        /** How far past a cut end the point that names the next tile is placed, as a fraction of the tile. */
        static constexpr double CUT_STEP_FRACTION = 0.05;

        /**
         * A point just past a piece's cut end: the source buffer puts the cut itself inside the neighbour's
         * overlap, so the end alone can name the wrong tile.
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

        /** The sampling transform (uv' = uv * scale.zw + offset.xy) of a span drape baked over `bounds` of its tile only. */
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
