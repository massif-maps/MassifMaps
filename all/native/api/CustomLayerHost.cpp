#include "api/CustomLayerHost.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace massif { namespace api {

    namespace {
        const size_t MIN_SIZE = offsetof(mm_custom_layer, user_data) + sizeof(void*);
    }

    bool CustomLayerHost::validate(const mm_custom_layer* layer) {
        return layer && layer->size >= MIN_SIZE && layer->render && (layer->mode == MM_CUSTOM_LAYER_OVERLAY || layer->mode == MM_CUSTOM_LAYER_SCENE);
    }

    CustomLayerHost::CustomLayerHost(const mm_custom_layer& layer) {
        std::memset(&_layer, 0, sizeof(_layer));
        std::memcpy(&_layer, &layer, std::min<size_t>(layer.size, sizeof(_layer)));
    }

    CustomLayerHost::~CustomLayerHost() {
        if (_layer.destroy) {
            _layer.destroy(_layer.user_data);
        }
    }

    void CustomLayerHost::disown() {
        _layer.destroy = nullptr;
    }

    mm_custom_layer_mode CustomLayerHost::getMode() const {
        return _layer.mode;
    }

    void CustomLayerHost::render(const mm_render_params& params) const {
        _layer.render(_layer.user_data, &params);
    }

    void CustomLayerHost::release() const {
        if (_layer.on_removed) {
            _layer.on_removed(_layer.user_data);
        }
    }

    void CustomLayerHost::contextLost() const {
        if (_layer.on_context_lost) {
            _layer.on_context_lost(_layer.user_data);
        }
    }

    bool CustomLayerHost::canHitTest() const {
        return _layer.hit_test != nullptr;
    }

    bool CustomLayerHost::hitTest(const double origin[3], const double direction[3], double& t, uint64_t& feature) const {
        return _layer.hit_test && _layer.hit_test(_layer.user_data, origin, direction, &t, &feature) != 0;
    }

    bool CustomLayerHost::click(uint64_t feature, int clickType, const double hitPos[3]) const {
        return _layer.on_click && _layer.on_click(_layer.user_data, feature, clickType, hitPos) != 0;
    }

} }
