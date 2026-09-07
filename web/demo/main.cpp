/*
 * The smallest thing that proves the web build renders: one raster layer on a canvas.
 *
 * The style preview this exists for reads its layer from JavaScript instead, through the facade's
 * C ABI - that binding is not written yet, so the layer is hard-coded here.
 */

#include "ui/WebMapView.h"
#include "core/MapPos.h"
#include "components/Layers.h"
#include "datasources/HTTPTileDataSource.h"
#include "layers/RasterTileLayer.h"
#include "projections/Projection.h"
#include "components/Options.h"
#include "utils/Log.h"

#include <memory>

#include <emscripten/emscripten.h>

namespace {
    std::shared_ptr<massif::WebMapView> _MapView;
}

int main() {
    massif::Log::SetShowInfo(true);

    _MapView = std::make_shared<massif::WebMapView>("#map");
    if (!_MapView->start()) {
        return 1;
    }

    auto dataSource = std::make_shared<massif::HTTPTileDataSource>(0, 19, "https://tile.openstreetmap.org/{z}/{x}/{y}.png");
    _MapView->getLayers()->add(std::make_shared<massif::RasterTileLayer>(dataSource));

    _MapView->setFocusPos(_MapView->getOptions()->getBaseProjection()->fromWgs84(massif::MapPos(2.3522, 48.8566)), 0);
    _MapView->setZoom(12, 0);

    // The frame loop is requestAnimationFrame, so main() returning must not tear the runtime down.
    emscripten_exit_with_live_runtime();
    return 0;
}
