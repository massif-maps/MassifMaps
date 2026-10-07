/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_TERRAINSKIRTS_H_
#define _MASSIF_TERRAINSKIRTS_H_

#include <vt/TileId.h>

#include <cglib/vec.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace massif {

    /**
     * The node heights a tile is drawn with: (nodes + 1)^2 heights in metres over an internal-coordinate box,
     * row 0 at the south. `source` names the data they came from; two tiles on one source meet exactly.
     */
    struct NodeFieldView {
        double minX = 0, minY = 0, maxX = 0, maxY = 0;
        int nodes = 0;
        long long source = -1;
        std::function<double(int, int)> height;

        double sample(double x, double y) const {
            double fx = std::min(std::max((x - minX) / (maxX - minX), 0.0), 1.0) * nodes;
            double fy = std::min(std::max((y - minY) / (maxY - minY), 0.0), 1.0) * nodes;
            int i = std::min(static_cast<int>(fx), nodes - 1), j = std::min(static_cast<int>(fy), nodes - 1);
            double u = fx - i, v = fy - j;
            return (height(i, j) * (1 - u) + height(i + 1, j) * u) * (1 - v) + (height(i, j + 1) * (1 - u) + height(i + 1, j + 1) * u) * v;
        }
    };

    struct TerrainSkirts {
        static constexpr int EDGE_SAMPLES = 65;
        static constexpr double MIN_GAP_METERS = 0.5;
        static constexpr double GAP_MARGIN = 1.25; // the drawn edge chords between nodes the samples land on
        static constexpr double DROP_PAD_METERS = 1.0;

        /**
         * Per cover tile, how far a skirt hangs below its west/east/south/north edge to meet the LOWER neighbour
         * drawn from other data there; 0 where none is needed. The higher side only: the lower one's skirt would
         * hang under its own ground. Tiles without a drawn field get none.
         */
        static std::map<vt::TileId, cglib::vec4<float>> drops(const std::vector<vt::TileId>& cover, double worldSize,
                                                              const std::function<bool(const vt::TileId&, NodeFieldView&)>& fieldOf) {
            std::map<vt::TileId, cglib::vec4<float>> result;
            std::set<vt::TileId> coverSet(cover.begin(), cover.end());
            int maxZoom = 0;
            for (const vt::TileId& tileId : cover) {
                maxZoom = std::max(maxZoom, tileId.zoom);
            }
            std::map<vt::TileId, NodeFieldView> fields;
            auto field = [&](const vt::TileId& tileId) -> const NodeFieldView* {
                auto it = fields.find(tileId);
                if (it == fields.end()) {
                    NodeFieldView view;
                    it = fields.emplace(tileId, fieldOf(tileId, view) && view.nodes > 0 && view.height ? view : NodeFieldView()).first;
                }
                return it->second.nodes > 0 ? &it->second : nullptr;
            };
            auto coverTileAt = [&](double x, double y, vt::TileId& found) {
                for (int zoom = maxZoom; zoom >= 0; zoom--) {
                    double extent = static_cast<double>(1 << zoom);
                    vt::TileId tileId(zoom, static_cast<int>(std::floor((x / worldSize + 0.5) * extent)), static_cast<int>(std::floor((0.5 - y / worldSize) * extent)));
                    if (coverSet.count(tileId) > 0) {
                        found = tileId;
                        return true;
                    }
                }
                return false;
            };

            for (const vt::TileId& tileId : cover) {
                const NodeFieldView* own = field(tileId);
                if (!own) {
                    continue;
                }
                double extent = static_cast<double>(1 << tileId.zoom);
                double x0 = (tileId.x / extent - 0.5) * worldSize, x1 = ((tileId.x + 1) / extent - 0.5) * worldSize;
                double yNorth = (0.5 - tileId.y / extent) * worldSize, ySouth = (0.5 - (tileId.y + 1) / extent) * worldSize;
                double outside = (x1 - x0) * 1.0e-3;
                cglib::vec4<float> tileDrops(0, 0, 0, 0);
                for (int edge = 0; edge < 4; edge++) {
                    double gap = 0;
                    for (int i = 0; i < EDGE_SAMPLES; i++) {
                        double t = (i + 0.5) / EDGE_SAMPLES;
                        double x = (edge == 0 ? x0 : edge == 1 ? x1 : x0 + (x1 - x0) * t);
                        double y = (edge == 2 ? ySouth : edge == 3 ? yNorth : ySouth + (yNorth - ySouth) * t);
                        double ox = (edge == 0 ? -outside : edge == 1 ? outside : 0), oy = (edge == 2 ? -outside : edge == 3 ? outside : 0);
                        vt::TileId neighbourId(0, 0, 0);
                        if (!coverTileAt(x + ox, y + oy, neighbourId)) {
                            continue;
                        }
                        const NodeFieldView* other = field(neighbourId);
                        if (!other || other->source == own->source) {
                            continue;
                        }
                        gap = std::max(gap, own->sample(x, y) - other->sample(x, y));
                    }
                    if (gap > MIN_GAP_METERS) {
                        tileDrops(edge) = static_cast<float>(gap * GAP_MARGIN + DROP_PAD_METERS);
                    }
                }
                if (tileDrops != cglib::vec4<float>(0, 0, 0, 0)) {
                    result.emplace(tileId, tileDrops);
                }
            }
            return result;
        }
    };

}

#endif
