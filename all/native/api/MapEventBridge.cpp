#include "api/MapEventBridge.h"
#include "ui/MapClickInfo.h"
#include "ui/MapInteractionInfo.h"

namespace massif { namespace api {

    MapEventBridge::MapEventBridge(const std::shared_ptr<Context>& context, Handle target,
                                   const std::shared_ptr<MapEventListener>& chained) :
        _context(context), _target(target), _chained(chained), _payloadCounter(0) {
    }

    MapEventBridge::~MapEventBridge() {
    }

    void MapEventBridge::emitWith(const std::string& event, const std::shared_ptr<void>& obj,
                                  const char* cppClass) {
        if (!_context) {
            return;
        }
        if (!obj) {
            _context->emit(_target, event, NULL_HANDLE);
            return;
        }
        // A throwaway id: the payload is addressable for the emit, and the id is dropped straight
        // after. A queued handler keeps it alive through the retain the queue takes.
        std::string id = "__payload/" + std::to_string(++_payloadCounter);
        Handle payload = NULL_HANDLE;
        if (_context->registerObject("payload", id, obj, cppClass, payload) != RESULT_OK) {
            return;
        }
        _context->emit(_target, event, payload);
        _context->unregisterObject("payload", id);
    }

    void MapEventBridge::onMapIdle() {
        if (_chained) {
            _chained->onMapIdle();
        }
        emitWith("map.idle", std::shared_ptr<void>(), nullptr);
    }

    void MapEventBridge::onMapMoved() {
        if (_chained) {
            _chained->onMapMoved();
        }
        emitWith("map.moved", std::shared_ptr<void>(), nullptr);
    }

    void MapEventBridge::onMapStable() {
        if (_chained) {
            _chained->onMapStable();
        }
        emitWith("map.stable", std::shared_ptr<void>(), nullptr);
    }

    void MapEventBridge::onMapInteraction(const std::shared_ptr<MapInteractionInfo>& mapInteractionInfo) {
        if (_chained) {
            _chained->onMapInteraction(mapInteractionInfo);
        }
        emitWith("map.interaction", mapInteractionInfo, "massif::MapInteractionInfo");
    }

    void MapEventBridge::onMapClicked(const std::shared_ptr<MapClickInfo>& mapClickInfo) {
        if (_chained) {
            _chained->onMapClicked(mapClickInfo);
        }
        emitWith("map.clicked", mapClickInfo, "massif::MapClickInfo");
    }

} }
