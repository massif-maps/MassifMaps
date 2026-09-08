/*
 * The web bench: one map on a canvas, configured from the URL query string - the browser's
 * equivalent of the Android demo's intent extras.
 *
 *   ?lon=2.35&lat=48.86&zoom=12
 *   ?source=https://tile.openstreetmap.org/{z}/{x}/{y}.png          raster (inferred)
 *   ?source=https://.../{z}/{x}/{y}.mvt&css=<url-encoded CartoCSS>  vector
 *   ?minzoom=0&maxzoom=14                                           what the tileset actually holds
 *   ?tiledrawsize=512&tilelodfactor=1                               pick tiles as a web map does
 *
 * The style is passed in rather than fetched: main() runs on the browser's main thread, where a
 * synchronous fetch is illegal. The JavaScript binding over the facade C ABI is what will replace
 * this whole file.
 */

#include "ui/WebMapView.h"
#include "api/MassifInterop.h"
#include "core/MapPos.h"
#include "components/Layers.h"
#include "components/Options.h"
#include "datasources/HTTPTileDataSource.h"
#include "layers/RasterTileLayer.h"
#include "layers/VectorTileLayer.h"
#include "projections/Projection.h"
#include "styles/CartoCSSStyleSet.h"
#include "styles/CompiledStyleSet.h"
#include "utils/DirAssetPackage.h"
#include "core/BinaryData.h"
#include "utils/Log.h"
#include "vectortiles/MBVectorTileDecoder.h"
#include "components/TerrainOptions.h"
#include "rastertiles/TerrariumElevationDataDecoder.h"
#include "rastertiles/MapBoxElevationDataDecoder.h"

#include <cstdlib>
#include <memory>
#include <string>

#include <emscripten/emscripten.h>
#include <emscripten/em_asm.h>

namespace {
    std::shared_ptr<massif::WebMapView> _MapView;

    const char* const DEFAULT_SOURCE = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";

    // TerrainOptions clamps this to 2..256; 64 is the SDK's phone-sized default.
    const int WEB_TERRAIN_MESH_RESOLUTION = 128;

    // Enough to see that a vector tile decoded: land, water, roads, buildings.
    const char* const DEFAULT_CSS =
        "Map { background-color: #f8f4f0; }\n"
        "#water { polygon-fill: #a0c8f0; }\n"
        "#landuse { polygon-fill: #e0e8d8; }\n"
        "#building { polygon-fill: #d9d0c9; }\n"
        "#road { line-color: #ffffff; line-width: 2; line-cap: round; line-join: round; }\n";

    /**
     * The query string first, then whatever the page put in globalThis.MASSIF_DEFAULTS - which is
     * how web/demo/config.json (gitignored, because it holds a tile-provider token) sets what
     * /demo/ shows with no parameters at all.
     */
    std::string queryParam(const char* name, const char* fallback) {
        char* value = reinterpret_cast<char*>(EM_ASM_PTR({
            var name = UTF8ToString($0);
            var found = new URLSearchParams(globalThis.location.search).get(name);
            if (found === null && globalThis.MASSIF_DEFAULTS) {
                found = globalThis.MASSIF_DEFAULTS[name] ?? null;
            }
            return found === null ? 0 : stringToNewUTF8(String(found));
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
    // A tileset that stops at z14 - every OpenMapTiles build does - draws NOTHING deeper unless
    // the source is told, because the layer asks for a tile that was never made instead of
    // overzooming the last one it has.
    int minZoom = static_cast<int>(queryNumber("minzoom", 0));
    int maxZoom = static_cast<int>(queryNumber("maxzoom", 19));
    auto dataSource = std::make_shared<massif::HTTPTileDataSource>(minZoom, maxZoom, source);

    if (isRasterSource(source)) {
        _MapView->getLayers()->add(std::make_shared<massif::RasterTileLayer>(dataSource));
    } else {
        std::shared_ptr<massif::MBVectorTileDecoder> decoder;
        std::string project = queryParam("project", "");
        if (!project.empty()) {
            // A whole CartoCSS project (project.json + .mss), preloaded under /styles - which is
            // what `massif-style mapbox2css` writes, so a converted MapBox style renders as-is.
            // CompiledStyleSet wants the style's entry file at the ROOT of the package, so the
            // package is the project directory and the style name is the file without its
            // extension - "project" for a mapbox2css run, or "night" for one of its themes.
            auto assets = std::make_shared<massif::DirAssetPackage>("/styles/" + project + "/");
            auto styleSet = std::make_shared<massif::CompiledStyleSet>(assets, queryParam("style", "project"));
            decoder = std::make_shared<massif::MBVectorTileDecoder>(styleSet);
        } else {
            auto styleSet = std::make_shared<massif::CartoCSSStyleSet>(queryParam("css", DEFAULT_CSS));
            decoder = std::make_shared<massif::MBVectorTileDecoder>(styleSet);
        }
        // Every font in /fonts becomes a fallback, so a style that names one the build does not
        // carry - "DIN Pro Medium" in Mapbox Standard - still draws its labels.
        auto fonts = std::make_shared<massif::DirAssetPackage>("/fonts/");
        for (const std::string& name : fonts->getAssetNames()) {
            if (std::shared_ptr<massif::BinaryData> data = fonts->loadAsset(name)) {
                decoder->addFallbackFont(data);
            }
        }
        _MapView->getLayers()->add(std::make_shared<massif::VectorTileLayer>(dataSource, decoder));
    }

    // ?terrain=<DEM url> turns on real 3D terrain. Mapterhorn's planet archive is Terrarium-coded
    // WebP in PMTiles, z0-12, and the SDK reads a .pmtiles URL by range - so a 705 GB archive costs
    // only the tiles actually looked at.
    std::string terrain = queryParam("terrain", "");
    if (!terrain.empty()) {
        auto elevationSource = std::make_shared<massif::HTTPTileDataSource>(0, static_cast<int>(queryNumber("terrainMaxZoom", 12)), terrain);
        // ?demEncoding=mapbox for Terrain-RGB, the default is Terrarium.
        std::shared_ptr<massif::ElevationDecoder> elevationDecoder;
        if (queryParam("demEncoding", "terrarium") == "mapbox") {
            elevationDecoder = std::make_shared<massif::MapBoxElevationDataDecoder>();
        } else {
            elevationDecoder = std::make_shared<massif::TerrariumElevationDataDecoder>();
        }
        auto terrainOptions = std::make_shared<massif::TerrainOptions>(elevationSource, elevationDecoder);
        terrainOptions->setEnabled(true);
        // Desktop terrain defaults, which are not a phone's. Auto-flatten exists so a phone stops
        // paying for a height field it cannot see from straight above; a desktop GPU can hold the
        // terrain up the whole time, and dropping it every time the map returns to 88 degrees is a
        // visible sink-and-rise. Setting both auto triggers to 0 is how TerrainOptions documents
        // "off". The mesh doubles for the same reason - 64 cells per tile edge is a phone budget.
        terrainOptions->setAutoFlattenTilt(0.0f);
        terrainOptions->setAutoFlattenParallax(0.0f);
        terrainOptions->setMeshResolution(WEB_TERRAIN_MESH_RESOLUTION);
        _MapView->getOptions()->setTerrainOptions(terrainOptions);
    }

    // A zoom NUMBER means a different distance here than in a web map: the SDK calibrates on a
    // 256-pixel tile and maplibre on a 512-pixel one, so the same number is a level apart. Pass 1
    // to read the query string's zoom as a web map would - see ZoomConvention.h. A tiledrawsize of
    // 512 adopts the same convention for the TILE the layer picks, which the offset does not touch.
    _MapView->getOptions()->setZoomOffset(static_cast<float>(queryNumber("zoomoffset", 0)));
    _MapView->getOptions()->setTileDrawSize(static_cast<int>(queryNumber("tiledrawsize", 256)));
    // And the SDK refines a full level finer than tangram and mapbox do: a tilelodfactor of 1 is
    // their rule verbatim, where the default 0.5 is what draws a level deeper at the same camera.
    _MapView->getOptions()->setTileLODFactor(static_cast<float>(queryNumber("tilelodfactor", 0.5)));

    massif::MapPos wgs84(queryNumber("lon", 2.3522), queryNumber("lat", 48.8566));
    _MapView->setFocusPos(_MapView->getOptions()->getBaseProjection()->fromWgs84(wgs84), 0);
    _MapView->setZoom(static_cast<float>(queryNumber("zoom", 12)), 0);

    // Hand the view to the facade so the page can drive the camera through the C ABI. Without this
    // the JavaScript binding has an ABI but nothing to point it at.
    massif::api::MassifInterop::adopt("map", "map", _MapView);
    // Options too, so the page can try a setting without a rebuild.
    massif::api::MassifInterop::adopt("options", "map", _MapView->getOptions());
    // And the layer list, which is what makes the whole map replaceable from JavaScript: the style
    // preview clears this and adds a layer it built from a spec of its own.
    massif::api::MassifInterop::adopt("layers", "map", _MapView->getLayers());

    // The frame loop is requestAnimationFrame, so main() returning must not tear the runtime down.
    emscripten_exit_with_live_runtime();
    return 0;
}
