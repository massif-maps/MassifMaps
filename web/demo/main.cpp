/*
 * The web bench: one map on a canvas, configured from the URL query string - the browser's
 * equivalent of the Android demo's intent extras.
 *
 *   ?lon=2.35&lat=48.86&zoom=12
 *   ?source=https://tile.openstreetmap.org/{z}/{x}/{y}.png          raster (inferred)
 *   ?source=https://.../{z}/{x}/{y}.mvt&css=<url-encoded CartoCSS>  vector
 *
 * The style is passed in rather than fetched: main() runs on the browser's main thread, where a
 * synchronous fetch is illegal. The JavaScript binding over the facade C ABI is what will replace
 * this whole file.
 */

#include "ui/WebMapView.h"
#include "core/MapPos.h"
#include "components/Layers.h"
#include "components/Options.h"
#include "datasources/HTTPTileDataSource.h"
#include "layers/RasterTileLayer.h"
#include "layers/VectorTileLayer.h"
#include "projections/Projection.h"
#include "styles/CartoCSSStyleSet.h"
#include "utils/Log.h"
#include "vectortiles/MBVectorTileDecoder.h"

#include <cstdlib>
#include <memory>
#include <string>

#include <emscripten/emscripten.h>
#include <emscripten/em_asm.h>

namespace {
    std::shared_ptr<massif::WebMapView> _MapView;

    const char* const DEFAULT_SOURCE = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";

    // Enough to see that a vector tile decoded: land, water, roads, buildings.
    const char* const DEFAULT_CSS =
        "Map { background-color: #f8f4f0; }\n"
        "#water { polygon-fill: #a0c8f0; }\n"
        "#landuse { polygon-fill: #e0e8d8; }\n"
        "#building { polygon-fill: #d9d0c9; }\n"
        "#road { line-color: #ffffff; line-width: 2; line-cap: round; line-join: round; }\n";

    std::string queryParam(const char* name, const char* fallback) {
        char* value = reinterpret_cast<char*>(EM_ASM_PTR({
            var name = UTF8ToString($0);
            var found = new URLSearchParams(globalThis.location.search).get(name);
            return found === null ? 0 : stringToNewUTF8(found);
        }, name));
        if (!value) {
            return fallback;
        }
        std::string result(value);
        std::free(value);
        return result;
    }

    double queryNumber(const char* name, double fallback) {
        std::string value = queryParam(name, "");
        return value.empty() ? fallback : std::atof(value.c_str());
    }

    // A raster source is one that names an image; anything else is vector tile data.
    bool isRasterSource(const std::string& source) {
        for (const char* extension : { ".png", ".jpg", ".jpeg", ".webp" }) {
            if (source.find(extension) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
}

int main() {
    massif::Log::SetShowInfo(true);

    _MapView = std::make_shared<massif::WebMapView>("#map");
    if (!_MapView->start()) {
        return 1;
    }

    std::string source = queryParam("source", DEFAULT_SOURCE);
    auto dataSource = std::make_shared<massif::HTTPTileDataSource>(0, 19, source);

    if (isRasterSource(source)) {
        _MapView->getLayers()->add(std::make_shared<massif::RasterTileLayer>(dataSource));
    } else {
        auto styleSet = std::make_shared<massif::CartoCSSStyleSet>(queryParam("css", DEFAULT_CSS));
        auto decoder = std::make_shared<massif::MBVectorTileDecoder>(styleSet);
        _MapView->getLayers()->add(std::make_shared<massif::VectorTileLayer>(dataSource, decoder));
    }

    massif::MapPos wgs84(queryNumber("lon", 2.3522), queryNumber("lat", 48.8566));
    _MapView->setFocusPos(_MapView->getOptions()->getBaseProjection()->fromWgs84(wgs84), 0);
    _MapView->setZoom(static_cast<float>(queryNumber("zoom", 12)), 0);

    // The frame loop is requestAnimationFrame, so main() returning must not tear the runtime down.
    emscripten_exit_with_live_runtime();
    return 0;
}
