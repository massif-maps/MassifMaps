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

namespace {
    std::shared_ptr<massif::WebMapView> _MapView;
    // Held so the relief hooks below can reach them. The peak finder's shaders are the thing being
    // iterated on, so NONE of their source lives here - the page supplies both and can change them
    // on a reload, which is the whole point of driving this from JavaScript.
    std::shared_ptr<massif::TerrainOptions> _terrainOptions;
    std::shared_ptr<massif::PostProcessEffect> _reliefEffect;
    bool _reliefWantsNormals = true;

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

    // ?source=none adds NO tile layer at all, which a panorama needs: a draped raster or vector
    // layer paints the terrain itself, so with one in the scene the surface shader's output is
    // covered and every surface parameter reads as a no-op. Measured: with a raster source, setting
    // uShadeStrength and uSlopeShade to 0 or to 1.5 both changed 33 pixels (the HUD), and turning
    // the ink off left a flat sheet of paper - the picture was the post-process alone.
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

    // The frame loop is requestAnimationFrame, so main() returning must not tear the runtime down.
    emscripten_exit_with_live_runtime();
    return 0;
}

/*
 * THE RELIEF HOOKS: enough of the peak finder to reproduce it in a browser, driven from JavaScript.
 *
 * They exist because the C ABI facade cannot build one: PostProcessEffect has no kind and no spec,
 * so a page can create a TerrainOptions but neither construct an effect nor attach it. Declaring a
 * kind for it would change the SDK's public surface; three functions here do not.
 *
 * NO SHADER SOURCE LIVES IN C++. The page passes both shaders in, so editing them is a reload
 * rather than a rebuild - which is the reason for running the panorama here at all. The parameters
 * go the same way, so ridge strength, deadzone and ground span can be swept from a script.
 */
extern "C" {

/*
 * The panorama CAMERA, in one call.
 *
 * A map clamps tilt to its own range because it is a map; a panorama looks at the horizon, which is
 * a few degrees, and a flyTo silently lands on the clamp instead - the first capture asked for 4 and
 * came back 84.3. The range has to move before the camera does, and neither is a value the C ABI
 * carries as a plain property.
 */
EMSCRIPTEN_KEEPALIVE void massifSetPanoramaCamera(double lon, double lat, float zoom, float rotation,
                                                  float tilt, float elevationMeters) {
    if (!_MapView) {
        return;
    }
    _MapView->getOptions()->setTiltRange(massif::MapRange(0.0f, 90.0f));
    // moveCameraTo places the CAMERA at the position, z included - it translates focus and camera
    // together by the camera-to-target vector rather than seating a focus point - so the eye goes on
    // the summit rather than on the ground under it, which is what first person means. focusLift is
    // the OTHER way to do this (the app's), and the two must not both be used or the eye is lifted
    // twice: this hook owns the height here, and massifSetFocusLift is left for comparing them.
    massif::MapPos wgs84(lon, lat, elevationMeters);
    massif::MapPos pos = _MapView->getOptions()->getBaseProjection()->fromWgs84(wgs84);
    pos.setZ(elevationMeters);
    _MapView->moveCameraTo(pos, zoom, rotation, tilt);
}

/** Must be called BEFORE massifSetReliefShader: the layout is fixed when the effect is built. */
EMSCRIPTEN_KEEPALIVE void massifSetReliefNormals(int wanted) {
    _reliefWantsNormals = (wanted != 0);
}

/**
 * The terrain's own base fill. Transparent by default, which means the terrain is see-through
 * wherever no layer has painted yet - and while the tiles load that is a lot of it, so the clear
 * colour shows through as hard black patches that settle into the picture as the drape arrives.
 * Filling with the paper colour makes the unpainted state the same colour as the finished one.
 */
EMSCRIPTEN_KEEPALIVE void massifSetTerrainBackground(int r, int g, int b, int a) {
    if (_terrainOptions) {
        _terrainOptions->setBackgroundColor(massif::Color(static_cast<unsigned char>(r), static_cast<unsigned char>(g),
                                                          static_cast<unsigned char>(b), static_cast<unsigned char>(a)));
    }
}

/**
 * The frame's haze. A panorama draws its own aerial perspective in the surface shader, over a
 * distance it chooses, so the SDK's fog on top of that is a second wash nobody asked for. ANDed with
 * the style, so switching it off here cannot be re-enabled by a style.
 */
EMSCRIPTEN_KEEPALIVE void massifSetFogEnabled(int enabled) {
    if (_MapView && _MapView->getOptions()->getFogOptions()) {
        _MapView->getOptions()->getFogOptions()->setEnabled(enabled != 0);
    }
}

/**
 * 0 off, 1 look, 2 first person. First person is the one a panorama wants: a drag turns the view
 * about the CAMERA on both axes and the position never moves, which is a mouse in a first person
 * game rather than a map being spun about a point on the ground.
 */
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
 * No sky, the way the app does it (peakFinder.ts applyAtmosphere). THREE things draw a band above
 * the horizon and each one alone leaves a gradient: the shader sky, the legacy sky BITMAP (whose
 * switch is a transparent skyColor - any real colour there generates a gradient), and the
 * BackgroundRenderer's plane, which falls back to the SDK's own block pattern when no style has an
 * opinion, so it has to be nulled rather than coloured. With all three off, the clear colour is
 * what shows.
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

/**
 * How high the eye stands above the ground, in metres - the panorama's one viewpoint control.
 *
 * NOT a camera z. With a terrain attached the renderer OWNS the focus height: it sits the focus on
 * the ground every frame, so an altitude written into the focus position lasts until the next one.
 * focusLift is ADDED on top of that rule, so it survives and means the same thing at every zoom.
 */
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
        // CLAMPED TO 256 by setMeshResolution, which is why asking for 512 here reads back as no
        // change at all. surfaceNodeResolution is the surface's own grid and goes to 512.
        _terrainOptions->setMeshResolution(static_cast<int>(value));
    } else if (key == "surfaceNodeResolution") {
        _terrainOptions->setSurfaceNodeResolution(static_cast<int>(value));
    } else if (key == "postProcessDownscale") {
        _terrainOptions->setPostProcessDownscale(static_cast<int>(value));
    } else if (key == "meshCacheSize") {
        _terrainOptions->setMeshCacheSize(static_cast<int>(value));
    } else if (key == "sharedGround") {
        _terrainOptions->setSharedGroundEnabled(value != 0);
    } else if (key == "tileEdgeStitching") {
        _terrainOptions->setTileEdgeStitchingEnabled(value != 0);
    }
}

/** Replaces the ink pass. A shader is compiled into an effect, so a new source is a new effect. */
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
    // Normals only when the shader asks for them. The depth-only outline wants the OTHER layout -
    // all 24 bits spent on depth - because an outline is exactly the thing that needs depth
    // precision, and the 16-bit sqrt depth the normal layout leaves room for quantises into visible
    // steps over a panorama's far plane. The shading no longer comes from here at all.
    _reliefEffect->setTerrainNormalsRequired(_reliefWantsNormals);
    _MapView->getMapRenderer()->setPostProcessEffect(_reliefEffect);
}

/** The SURFACE shader's own uniforms, which are terrain options rather than effect parameters. */
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
    // AND ASK FOR A FRAME. An effect's parameters live on the effect object, which holds no
    // reference back to the renderer (MapRenderer::setPostProcessEffect), so a write on its own
    // cannot request a redraw - and on an on-demand renderer with a still camera nothing else will.
    // Setting the same effect again is what the SDK documents as the way to publish a change; the
    // app's applyReliefOutline ends with exactly that call. Without this every parameter swept here
    // was a silent no-op, which is a bench that lies rather than a bench that measures.
    if (_MapView) {
        _MapView->getMapRenderer()->setPostProcessEffect(_reliefEffect);
    }
}

}
