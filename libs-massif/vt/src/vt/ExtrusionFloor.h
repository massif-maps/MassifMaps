/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_EXTRUSIONFLOOR_H_
#define _MASSIF_VT_EXTRUSIONFLOOR_H_

#include <cstddef>
#include <vector>

#include <cglib/vec.h>

namespace massif::vt {

    /**
     * Where the drawn ground under a footprint is read for its floor: every ring vertex, then the centroid of
     * every roof triangle - inside, where a thin wall's crest is - thinned evenly to MAX_POINTS (04-terrain.md).
     */
    struct ExtrusionFloor {
        static constexpr std::size_t MAX_POINTS = 32;

        static std::vector<cglib::vec2<float>> floorPoints(const std::vector<std::vector<cglib::vec2<float>>>& rings, const std::vector<cglib::vec2<float>>& vertices, const std::vector<int>& elements) {
            std::vector<cglib::vec2<float>> points;
            for (const std::vector<cglib::vec2<float>>& ring : rings) {
                points.insert(points.end(), ring.begin(), ring.end());
            }
            for (std::size_t i = 0; i + 2 < elements.size(); i += 3) {
                points.push_back((vertices[elements[i]] + vertices[elements[i + 1]] + vertices[elements[i + 2]]) * (1.0f / 3.0f));
            }
            if (points.size() > MAX_POINTS) {
                std::vector<cglib::vec2<float>> thinned;
                thinned.reserve(MAX_POINTS);
                for (std::size_t k = 0; k < MAX_POINTS; k++) {
                    thinned.push_back(points[k * points.size() / MAX_POINTS]);
                }
                points.swap(thinned);
            }
            return points;
        }
    };

}

#endif
