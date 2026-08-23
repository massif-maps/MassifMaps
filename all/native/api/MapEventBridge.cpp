#include "api/MapEventBridge.h"
#include "ui/MapClickInfo.h"
#include "ui/MapInteractionInfo.h"
#include "ui/VectorTileClickInfo.h"
#include "ui/VectorElementClickInfo.h"

namespace massif { namespace api {

    PayloadEmitter::PayloadEmitter(const std::shared_ptr<Context>& context, Handle target) :
        _context(context), _target(target), _payloadCounter(0) {
    }

    bool PayloadEmitter::emit(const std::string& event, const std::shared_ptr<void>& obj,
                              const char* cppClass) {
        if (!_context) {
            return false;
        }
        if (!obj) {
            return _context->emit(_target, event, NULL_HANDLE);
        }
        // A throwaway id: the payload is addressable for the emit, and the id is dropped straight
        // after. A queued handler keeps it alive through the retain the queue takes.
        std::string id = "__payload/" + std::to_string(++_payloadCounter);
        Handle payload = NULL_HANDLE;
        if (_context->registerObject("payload", id, obj, cppClass, payload) != RESULT_OK) {
            return false;
        }
        // A click info carries map coordinates but names no projection, so it borrows the one the
        // event's target is in - which for a map is its base projection.
        _context->setObjectProjection(payload, _context->getObjectProjection(_target));
        bool consumed = _context->emit(_target, event, payload);
        _context->unregisterObject("payload", id);
        return consumed;
    }

    MapEventBridge::MapEventBridge(const std::shared_ptr<Context>& context, Handle target,
                                   const std::shared_ptr<MapEventListener>& chained) :
        _emitter(context, target), _chained(chained) {
    }

    MapEventBridge::~MapEventBridge() {
    }

    void MapEventBridge::onMapIdle() {
        if (_chained) {
            _chained->onMapIdle();
        }
        _emitter.emit("map.idle", std::shared_ptr<void>(), nullptr);
    }

    void MapEventBridge::onMapMoved() {
        if (_chained) {
            _chained->onMapMoved();
        }
        _emitter.emit("map.moved", std::shared_ptr<void>(), nullptr);
    }

    void MapEventBridge::onMapStable() {
        if (_chained) {
            _chained->onMapStable();
        }
        _emitter.emit("map.stable", std::shared_ptr<void>(), nullptr);
    }

    void MapEventBridge::onMapInteraction(const std::shared_ptr<MapInteractionInfo>& mapInteractionInfo) {
        if (_chained) {
            _chained->onMapInteraction(mapInteractionInfo);
        }
        _emitter.emit("map.interaction", mapInteractionInfo, "massif::MapInteractionInfo");
    }

    void MapEventBridge::onMapClicked(const std::shared_ptr<MapClickInfo>& mapClickInfo) {
        if (_chained) {
            _chained->onMapClicked(mapClickInfo);
        }
        _emitter.emit("map.clicked", mapClickInfo, "massif::MapClickInfo");
    }

    VectorTileEventBridge::VectorTileEventBridge(const std::shared_ptr<Context>& context, Handle target,
                                                 const std::shared_ptr<VectorTileEventListener>& chained) :
        _emitter(context, target), _chained(chained) {
    }

    VectorTileEventBridge::~VectorTileEventBridge() {
    }

    bool VectorTileEventBridge::onVectorTileClicked(const std::shared_ptr<VectorTileClickInfo>& clickInfo) {
        // Either side may claim the click, so the results are OR-ed rather than the facade's
        // answer replacing the app's.
        bool chainedHandled = _chained ? _chained->onVectorTileClicked(clickInfo) : false;
        bool consumed = _emitter.emit("vectortile.clicked", clickInfo, "massif::VectorTileClickInfo");
        return chainedHandled || consumed;
    }

    VectorElementEventBridge::VectorElementEventBridge(const std::shared_ptr<Context>& context, Handle target,
                                                       const std::shared_ptr<VectorElementEventListener>& chained) :
        _emitter(context, target), _chained(chained) {
    }

    VectorElementEventBridge::~VectorElementEventBridge() {
    }

    bool VectorElementEventBridge::onVectorElementClicked(const std::shared_ptr<VectorElementClickInfo>& clickInfo) {
        bool chainedHandled = _chained ? _chained->onVectorElementClicked(clickInfo) : false;
        bool consumed = _emitter.emit("vectorelement.clicked", clickInfo, "massif::VectorElementClickInfo");
        return chainedHandled || consumed;
    }

} }
