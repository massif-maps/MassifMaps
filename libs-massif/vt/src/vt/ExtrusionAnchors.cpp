#include "ExtrusionAnchors.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <numeric>

namespace {
    using namespace massif;

    struct PointKey {
        std::uint32_t x, y;

        bool operator == (const PointKey& other) const { return x == other.x && y == other.y; }
    };

    struct PointKeyHash {
        std::size_t operator () (const PointKey& key) const {
            return std::hash<std::uint64_t>()((static_cast<std::uint64_t>(key.x) << 32) | key.y);
        }
    };

    // The bit pattern, not a rounded value: two footprints share a vertex only when the source
    // repeated the same integer coordinate, and the decoder's transform is affine, so both come
    // out of it identical. Rounding would instead glue neighbours that merely pass close by.
    PointKey pointKey(const cglib::vec2<float>& p) {
        PointKey key;
        float x = p(0) == 0.0f ? 0.0f : p(0); // -0 and +0 are the same vertex
        float y = p(1) == 0.0f ? 0.0f : p(1);
        std::memcpy(&key.x, &x, sizeof(float));
        std::memcpy(&key.y, &y, sizeof(float));
        return key;
    }

    std::size_t findRoot(std::vector<std::size_t>& parent, std::size_t i) {
        while (parent[i] != i) {
            parent[i] = parent[parent[i]];
            i = parent[i];
        }
        return i;
    }

    void unite(std::vector<std::size_t>& parent, std::size_t a, std::size_t b) {
        a = findRoot(parent, a);
        b = findRoot(parent, b);
        if (a != b) {
            parent[a] = b;
        }
    }

    // Where the rings cross the line coord == value, as an interval along the other axis. Only a
    // segment with its two ends strictly on opposite sides counts: a ring merely touching the line
    // has no inside to share with the tile on the far side.
    bool crossingInterval(const std::vector<const std::vector<cglib::vec2<float>>*>& rings, int axis, float value, float lo, float hi, float& minT, float& maxT) {
        bool found = false;
        int other = 1 - axis;
        for (const std::vector<cglib::vec2<float>>* ring : rings) {
            std::size_t n = ring->size();
            for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
                const cglib::vec2<float>& p = (*ring)[j];
                const cglib::vec2<float>& q = (*ring)[i];
                float dp = p(axis) - value;
                float dq = q(axis) - value;
                if ((dp < 0) == (dq < 0) || dp == dq) {
                    continue;
                }
                float t = p(other) + (q(other) - p(other)) * (dp / (dp - dq));
                if (t < lo || t > hi) {
                    continue; // crosses the line beyond the box, which is another tile's business
                }
                minT = found ? std::min(minT, t) : t;
                maxT = found ? std::max(maxT, t) : t;
                found = true;
            }
        }
        return found;
    }
}

namespace massif::vt {
    std::unordered_map<long long, cglib::vec2<float>> buildExtrusionAnchors(const std::vector<ExtrusionFootprint>& footprints, const cglib::bbox2<float>& sourceBox) {
        std::unordered_map<long long, cglib::vec2<float>> anchors;
        if (footprints.empty()) {
            return anchors;
        }
        anchors.reserve(footprints.size());

        std::vector<std::size_t> parent(footprints.size());
        std::iota(parent.begin(), parent.end(), std::size_t(0));

        std::unordered_map<PointKey, std::size_t, PointKeyHash> vertexOwner;
        std::unordered_map<long long, std::size_t> buildingOwner, localOwner;
        for (std::size_t i = 0; i < footprints.size(); i++) {
            const ExtrusionFootprint& footprint = footprints[i];
            // One multi-polygon feature is one building, however far apart its polygons lie: the
            // symbolizer draws them all under the one id, so they get the one anchor either way.
            auto localResult = localOwner.emplace(footprint.localId, i);
            if (!localResult.second) {
                unite(parent, localResult.first->second, i);
            }
            for (const std::vector<cglib::vec2<float>>& ring : footprint.rings) {
                for (const cglib::vec2<float>& p : ring) {
                    auto result = vertexOwner.emplace(pointKey(p), i);
                    if (!result.second) {
                        unite(parent, result.first->second, i);
                    }
                }
            }
            if (footprint.buildingId != 0) {
                auto result = buildingOwner.emplace(footprint.buildingId, i);
                if (!result.second) {
                    unite(parent, result.first->second, i);
                }
            }
        }

        // The group's own centroid is the mean of every OUTER ring point it owns, which is what a
        // single footprint already got - mapbox accumulates its parts the same way.
        struct Group {
            cglib::vec2<double> acc { 0, 0 };
            std::size_t accCount = 0;
            std::vector<const std::vector<cglib::vec2<float>>*> rings;
        };
        std::unordered_map<std::size_t, Group> groups;
        for (std::size_t i = 0; i < footprints.size(); i++) {
            const ExtrusionFootprint& footprint = footprints[i];
            if (footprint.rings.empty() || footprint.rings.front().empty()) {
                continue;
            }
            Group& group = groups[findRoot(parent, i)];
            for (const cglib::vec2<float>& p : footprint.rings.front()) {
                group.acc = group.acc + cglib::vec2<double>(p(0), p(1));
                group.accCount++;
            }
            group.rings.push_back(&footprint.rings.front());
        }

        std::unordered_map<std::size_t, cglib::vec2<float>> groupAnchors;
        groupAnchors.reserve(groups.size());
        for (auto it = groups.begin(); it != groups.end(); it++) {
            const Group& group = it->second;
            if (group.accCount == 0) {
                continue;
            }
            cglib::vec2<float> anchor(static_cast<float>(group.acc(0) / group.accCount), static_cast<float>(group.acc(1) / group.accCount));

            // The four edges of the source box, as (axis, value): x = min, x = max, y = min, y = max.
            const float edgeValue[4] = { sourceBox.min(0), sourceBox.max(0), sourceBox.min(1), sourceBox.max(1) };
            float crossMin[4] = { 0, 0, 0, 0 }, crossMax[4] = { 0, 0, 0, 0 };
            bool crossed[4] = { false, false, false, false };
            int crossCount = 0;
            for (int edge = 0; edge < 4; edge++) {
                int axis = edge < 2 ? 0 : 1;
                crossed[edge] = crossingInterval(group.rings, axis, edgeValue[edge], sourceBox.min(1 - axis), sourceBox.max(1 - axis), crossMin[edge], crossMax[edge]);
                crossCount += crossed[edge] ? 1 : 0;
            }
            if (crossCount == 1) {
                int edge = static_cast<int>(std::find(crossed, crossed + 4, true) - crossed);
                int axis = edge < 2 ? 0 : 1;
                float mid = 0.5f * (crossMin[edge] + crossMax[edge]);
                anchor = axis == 0 ? cglib::vec2<float>(edgeValue[edge], mid) : cglib::vec2<float>(mid, edgeValue[edge]);
            }
            else if (crossCount == 2 && !(crossed[0] && crossed[1]) && !(crossed[2] && crossed[3])) {
                anchor = cglib::vec2<float>(crossed[0] ? edgeValue[0] : edgeValue[1], crossed[2] ? edgeValue[2] : edgeValue[3]);
            }
            groupAnchors[it->first] = anchor;
        }

        for (std::size_t i = 0; i < footprints.size(); i++) {
            auto it = groupAnchors.find(findRoot(parent, i));
            if (it != groupAnchors.end()) {
                anchors[footprints[i].localId] = it->second;
            }
        }
        return anchors;
    }
}
