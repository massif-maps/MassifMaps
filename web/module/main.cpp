/*
 * The SDK module an app loads: no map until the page asks for one, and everything after that
 * through the facade (web/js/massif.mjs). web/demo/main.cpp is the bench, not this.
 */

#include "ui/WebMapView.h"
#include "api/MassifApiC.h"
#include "api/MassifInterop.h"
#include "components/Layers.h"
#include "components/Options.h"
#include "layers/VectorLayer.h"
#include "layers/VectorTileLayer.h"
#include "utils/Log.h"

#include <memory>
#include <string>

#include <emscripten/emscripten.h>
#include <emscripten/threading.h>

namespace {
    // One per module: emscripten keeps one callback per event target, so a second view would take
    // the document's pointer events from the first.
    std::shared_ptr<massif::WebMapView> _mapView;
}

int main() {
    // "ui" delivery runs a handler on the page's thread, where the JavaScript function table lives.
    mm_set_ui_dispatcher(mm_context_default(), [](void*, void (*function)(void*), void* argument) {
        if (emscripten_is_main_runtime_thread()) {
            // Never inline: the async run below calls through on its own thread, inside the emitter.
            emscripten_async_call(function, argument, 0);
        } else {
            emscripten_async_run_in_main_runtime_thread(EM_FUNC_SIG_VI, reinterpret_cast<void*>(function), argument);
        }
    }, nullptr);
    emscripten_exit_with_live_runtime();
    return 0;
}

extern "C" {

/**
 * Starts the map on a canvas. Nothing is adopted: the page names it with massifAdopt.
 * @return 1, or 0 if a map already exists or the canvas has no WebGL 2.
 */
EMSCRIPTEN_KEEPALIVE int massifCreateMap(const char* canvasSelector) {
    if (_mapView || !canvasSelector) {
        return 0;
    }
    auto mapView = std::make_shared<massif::WebMapView>(canvasSelector);
    if (!mapView->start()) {
        return 0;
    }
    _mapView = mapView;
    return 1;
}

/**
 * Adopts part of the map under `kind`/`id` - `what` is "view", "options" or "layers" - which is
 * what MassifInterop::adopt is to the other platforms' bindings.
 * @return The handle, or 0 if there is no map, `what` is unknown or the id is taken.
 */
EMSCRIPTEN_KEEPALIVE int massifAdopt(const char* kind, const char* what, const char* id) {
    if (!_mapView || !kind || !what || !id) {
        return 0;
    }
    std::string part(what);
    if (part == "view") {
        return massif::api::MassifInterop::adopt(kind, id, std::static_pointer_cast<massif::BaseMapView>(_mapView));
    }
    if (part == "options") {
        return massif::api::MassifInterop::adopt(kind, id, _mapView->getOptions());
    }
    if (part == "layers") {
        return massif::api::MassifInterop::adopt(kind, id, _mapView->getLayers());
    }
    return 0;
}

/** Makes the map's clicks and moves facade events on `handle`, chaining any listener already set. */
EMSCRIPTEN_KEEPALIVE void massifAttachMapEvents(int handle) {
    if (_mapView) {
        _mapView->setMapEventListener(massif::api::MassifInterop::createEventBridge(handle, _mapView->getMapEventListener()));
    }
}

/**
 * Makes a layer's feature or element clicks facade events on its handle, as the Android and iOS
 * sugar does when a click is first subscribed.
 * @return 1, or 0 if the handle is not a vector or vector tile layer.
 */
EMSCRIPTEN_KEEPALIVE int massifBridgeLayerClicks(int handle) {
    std::shared_ptr<massif::Layer> layer = massif::api::MassifInterop::getLayerByHandle(handle);
    if (auto tileLayer = std::dynamic_pointer_cast<massif::VectorTileLayer>(layer)) {
        tileLayer->setVectorTileEventListener(massif::api::MassifInterop::createVectorTileEventBridge(handle, tileLayer->getVectorTileEventListener()));
        return 1;
    }
    if (auto vectorLayer = std::dynamic_pointer_cast<massif::VectorLayer>(layer)) {
        vectorLayer->setVectorElementEventListener(massif::api::MassifInterop::createVectorElementEventBridge(handle, vectorLayer->getVectorElementEventListener()));
        return 1;
    }
    return 0;
}

}
