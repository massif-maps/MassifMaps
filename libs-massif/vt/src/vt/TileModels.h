#ifndef _MASSIF_VT_TILEMODELS_H_
#define _MASSIF_VT_TILEMODELS_H_

#include "Color.h"
#include "Model.h"
#include "Styles.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

#include <cglib/vec.h>
#include <cglib/bbox.h>
#include <cglib/ray.h>

namespace massif::vt {
    // What every instance of one draw shares: one model, one opacity, one spin.
    struct ModelStyle final {
        std::shared_ptr<const Model> model;
        FloatFunction opacityFunc = FloatFunction(1.0f);
        bool castShadows = true;
        float spinSpeed = 0.0f; // radians per second about spinAxis, through the model origin
        cglib::vec3<float> spinAxis = cglib::vec3<float>(0, 1, 0);
        bool densityReduction = true; // see TileLayerBuilder::createModelProcessor
        bool allowOverlap = false;

        bool operator == (const ModelStyle& other) const {
            return model == other.model && opacityFunc == other.opacityFunc && castShadows == other.castShadows && spinSpeed == other.spinSpeed && spinAxis == other.spinAxis && densityReduction == other.densityReduction && allowOverlap == other.allowOverlap;
        }
    };

    struct ModelInstance final {
        cglib::vec2<float> position = cglib::vec2<float>(0, 0); // vertex frame: tile units, y up
        float zOffset = 0.0f;   // metres above the ground
        float scale = 1.0f;
        float rotation = 0.0f;  // radians, clockwise from north as a compass heading
        float colorMix = 0.0f;  // 0 keeps the model's colours, 1 replaces them by color
        float spinPhase = 0.0f; // radians
        Color color = Color(1, 1, 1, 1);
    };

    // One model's instances in one tile layer, drawn with a single instanced call per mesh.
    class TileModelInstances final {
    public:
        explicit TileModelInstances(ModelStyle style, std::vector<ModelInstance> instances, std::vector<long long> ids, std::vector<long long> featureIds, std::vector<int> geoPosIndexes) : _style(std::move(style)), _instances(std::move(instances)), _ids(std::move(ids)), _featureIds(std::move(featureIds)), _geoPosIndexes(std::move(geoPosIndexes)) { }

        const ModelStyle& getStyle() const { return _style; }
        const std::vector<ModelInstance>& getInstances() const { return _instances; }
        // Per instance: the tile-local id a click is decoded by, the source feature id an app sees,
        // and, for a MultiPoint, which of its points.
        const std::vector<long long>& getIds() const { return _ids; }
        const std::vector<long long>& getFeatureIds() const { return _featureIds; }
        const std::vector<int>& getGeoPosIndexes() const { return _geoPosIndexes; }

        std::size_t getResidentSize() const {
            return 32 + _instances.size() * (sizeof(ModelInstance) + 2 * sizeof(long long) + sizeof(int));
        }

    private:
        const ModelStyle _style;
        const std::vector<ModelInstance> _instances;
        const std::vector<long long> _ids;
        const std::vector<long long> _featureIds;
        const std::vector<int> _geoPosIndexes;
    };

    // Crowns placed so far in one tile layer, as discs; a model rule without allow-overlap keeps a
    // point only when its disc touches none. Discs of one point (a tower and its rotor) never collide.
    class ModelFootprints final {
    public:
        bool overlaps(const cglib::vec2<float>& position, float radius, long long id, int geoPointIndex) const {
            if (_cellSize <= 0) {
                return false;
            }
            auto hits = [&](const Footprint& other) {
                float reach = radius + other.radius;
                return !(other.id == id && other.geoPointIndex == geoPointIndex) && cglib::length(other.position - position) < reach;
            };
            int range = static_cast<int>(std::ceil((radius + _maxRadius) / _cellSize));
            if (range > 8) {
                return std::any_of(_footprints.begin(), _footprints.end(), hits);
            }
            int cx = static_cast<int>(std::floor(position(0) / _cellSize)), cy = static_cast<int>(std::floor(position(1) / _cellSize));
            for (int y = cy - range; y <= cy + range; y++) {
                for (int x = cx - range; x <= cx + range; x++) {
                    auto it = _cells.find(cellKey(x, y));
                    if (it != _cells.end() && std::any_of(it->second.begin(), it->second.end(), [&](std::size_t index) { return hits(_footprints[index]); })) {
                        return true;
                    }
                }
            }
            return false;
        }

        void add(const cglib::vec2<float>& position, float radius, long long id, int geoPointIndex) {
            if (!(radius > 0)) {
                return;
            }
            if (_cellSize <= 0) {
                _cellSize = 2 * radius;
            }
            _maxRadius = std::max(_maxRadius, radius);
            _cells[cellKey(static_cast<int>(std::floor(position(0) / _cellSize)), static_cast<int>(std::floor(position(1) / _cellSize)))].push_back(_footprints.size());
            _footprints.push_back(Footprint { position, radius, id, geoPointIndex });
        }

    private:
        struct Footprint {
            cglib::vec2<float> position;
            float radius;
            long long id;
            int geoPointIndex;
        };

        static long long cellKey(int x, int y) { return (static_cast<long long>(x) << 32) ^ static_cast<unsigned int>(y); }

        float _cellSize = 0;
        float _maxRadius = 0;
        std::vector<Footprint> _footprints;
        std::unordered_map<long long, std::vector<std::size_t>> _cells;
    };

    // The box a click is tested against: the model's bounds, or for a spinning model every box the
    // spin can sweep, which is the cube around the sphere through its farthest corner.
    inline cglib::bbox3<double> modelPickBounds(const ModelStyle& style) {
        cglib::bbox3<double> bounds = cglib::bbox3<double>::convert(style.model->bounds);
        if (style.spinSpeed != 0.0f) {
            double r = 0;
            for (int i = 0; i < 3; i++) {
                r = std::max(r, std::max(std::abs(bounds.min(i)), std::abs(bounds.max(i))));
            }
            r *= std::sqrt(3.0);
            bounds = cglib::bbox3<double>(cglib::vec3<double>(-r, -r, -r), cglib::vec3<double>(r, r, r));
        }
        return bounds;
    }

    // Where a ray meets an instance's box, as the ray's own parameter. metres: world units per metre
    // at the anchor; the ray goes into model space by an affine map, which keeps t.
    inline bool intersectModelInstance(const cglib::bbox3<double>& bounds, const ModelInstance& instance, const cglib::vec3<double>& anchor, double metres, const cglib::ray3<double>& ray, double& t) {
        double unit = metres * instance.scale;
        if (!(unit > 0)) {
            return false;
        }
        double c = std::cos(instance.rotation), s = std::sin(instance.rotation);
        auto toModel = [c, s, unit](const cglib::vec3<double>& v) {
            return cglib::vec3<double>((v(0) * c - v(1) * s) / unit, (v(0) * s + v(1) * c) / unit, v(2) / unit);
        };
        cglib::vec3<double> origin = toModel(ray.origin - anchor) - cglib::vec3<double>(0, 0, instance.zOffset / instance.scale);
        cglib::vec3<double> direction = toModel(ray.direction);
        if (!std::isfinite(origin(0)) || !std::isfinite(origin(1)) || !std::isfinite(origin(2))) {
            return false; // a NaN skips every slab below and would hit everywhere
        }
        // Slabs by hand: cglib::intersect_bbox lets a ray parallel to a slab through from outside it,
        // which is every straight-down click on a flat map.
        double tmin = -std::numeric_limits<double>::infinity(), tmax = std::numeric_limits<double>::infinity();
        for (int i = 0; i < 3; i++) {
            if (direction(i) == 0) {
                if (origin(i) < bounds.min(i) || origin(i) > bounds.max(i)) {
                    return false;
                }
                continue;
            }
            double t1 = (bounds.min(i) - origin(i)) / direction(i);
            double t2 = (bounds.max(i) - origin(i)) / direction(i);
            tmin = std::max(tmin, std::min(t1, t2));
            tmax = std::min(tmax, std::max(t1, t2));
        }
        if (tmax < 0 || tmin > tmax) {
            return false;
        }
        t = (tmin < 0 ? tmax : tmin);
        return true;
    }
}

#endif
