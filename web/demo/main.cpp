/*
 * The web bench: one map on a canvas, configured from the URL query string - the browser's
 * equivalent of the Android demo's intent extras.
 *
 *   ?lon=2.35&lat=48.86&zoom=12
 *   ?source=https://tile.openstreetmap.org/{z}/{x}/{y}.png          raster (inferred)
 *   ?source=https://.../{z}/{x}/{y}.mvt&css=<url-encoded CartoCSS>  vector
 *   ?minzoom=0&maxzoom=14                                           what the tileset actually holds
 *   ?tiledrawsize=512&tilelodfactor=1                               pick tiles as a web map does
 *   ?labelperspective=0                                             0 holds a label's screen size,
 *                                                                   0.5 shrinks it as maplibre does
 *
 * The style is passed in rather than fetched: main() runs on the browser's main thread, where a
 * synchronous fetch is illegal. The JavaScript binding over the facade C ABI is what will replace
 * this whole file.
 */

#include "ui/WebMapView.h"
#include "api/MassifInterop.h"
#include "core/MapPos.h"
#include "graphics/Color.h"
#include "components/Layers.h"
#include "components/Options.h"
#include "datasources/HTTPTileDataSource.h"
#include "datasources/PersistentCacheTileDataSource.h"
#include "api/MassifApiC.h"
#include "layers/RasterTileLayer.h"
#include "renderers/PostProcessEffect.h"
#include "renderers/MapRenderer.h"
#include "layers/VectorTileLayer.h"
#include "projections/Projection.h"
#include "styles/CartoCSSStyleSet.h"
#include "styles/CompiledStyleSet.h"
#include "utils/DirAssetPackage.h"
#include "core/BinaryData.h"
#include "utils/Log.h"
#include "vectortiles/MBVectorTileDecoder.h"
#include "components/TerrainOptions.h"
#include "components/FogOptions.h"
#include "components/SkyOptions.h"
#include "graphics/Bitmap.h"
#include "rastertiles/TerrariumElevationDataDecoder.h"
#include "rastertiles/MapBoxElevationDataDecoder.h"

#include <cstdlib>
#include <memory>
#include <string>

#include <emscripten/emscripten.h>
#include <emscripten/em_asm.h>
#include <emscripten/threading.h>

namespace {
    std::shared_ptr<massif::WebMapView> _MapView;
    // Held for the relief hooks below; the page supplies the shader sources, so a reload changes them.
    std::shared_ptr<massif::TerrainOptions> _terrainOptions;
    std::shared_ptr<massif::PostProcessEffect> _reliefEffect;
    bool _reliefWantsNormals = true;

    const char* const DEFAULT_SOURCE = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";

    // 64 is the SDK's phone-sized default; ?meshResolution= overrides this at init, which is the
    // only time it can be changed (see where it is applied).
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

    // Before any layer: a layer reads the display scale and the tile draw size when it joins the
    // map, so a change made after it is added reaches the camera but not the style.
    //
    // A zoom NUMBER means a different distance here than in a web map: the SDK calibrates on a
    // 256-pixel tile and maplibre on a 512-pixel one, so the same number is a level apart. Pass 1
    // to read the query string's zoom as a web map would - see ZoomConvention.h. A tiledrawsize of
    // 512 adopts the same convention for the TILE the layer picks, which the offset does not touch.
    _MapView->getOptions()->setZoomOffset(static_cast<float>(queryNumber("zoomoffset", 0)));
    _MapView->getOptions()->setTileDrawSize(static_cast<int>(queryNumber("tiledrawsize", 256)));
    // And the SDK refines a full level finer than tangram and mapbox do: a tilelodfactor of 1 is
    // their rule verbatim, where the default 0.5 is what draws a level deeper at the same camera.
    _MapView->getOptions()->setTileLODFactor(static_cast<float>(queryNumber("tilelodfactor", 0.5)));
    const double dpi = queryNumber("dpi", 0);
    if (dpi > 0) {
        _MapView->getOptions()->setDPI(static_cast<float>(dpi));
    }

    std::string source = queryParam("source", DEFAULT_SOURCE);
    // A tileset that stops at z14 - every OpenMapTiles build does - draws NOTHING deeper unless
    // the source is told, because the layer asks for a tile that was never made instead of
    // overzooming the last one it has.
    int minZoom = static_cast<int>(queryNumber("minzoom", 0));
    int maxZoom = static_cast<int>(queryNumber("maxzoom", 19));
    auto dataSource = std::make_shared<massif::HTTPTileDataSource>(minZoom, maxZoom, source);

    // ?source=none adds no tile layer, which a panorama needs: a draped layer covers the surface
    // shader's output, so every surface parameter would be a no-op.
    if (source == "none") {
        // nothing
    } else if (isRasterSource(source)) {
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
        auto vectorLayer = std::make_shared<massif::VectorTileLayer>(dataSource, decoder);
        // A cased road cross-fades badly: the fill is near the background colour, so early in the
        // fade only the casing reads and the road looks like an outline waiting to be filled.
        // 0 is what maplibre does - vector geometry appears, only rasters fade.
        vectorLayer->setLayerBlendingSpeed(static_cast<float>(queryNumber("blendspeed", 1)));
        vectorLayer->setLabelPerspectiveScaling(static_cast<float>(queryNumber("labelperspective", 0.5)));
        _MapView->getLayers()->add(vectorLayer);
    }

    // ?terrain=<DEM url> turns on real 3D terrain. Mapterhorn's planet archive is Terrarium-coded
    // WebP in PMTiles, z0-12, and the SDK reads a .pmtiles URL by range - so a 705 GB archive costs
    // only the tiles actually looked at.
    std::string terrain = queryParam("terrain", "");
    if (!terrain.empty()) {
        std::shared_ptr<massif::TileDataSource> elevationSource = std::make_shared<massif::HTTPTileDataSource>(0, static_cast<int>(queryNumber("terrainMaxZoom", 12)), terrain);
        // ?demCache=<path>: DEM database on a directory the page mounted on IndexedDB.
        std::string demCache = queryParam("demCache", "");
        if (!demCache.empty()) {
            elevationSource = std::make_shared<massif::PersistentCacheTileDataSource>(elevationSource, demCache);
        }
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
        // At init: meshes and the tile transformer read it when created, so a later change does nothing.
        terrainOptions->setMeshResolution(static_cast<int>(queryNumber("meshResolution", WEB_TERRAIN_MESH_RESOLUTION)));
        // At init: a mesh is built once and cached, so a later change leaves cached meshes as they were.
        terrainOptions->setTileEdgeStitchingEnabled(queryNumber("tileEdgeStitching", 1) != 0);
        // At init: several TerrainOptions setters do not invalidate what is already built or culled.
        const double viewDistance = queryNumber("viewDistance", 0);
        if (viewDistance > 0) {
            terrainOptions->setViewDistance(static_cast<float>(viewDistance));
        }
        // geo-three's cut (setSubdivideDistance) and its level cap; the cut is cached on the camera.
        const double subdivideDistance = queryNumber("subdivideDistance", 0);
        if (subdivideDistance > 0) {
            terrainOptions->setSubdivideDistance(static_cast<float>(subdivideDistance));
        }
        const double cutMaxZoom = queryNumber("cutMaxZoom", 0);
        if (cutMaxZoom > 0) {
            terrainOptions->setMaxZoom(static_cast<int>(cutMaxZoom));
        }
        // maxVisibleTiles = meshCacheSize / 2, the real limit on distant detail rather than viewDistance.
        // At init: the cut is cached on the camera and the elevation version, not on this budget.
        const double meshCacheSize = queryNumber("meshCacheSize", 0);
        if (meshCacheSize > 0) {
            terrainOptions->setMeshCacheSize(static_cast<int>(meshCacheSize));
        }
        const double exaggeration = queryNumber("exaggeration", 0);
        if (exaggeration > 0) {
            terrainOptions->setExaggeration(static_cast<float>(exaggeration));
        }
        _MapView->getOptions()->setTerrainOptions(terrainOptions);
        _terrainOptions = terrainOptions;
    }

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
    // "ui" handlers run on the page's thread: a JS handler is only callable where its function table lives.
    mm_set_ui_dispatcher(mm_context_default(), [](void*, void (*function)(void*), void* argument) {
        emscripten_async_run_in_main_runtime_thread(EM_FUNC_SIG_VI, reinterpret_cast<void*>(function), argument);
    }, nullptr);

    // The frame loop is requestAnimationFrame, so main() returning must not tear the runtime down.
    emscripten_exit_with_live_runtime();
    return 0;
}

/*
 * Relief hooks for the peak finder: the C ABI facade cannot build or attach a PostProcessEffect
 * (it has no kind or spec). The page passes the shader sources, so editing them is a reload.
 */
extern "C" {

/* The panorama camera: the tilt range must open before the move, or the tilt lands on the clamp. */
EMSCRIPTEN_KEEPALIVE void massifSetPanoramaCamera(double lon, double lat, float zoom, float rotation,
                                                  float tilt, float elevationMeters) {
    if (!_MapView) {
        return;
    }
    // Negative tilt looks above the horizon (90 is straight down); the default range stops at 0.
    _MapView->getOptions()->setTiltRange(massif::MapRange(-90.0f, 90.0f));
    // Height through focusLift: in first person the renderer holds the eye at terrain z + focusLift
    // every frame, so a z in the camera position is overwritten.
    massif::MapPos wgs84(lon, lat, 0.0);
    massif::MapPos pos = _MapView->getOptions()->getBaseProjection()->fromWgs84(wgs84);
    pos.setZ(0.0);
    _MapView->moveCameraTo(pos, zoom, rotation, tilt);
    if (_terrainOptions) {
        _terrainOptions->setFocusLift(elevationMeters < 0.0f ? 0.0f : elevationMeters);
    }
}

/** Turns a vector tile layer's clicks into "vectortile.clicked" facade events. */
EMSCRIPTEN_KEEPALIVE int massifBridgeLayerClicks(int handle) {
    auto layer = std::dynamic_pointer_cast<massif::VectorTileLayer>(massif::api::MassifInterop::getLayerByHandle(handle));
    if (!layer) {
        return 0;
    }
    layer->setVectorTileEventListener(massif::api::MassifInterop::createVectorTileEventBridge(handle, layer->getVectorTileEventListener()));
    return 1;
}

/** Call before massifSetReliefShader: the layout is fixed when the effect is built. */
EMSCRIPTEN_KEEPALIVE void massifSetReliefNormals(int wanted) {
    _reliefWantsNormals = (wanted != 0);
}

/** Terrain base fill; transparent by default, which shows black patches until the drape arrives. */
EMSCRIPTEN_KEEPALIVE void massifSetTerrainBackground(int r, int g, int b, int a) {
    if (_terrainOptions) {
        _terrainOptions->setBackgroundColor(massif::Color(static_cast<unsigned char>(r), static_cast<unsigned char>(g),
                                                          static_cast<unsigned char>(b), static_cast<unsigned char>(a)));
    }
}

/** The panorama draws its own aerial perspective. ANDed with the style, so a style cannot re-enable it. */
EMSCRIPTEN_KEEPALIVE void massifSetFogEnabled(int enabled) {
    if (_MapView && _MapView->getOptions()->getFogOptions()) {
        _MapView->getOptions()->getFogOptions()->setEnabled(enabled != 0);
    }
}

/** 0 off, 1 look, 2 first person (a drag turns the view about the camera, which never moves). */
EMSCRIPTEN_KEEPALIVE void massifSetFreeRoamMode(int mode) {
    if (!_MapView) {
        return;
    }
    massif::FreeRoamMode::FreeRoamMode modes[] = { massif::FreeRoamMode::FREE_ROAM_MODE_OFF,
                                                   massif::FreeRoamMode::FREE_ROAM_MODE_LOOK,
                                                   massif::FreeRoamMode::FREE_ROAM_MODE_FIRST_PERSON };
    _MapView->getOptions()->setFreeRoamMode(modes[mode < 0 || mode > 2 ? 0 : mode]);
}

/**
 * No sky, as peakFinder.ts applyAtmosphere: the shader sky, the legacy sky bitmap (off only with a
 * transparent skyColor) and the background bitmap (null, else the block pattern) each draw a band.
 */
EMSCRIPTEN_KEEPALIVE void massifSetSkyEnabled(int enabled, int r, int g, int b) {
    if (!_MapView) {
        return;
    }
    std::shared_ptr<massif::Options> options = _MapView->getOptions();
    if (std::shared_ptr<massif::SkyOptions> sky = options->getSkyOptions()) {
        sky->setEnabled(enabled != 0);
    }
    if (enabled == 0) {
        massif::Color paper(static_cast<unsigned char>(r), static_cast<unsigned char>(g),
                            static_cast<unsigned char>(b), 255);
        options->setSkyColor(massif::Color(0, 0, 0, 0));
        options->setBackgroundBitmap(std::shared_ptr<massif::Bitmap>());
        options->setClearColor(paper);
    }
}

/** Eye height above the ground, metres; not a camera z, which the renderer resets every frame. */
EMSCRIPTEN_KEEPALIVE void massifSetFocusLift(float metres) {
    if (_terrainOptions) {
        _terrainOptions->setFocusLift(metres < 0.0f ? 0.0f : metres);
    }
}

EMSCRIPTEN_KEEPALIVE void massifSetSurfaceShader(const char* source) {
    if (_terrainOptions && source) {
        _terrainOptions->setSurfaceShaderSource(source);
    }
}

EMSCRIPTEN_KEEPALIVE void massifSetTerrainFloat(const char* name, float value) {
    if (!_terrainOptions || !name) {
        return;
    }
    std::string key(name);
    if (key == "normalSampleDistance") {
        _terrainOptions->setNormalSampleDistance(value);
    } else if (key == "meshResolution") {
        // Clamped to 256; surfaceNodeResolution is the surface's own grid and goes to 512.
        _terrainOptions->setMeshResolution(static_cast<int>(value));
    } else if (key == "surfaceNodeResolution") {
        _terrainOptions->setSurfaceNodeResolution(static_cast<int>(value));
    } else if (key == "postProcessDownscale") {
        _terrainOptions->setPostProcessDownscale(static_cast<int>(value));
    } else if (key == "meshCacheSize") {
        _terrainOptions->setMeshCacheSize(static_cast<int>(value));
    } else if (key == "sharedGround") {
        _terrainOptions->setSharedGroundEnabled(value != 0);
    } else if (key == "exaggeration") {
        _terrainOptions->setExaggeration(value);
    } else if (key == "viewDistance") {
        // Minimum draw distance, metres: from a summit the factor rule alone can cut the far ranges.
        _terrainOptions->setViewDistance(value);
    } else if (key == "viewDistanceMax") {
        _terrainOptions->setViewDistanceMax(value);
    } else if (key == "viewDistanceFactor") {
        _terrainOptions->setViewDistanceFactor(value);
    } else if (key == "tileEdgeStitching") {
        _terrainOptions->setTileEdgeStitchingEnabled(value != 0);
    } else if (key == "subdivideDistance") {
        _terrainOptions->setSubdivideDistance(value);
    }
}

/** Replaces the ink pass; a new source is a new effect. */
EMSCRIPTEN_KEEPALIVE void massifSetReliefShader(const char* source) {
    if (!_MapView) {
        return;
    }
    if (!source || !*source) {
        _reliefEffect.reset();
        _MapView->getMapRenderer()->setPostProcessEffect(nullptr);
        return;
    }
    _reliefEffect = std::make_shared<massif::PostProcessEffect>("relief", source);
    _reliefEffect->setTerrainDepthRequired(true);
    // Without normals all 24 bits go to depth; the normal layout's 16 bits step on a far plane.
    _reliefEffect->setTerrainNormalsRequired(_reliefWantsNormals);
    _MapView->getMapRenderer()->setPostProcessEffect(_reliefEffect);
}

/** The surface shader's uniforms, which are terrain options rather than effect parameters. */
EMSCRIPTEN_KEEPALIVE void massifSetSurfaceParam(const char* name, float value) {
    if (_terrainOptions && name) {
        _terrainOptions->setSurfaceParameter(name, value);
    }
}

EMSCRIPTEN_KEEPALIVE void massifSetReliefParam(const char* name, float value) {
    if (!_reliefEffect || !name) {
        return;
    }
    _reliefEffect->setFloatParameter(name, value);
    // An effect holds no reference to the renderer, so a parameter write cannot request a redraw;
    // re-setting the effect is the documented way to publish a change.
    if (_MapView) {
        _MapView->getMapRenderer()->setPostProcessEffect(_reliefEffect);
    }
}

}
