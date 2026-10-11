#ifndef _MASSIF_VT_TILEMODELS_H_
#define _MASSIF_VT_TILEMODELS_H_

#include "Color.h"
#include "Model.h"
#include "Styles.h"

#include <cstdint>
#include <memory>
#include <vector>

#include <cglib/vec.h>

namespace massif::vt {
    // What every instance of one draw shares: one model, one opacity, one spin.
    struct ModelStyle final {
        std::shared_ptr<const Model> model;
        FloatFunction opacityFunc = FloatFunction(1.0f);
        bool castShadows = true;
        float spinSpeed = 0.0f; // radians per second about spinAxis, through the model origin
        cglib::vec3<float> spinAxis = cglib::vec3<float>(0, 1, 0);

        bool operator == (const ModelStyle& other) const {
            return model == other.model && opacityFunc == other.opacityFunc && castShadows == other.castShadows && spinSpeed == other.spinSpeed && spinAxis == other.spinAxis;
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
        explicit TileModelInstances(ModelStyle style, std::vector<ModelInstance> instances, std::vector<long long> ids, std::vector<int> geoPosIndexes) : _style(std::move(style)), _instances(std::move(instances)), _ids(std::move(ids)), _geoPosIndexes(std::move(geoPosIndexes)) { }

        const ModelStyle& getStyle() const { return _style; }
        const std::vector<ModelInstance>& getInstances() const { return _instances; }
        // Per instance: the feature id and, for a MultiPoint, which of its points.
        const std::vector<long long>& getIds() const { return _ids; }
        const std::vector<int>& getGeoPosIndexes() const { return _geoPosIndexes; }

        std::size_t getResidentSize() const {
            return 32 + _instances.size() * (sizeof(ModelInstance) + sizeof(long long) + sizeof(int));
        }

    private:
        const ModelStyle _style;
        const std::vector<ModelInstance> _instances;
        const std::vector<long long> _ids;
        const std::vector<int> _geoPosIndexes;
    };
}

#endif
