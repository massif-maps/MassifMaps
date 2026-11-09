/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_DISTANCETILELOD_H_
#define _MASSIF_DISTANCETILELOD_H_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace massif {

    /**
     * The tile cut of every planar layer (02-tiles.md, "The zoom follows the distance"). A tile splits while it spans
     * more than a pixel budget on screen as seen from its NEAREST point, so the level falls with the distance whatever
     * way the camera looks. Every tile carries the budget scale it stops splitting at (`critical`),
     * and the scale that fits the tile budget is read off the finished tree: a pure function of the view, with no
     * state carried between culls.
     */
    struct DistanceTileLOD {
        struct Node {
            double critical; // splits while the scale is under this; 0 never splits, infinity always does
            double parentCritical; // the parent's: this node exists while the scale is under it (infinity for a root)
            int bucket; // 0 = visible, which is what the budget counts
        };

        static constexpr double MAX_SCALE = 32.0; // five levels coarser than the rule
        static constexpr std::size_t MAX_NODES = 16384; // a runaway tree is cut short, not walked
        static constexpr std::size_t DEFAULT_BUDGET = 112;
        static constexpr std::size_t MIN_BUDGET = 24;

        static constexpr int NO_VIEW_CAP = 1000; // Limits::capOffset of a cut no zoom of the view caps

        /** How the pixels a tile may span grow where the ground is seen at a grazing angle: what a far tile gives up in level. */
        enum class FarRule {
            Slant, // from the first degree of tilt, to View::farCoarsening of the slant distance per vertical one (the area rule's fit)
            Distance // mapbox-gl-js: none within 45 degrees of vertical, then a bounded stretch; the level follows the distance
        };

        static constexpr double FAR_COARSENING = 0.65; // fitted with MIN_GRAZING to the area rule's tile count (02-tiles.md), not derived
        static constexpr double MIN_GRAZING = 1.0 / 32.0; // vertical over slant distance, under which a tile reads as edge on
        static constexpr double GRAZING_SIN = 0.707; // mapbox-gl-js distToSplitScale: no stretch while the view ray is within 45 degrees of vertical
        static constexpr double GRAZING_STRETCH = 1.1; // and past it, every further vertical distance of slant may be this much longer
        static constexpr double MAX_GRAZING_SLANT = 64.0; // slant over vertical distance, bounded for a tile whose box holds the eye's height
        static constexpr double MAX_GRAZING_ALLOWANCE = 32.0; // five levels coarser at the horizon
        static constexpr double FLAT_TILT_TOLERANCE = 1.0; // degrees off straight down that still count as a 2D view

        /** What one cull measures a tile with. */
        struct View {
            double focalPixels = 0; // pixels one world unit spans at distance 1: projection(1,1) * half height
            double maxEdgePixels = 0; // what a tile may span; 0 splits to the cap
            double zoom0Distance = 0; // the eye-to-focus distance at zoom 0 (ViewState::getZoom0Distance)
            double zoomShift = 0; // from the camera's zoom to a tile level: Options::ZoomOffset + the layer's bias
            FarRule farRule = FarRule::Slant; // how a tile seen at a grazing angle may span more pixels (farAllowance)
            double farCoarsening = 0; // FarRule::Slant: the exponent of the extra pixels a tile may span, 0 for none
        };

        /**
         * The levels a tile is held to, whatever the rule says. The view level of a tile is the zoom a top-down
         * camera at its nearest distance would be at: no camera zoom enters, which under a terrain or free roam
         * camera follows wherever the view ray meets the ground.
         */
        struct Limits {
            int minZoom = 0; // the layer's own: coarser always splits
            int maxZoom = 0; // the data's, plus overzoom: never split from here on
            int capOffset = 0; // levels over the view level a tile may go, NO_VIEW_CAP for any
            int coarsening = -1; // levels under the view level a tile may be coarser, negative for any (terrain occluders)
            int floorCap = std::numeric_limits<int>::max(); // the coarsening floor never above this level
            int zoomCap = std::numeric_limits<int>::max(); // never split from here on: the level of a look-independent reference distance
        };

        /** The scale a tile stops splitting at: its edge on screen, over the pixels a tile may span. */
        static double criticalScale(double tileEdge, double nearestDistance, double focalPixels, double maxEdgePixels) {
            if (!(maxEdgePixels > 0) || !(nearestDistance > 0)) {
                return std::numeric_limits<double>::infinity();
            }
            return tileEdge * focalPixels / (nearestDistance * maxEdgePixels);
        }

        /**
         * The zoom a top-down camera `distance` from the ground would be at: the camera's own zoom at its focus
         * distance, falling one per doubling. A tile's view zoom is the one to match its style's zoom gates at.
         */
        static double viewZoom(const View& view, double distance) {
            if (!(distance > 0) || !(view.zoom0Distance > 0)) {
                return std::numeric_limits<double>::infinity();
            }
            return std::log2(view.zoom0Distance / distance) + view.zoomShift;
        }

        /** The zoom a tile is styled at: its view zoom, under the level its cap holds the cut at. */
        static double styleZoom(const View& view, const Limits& limits, double distance) {
            return std::min(viewZoom(view, distance), limits.zoomCap + 0.999);
        }

        /** A view looking straight down, `tilt` as ViewState has it (90 is down). */
        static bool isFlat(double tilt) {
            return tilt >= 90.0 - FLAT_TILT_TOLERANCE;
        }

        /** The camera distance every tile of a flat view is styled at, 0 where the per-tile one stands: tilted, or ground that is drawn in 3D. */
        static double flatStyleDistance(double tilt, bool terrainRendered, double cameraDistance) {
            return (!terrainRendered && isFlat(tilt)) ? cameraDistance : 0.0;
        }

        /** The distance a tile's style zoom reads: the camera's own when `flatDistance` is set, so a flat view styles every tile alike. */
        static double styleDistance(double tileDistance, double flatDistance) {
            return flatDistance > 0 ? flatDistance : tileDistance;
        }

        /** The whole level of the view zoom, bounded so that an eye inside the tile still has one. */
        static int viewLevel(const View& view, double distance) {
            double zoom = viewZoom(view, distance);
            return std::isnan(zoom) ? 0 : static_cast<int>(std::max(-NO_VIEW_CAP * 1.0, std::min(NO_VIEW_CAP * 1.0, std::floor(zoom))));
        }

        /** The allowance of FarRule::Slant: the slant distance per vertical one, to the exponent. 1 straight below the eye. */
        static double slantAllowance(double exponent, double distance, double verticalDistance) {
            double slant = distance / std::max(verticalDistance, distance * MIN_GRAZING);
            return std::pow(std::max(1.0, slant), exponent);
        }

        /**
         * The allowance of FarRule::Distance, mapbox-gl-js's `distToSplitScale` inverted. 1 while the ray is within
         * 45 degrees of vertical, so a tile at a given distance has the same level at any tilt up to there; past it
         * the slant distance a level covers stretches geometrically, which bounds the tiles a horizon asks for.
         */
        static double distanceAllowance(double distance, double verticalDistance) {
            const double acute = 1.0 / GRAZING_SIN;
            double slant = distance / std::max(verticalDistance, distance / MAX_GRAZING_SLANT);
            if (slant <= acute) {
                return 1.0;
            }
            double reach = acute + (std::pow(GRAZING_STRETCH, slant - acute + 1.0) - 1.0) / (GRAZING_STRETCH - 1.0) - 1.0;
            return std::min(reach / slant, MAX_GRAZING_ALLOWANCE);
        }

        /** The factor on the pixels a tile may span, a function of the tile and the eye alone. */
        static double farAllowance(const View& view, double distance, double verticalDistance) {
            if (!(distance > 0)) {
                return 1.0;
            }
            if (view.farRule == FarRule::Distance) {
                return distanceAllowance(distance, verticalDistance);
            }
            return view.farCoarsening > 0 ? slantAllowance(view.farCoarsening, distance, verticalDistance) : 1.0;
        }

        /**
         * The scale a tile of `zoom` and `tileEdge` world units stops splitting at, `distance` from the eye at its
         * nearest point and `verticalDistance` from it along the vertical.
         */
        static double nodeCritical(const View& view, const Limits& limits, int zoom, double tileEdge, double distance, double verticalDistance) {
            if (zoom < limits.minZoom) {
                return std::numeric_limits<double>::infinity();
            }
            int level = viewLevel(view, distance);
            int cap = limits.capOffset >= NO_VIEW_CAP ? limits.maxZoom : std::min(limits.maxZoom, level + limits.capOffset);
            cap = std::min(cap, limits.zoomCap);
            if (zoom >= cap) {
                return 0.0;
            }
            if (limits.coarsening >= 0 && zoom < std::min(level - limits.coarsening, limits.floorCap)) {
                return std::numeric_limits<double>::infinity();
            }
            return criticalScale(tileEdge, distance, view.focalPixels, view.maxEdgePixels * farAllowance(view, distance, verticalDistance));
        }

        static bool isLeaf(const Node& node, double scale) {
            return node.critical <= scale && scale < node.parentCritical;
        }

        static std::size_t countVisible(const std::vector<Node>& nodes, double scale) {
            std::size_t count = 0;
            for (const Node& node : nodes) {
                count += (node.bucket == 0 && isLeaf(node, scale)) ? 1 : 0;
            }
            return count;
        }

        /** The finest scale (1 = the rule itself) whose cut keeps at most `budget` visible tiles. */
        static double fitScale(const std::vector<Node>& nodes, std::size_t budget) {
            if (countVisible(nodes, 1.0) <= budget) {
                return 1.0;
            }
            // The count only changes where some tile stops splitting.
            std::vector<double> scales;
            for (const Node& node : nodes) {
                if (node.critical > 1.0 && node.critical <= MAX_SCALE) {
                    scales.push_back(node.critical);
                }
            }
            std::sort(scales.begin(), scales.end());
            scales.erase(std::unique(scales.begin(), scales.end()), scales.end());
            std::size_t low = 0, high = scales.size();
            while (low < high) {
                std::size_t middle = (low + high) / 2;
                if (countVisible(nodes, scales[middle]) <= budget) {
                    high = middle;
                } else {
                    low = middle + 1;
                }
            }
            return low < scales.size() ? scales[low] : MAX_SCALE;
        }

        /**
         * How many visible tiles the drape cover may take so that it fits the drape cache, which holds colour drapes
         * of `resolution` texels in `cacheMegabytes`, at most `entryCap` of them. Under a third of them stay free for
         * the stand-ins and the generation behind the cover.
         */
        static std::size_t budget(int resolution, int cacheMegabytes, std::size_t entryCap) {
            if (resolution <= 0 || cacheMegabytes <= 0) {
                return DEFAULT_BUDGET;
            }
            std::size_t texels = static_cast<std::size_t>(std::min(2048, std::max(128, resolution)));
            std::size_t bytesPerEntry = texels * texels * 4;
            std::size_t entries = std::min(entryCap, static_cast<std::size_t>(cacheMegabytes) * 1024 * 1024 / bytesPerEntry);
            return std::max(MIN_BUDGET, entries * 7 / 10);
        }
    };

}

#endif
