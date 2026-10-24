package com.massifmaps.MassifDemo.demo;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import com.massifmaps.api.Massif;
import com.massifmaps.api.MassifApi;
import com.massifmaps.api.MassifException;
import com.massifmaps.api.MassifLayer;
import com.massifmaps.api.MassifObject;
import com.massifmaps.api.MassifSource;
import com.massifmaps.api.PropertyGroup;
import com.massifmaps.api.Spec;
import com.massifmaps.api.MapCamera;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.Position;
import com.massifmaps.MassifDemo.customlayer.CustomHeatmap;
import com.massifmaps.ui.MapView;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * The whole demo map, as ONE composable configuration instead of a set of separate examples.
 *
 * MODEL
 *  - {@link DemoConfig} says what should exist;
 *  - this class owns the map objects and makes reality match the config: {@link #build()} once,
 *    then {@link #rebuildLayers()} / apply*() whenever something changes;
 *  - the panel (DemoPanel) only writes DemoConfig fields and calls back into here.
 *
 * LAYERS
 *  Each entry of {@link Feature} is an independent layer that can be switched on or off at any
 *  time. They are (re)built lazily and cached in {@link #layers}, and always re-added in
 *  {@link #LAYER_ORDER}, so toggling one never changes the draw order of the others.
 *
 * SOURCES
 *  Tile sources are created ONCE and shared: the DEM feeds the 3D terrain, the hillshade, the
 *  contours and the hypsometric tint at the same time, so a tile is downloaded and decoded once.
 */
public class DemoMap {

    private static final String TAG = "DemoMap";

    /** One switchable layer of the demo. */
    public enum Feature {
        CELESTIAL, STARS, BASE, SATELLITE, API_SOURCE, HILLSHADE, HYPSO, RADAR, CONTOUR, CONTOUR_TILES, ROUTES, ROUTE_TEST, ROUTE_SELECT, MANEUVERS, ELEMENTS, SPANS, BUGS, MODELS, PEAKS, CUSTOM_OVERLAY, CUSTOM_SCENE
    }

    /** Bottom -> top draw order. Toggling a layer never reorders the others. */
    private static final Feature[] LAYER_ORDER = {
        // The sky goes FIRST, so the map and the terrain draw over it and a ridge hides what is
        // behind it - which is what a body in the sky should do.
        Feature.CELESTIAL, Feature.STARS,
        Feature.BASE, Feature.SATELLITE, Feature.API_SOURCE, Feature.HILLSHADE, Feature.HYPSO, Feature.RADAR,
        Feature.CONTOUR, Feature.CONTOUR_TILES, Feature.ROUTES, Feature.ROUTE_TEST, Feature.ROUTE_SELECT, Feature.MANEUVERS, Feature.ELEMENTS,
        Feature.SPANS,
        Feature.BUGS,
        Feature.MODELS,
        // The summit names go over everything the map draws, bar the custom layers: the heatmap is depth tested
        // at its place, and its legend draws after everything whatever its place.
        Feature.PEAKS,
        Feature.CUSTOM_SCENE, Feature.CUSTOM_OVERLAY
    };

    /** Sun, moon and their daily paths - demo content built on the generic celestial API. */
    public final DemoCelestial celestial = new DemoCelestial();
    /** The bright-star catalogue, the constellation figures and the planets - same API. */
    public final DemoStars stars = new DemoStars();
    /** Device orientation driving the camera, for the star sky mode. */
    private DemoOrientation orientation;
    /** Decoder of the route selection bench - the selection is one style parameter on it. */
    private MassifObject routeSelectDecoder;
    private long routeSelectId = 1;
    private Runnable routeSelectCycle;
    /** Live camera preview behind the transparent map, for the star sky mode. */
    private DemoCameraPreview cameraPreview;

    private final Context context;
    public final MapView mapView;
    /** The map as the facade sees it. */
    public final MassifMap map;
    public final String dataPath;

    // --- map objects the panel needs to reach ---------------------------------------------------
    // The option groups are the map's own: they exist once an applyXxxOptions() has run.
    public PropertyGroup terrainOptions;
    public PropertyGroup lightOptions;
    public PropertyGroup skyOptions;
    public PropertyGroup fogOptions;
    public MassifLayer hillshadeLayer;                   // stand-alone hillshade layer, when built
    public MassifLayer baseLayer;                        // base map layer, whatever the mode
    public MassifLayer compositeLayer;                   // same layer as baseLayer in COMPOSITE mode
    public MassifObject baseDecoder;                     // decoder (style) of the base layer
    public MassifObject contourSource;                   // shared by the layer and the composite slot
    public MassifObject terrainObject;                   // the TerrainOptions object behind terrainOptions
    public MassifObject reliefEffect;                    // the attached relief outline effect, if any
    // What the peak-finder mode switched off, so leaving it puts the map back as it was.
    private boolean savedLayerBase, savedLayerHillshade, savedLayerContour, savedLayerContourTiles, savedLayerSatellite, savedLayerHypso;
    private float savedTilt;
    private float savedOcclusionTolerance;
    private float savedViewDistance = 1;
    /** Result of the last {@link #checkCompositeSlots()}: which slots the style really has. */
    public String compositeStatus = "";

    private final Map<Feature, MassifLayer> layers = new LinkedHashMap<Feature, MassifLayer>();

    // --- shared, lazily created tile sources (registered under an id each) -----------------------
    private MassifObject cachedDem;
    private MassifObject cachedVector;
    private MassifObject cachedRaster;
    private MassifObject cachedContourTiles;
    private MassifObject cachedLandform;
    private MassifObject cachedManeuvers;
    private int maneuverLayerIndex = -1;
    private MassifObject maneuverBuilder;
    private final Map<Integer, String> maneuverArrows = new LinkedHashMap<Integer, String>();
    private boolean seededManeuvers;

    // The enums the facade takes as numbers, named as in the SDK headers.
    private static final int TILE_SUBSTITUTION_VISIBLE = 1, TILE_SUBSTITUTION_NONE = 2;
    private static final int RASTER_FILTER_BILINEAR = 1;
    private static final int RENDER_ORDER_LAST = 1;
    private static final int COMPOSITE_RASTER = 0, COMPOSITE_HILLSHADE = 1;
    private static final int LOD_PROFILE_REFERENCE = 0, LOD_PROFILE_MOBILE = 1, LOD_PROFILE_DESKTOP = 2;
    private static final int FLATTEN_RENDER = 0, FLATTEN_FULL = 1;
    private static final int SKY_GRADIENT = 0, SKY_ATMOSPHERE = 1;
    private static final int SKY_LOW = 0, SKY_MEDIUM = 1, SKY_HIGH = 2;
    private static final int HILLSHADE_STANDARD = 0, HILLSHADE_COMBINED = 1, HILLSHADE_IGOR = 2, HILLSHADE_MULTIDIRECTIONAL = 3, HILLSHADE_BASIC = 4;
    private static final int PANNING_SPEED_MAP = 0, PANNING_SPEED_ANCHORED = 1, PANNING_SPEED_CONSTANT = 2;
    private static final int FREE_ROAM_OFF = 0, FREE_ROAM_LOOK = 1, FREE_ROAM_FIRST_PERSON = 2;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private boolean paramOn = true;

    public DemoMap(Context context, MapView mapView, String dataPath) {
        this.context = context;
        this.mapView = mapView;
        this.map = MassifMap.attach(mapView).eventProjection("EPSG:4326");
        this.dataPath = dataPath;
        DemoStyles.init(context);
    }

    // =============================================================================================
    // BUILD
    // =============================================================================================

    /** Applies the whole {@link DemoConfig} to a fresh map. */
    public void build() {
        // Options first: layers created afterwards pick up the terrain/light state immediately.
        map.set("tileThreadPoolSize", DemoConfig.TILE_THREAD_POOL_SIZE).set("envelopeThreadPoolSize", DemoConfig.TILE_THREAD_POOL_SIZE);
        applyTileLODConfig();
        applyDebugConfig();
        applyGlobeMode();
        applyTerrainOptions();
        applyLightOptions();
        applySkyOptions();

        rebuildLayers();
        applyCamera();

        if (DemoConfig.CAPTURE_TEST) {
            handler.postDelayed(new Runnable() {
                public void run() {
                    DemoTests.runCapture(DemoMap.this);
                }
            }, 45000);
        }
        if (DemoConfig.RELIEF_OUTLINE) {
            // Attaching a post-process effect before the GL surface exists leaves the offscreen
            // colour buffer unwritten and the screen black, so wait for the first frames.
            handler.postDelayed(new Runnable() {
                public void run() {
                    setReliefOutlineEnabled(true);
                }
            }, (long) DemoConfig.RELIEF_OUTLINE_DELAY_MS);
        }
        if (DemoConfig.PEAK_FINDER) {
            // After the same delay as the effect: attaching it before the GL surface exists
            // leaves the offscreen colour buffer unwritten.
            handler.postDelayed(new Runnable() {
                public void run() {
                    setPeakFinderMode(true);
                    if (DemoConfig.AR_MODE) {
                        setArMode(true); // over the camera, once the view it composites is there
                    }
                }
            }, (long) DemoConfig.RELIEF_OUTLINE_DELAY_MS);
        }
        if (DemoConfig.DAY_CYCLE) {
            applyDayCycle(DemoConfig.DAY_CYCLE_HOUR);
        }
        if (DemoConfig.STAR_SKY) {
            // Already built without the map layers (isEnabled), so there is nothing to fade out.
            saveMapAppearance();
            enterStarSky();
        }
        startScriptedAnimation();
        if (DemoConfig.GEOJSON_BENCH != null && !DemoConfig.GEOJSON_BENCH.isEmpty()) {
            DemoTests.runGeoJSONBench(this, DemoConfig.GEOJSON_BENCH);
        }
        if (DemoConfig.GEOJSON_BENCH_LAYER != null && !DemoConfig.GEOJSON_BENCH_LAYER.isEmpty()) {
            DemoTests.addGeoJSONBenchLayer(this, DemoConfig.GEOJSON_BENCH_LAYER);
        }
        if (DemoConfig.POPUP_FONTS != null && !DemoConfig.POPUP_FONTS.isEmpty()) {
            DemoTests.runPopupFonts(this, DemoConfig.POPUP_FONTS,
                    new Position(DemoConfig.START_LON, DemoConfig.START_LAT));
        }
    }

    // =============================================================================================
    // LAYERS
    // =============================================================================================

    /** True if the feature is currently switched on in the config. */
    public boolean isEnabled(Feature feature) {
        // Star sky mode: every map layer is left OUT of the layer list rather than hidden, so no
        // tile is fetched, decoded or drawn - the mode costs what an empty map costs.
        if (DemoConfig.STAR_SKY && feature != Feature.CELESTIAL && feature != Feature.STARS) {
            return false;
        }
        switch (feature) {
            case CELESTIAL: return DemoConfig.CELESTIAL;
            case STARS: return DemoConfig.STARS;
            case BASE: return DemoConfig.LAYER_BASE;
            case SATELLITE: return DemoConfig.LAYER_SATELLITE;
            case HILLSHADE: return DemoConfig.LAYER_HILLSHADE;
            case HYPSO: return DemoConfig.LAYER_HYPSO;
            case RADAR: return DemoConfig.LAYER_RADAR;
            case CUSTOM_OVERLAY: return DemoConfig.LAYER_CUSTOM_OVERLAY;
            case CUSTOM_SCENE: return DemoConfig.LAYER_CUSTOM_SCENE;
            case CONTOUR: return DemoConfig.LAYER_CONTOUR;
            case CONTOUR_TILES: return DemoConfig.LAYER_CONTOUR_TILES;
            case ROUTES: return DemoConfig.LAYER_ROUTES;
            case ROUTE_TEST: return DemoConfig.LAYER_ROUTE_TEST;
            case ROUTE_SELECT: return DemoConfig.LAYER_ROUTE_SELECT;
            case MANEUVERS: return DemoConfig.LAYER_MANEUVERS;
            case ELEMENTS: return DemoConfig.LAYER_ELEMENTS;
            case API_SOURCE: return DemoConfig.LAYER_API_SOURCE;
            case SPANS: return DemoConfig.LAYER_SPANS;
            case BUGS: return DemoConfig.LAYER_BUGS;
            case MODELS: return DemoConfig.LAYER_MODELS;
            case PEAKS: return DemoConfig.LAYER_PEAKS;
            default: return false;
        }
    }

    public void setEnabled(Feature feature, boolean enabled) {
        switch (feature) {
            case CELESTIAL: DemoConfig.CELESTIAL = enabled; break;
            case STARS: DemoConfig.STARS = enabled; break;
            case BASE: DemoConfig.LAYER_BASE = enabled; break;
            case SATELLITE: DemoConfig.LAYER_SATELLITE = enabled; break;
            case HILLSHADE: DemoConfig.LAYER_HILLSHADE = enabled; break;
            case HYPSO: DemoConfig.LAYER_HYPSO = enabled; break;
            case RADAR: DemoConfig.LAYER_RADAR = enabled; break;
            case CUSTOM_OVERLAY: DemoConfig.LAYER_CUSTOM_OVERLAY = enabled; break;
            case CUSTOM_SCENE: DemoConfig.LAYER_CUSTOM_SCENE = enabled; break;
            case CONTOUR: DemoConfig.LAYER_CONTOUR = enabled; break;
            case CONTOUR_TILES: DemoConfig.LAYER_CONTOUR_TILES = enabled; break;
            case ROUTES: DemoConfig.LAYER_ROUTES = enabled; break;
            case ROUTE_TEST: DemoConfig.LAYER_ROUTE_TEST = enabled; break;
            case ROUTE_SELECT: DemoConfig.LAYER_ROUTE_SELECT = enabled; break;
            case MANEUVERS: DemoConfig.LAYER_MANEUVERS = enabled; break;
            case ELEMENTS: DemoConfig.LAYER_ELEMENTS = enabled; break;
            case API_SOURCE: DemoConfig.LAYER_API_SOURCE = enabled; break;
            case BUGS: DemoConfig.LAYER_BUGS = enabled; break;
            case MODELS: DemoConfig.LAYER_MODELS = enabled; break;
            case PEAKS: DemoConfig.LAYER_PEAKS = enabled; break;
        }
        rebuildLayers();
    }

    /** Pushes DemoConfig.RASTER_ZOOM_BIAS to the stand-alone raster layer, if built. */
    public void applyRasterZoomBias() {
        MassifLayer raster = layers.get(Feature.SATELLITE);
        if (raster != null) {
            raster.set("zoomLevelBias", DemoConfig.RASTER_ZOOM_BIAS);
        }
        mapView.requestRender();
    }

    /** Drops the cached instance of a layer so the next rebuild constructs it from the config. */
    public void invalidate(Feature feature) {
        MassifLayer layer = layers.remove(feature);
        if (layer != null) {
            map.remove(layer);
            Massif.destroy("layer", layerId(feature));
        }
    }

    private static String layerId(Feature feature) {
        return "demo-" + feature.name().toLowerCase().replace('_', '-');
    }

    /** Builds a layer from a spec under the feature's id, or null (logged) when it cannot be built. */
    private MassifLayer layer(Feature feature, Spec spec) {
        String id = layerId(feature);
        Massif.destroy("layer", id);
        try {
            return map.buildLayer(id, spec);
        } catch (MassifException e) {
            Log.w(TAG, feature + " layer not built: " + e.getMessage());
            return null;
        }
    }

    /** Registers a source under an id, replacing what an earlier build left there. */
    private MassifSource source(String id, Spec spec) {
        Massif.destroy("source", id);
        return Massif.source(id, spec);
    }

    /** Registers a style (decoder) under an id, replacing what an earlier build left there. */
    private MassifObject style(String id, Spec spec) {
        Massif.destroy("style", id);
        return Massif.style(id, spec);
    }

    /** One call on the map or an object under it, with the result dropped: "light.setSunPositionFromTime". */
    private void callOptions(String method, Object... args) {
        map.call(method, args).close();
    }

    /**
     * Rebuilds the layer list from the config. Layer objects are cached, so switching one off and
     * on again does not refetch anything; use {@link #invalidate} when a layer's own settings
     * changed in a way that needs a new object (style source, base mode).
     */
    public void rebuildLayers() {
        Feature[] order = LAYER_ORDER.clone();
        if (DemoConfig.SATELLITE_UNDER_BASE) {
            List<Feature> list = Arrays.asList(order);
            Collections.swap(list, list.indexOf(Feature.BASE), list.indexOf(Feature.SATELLITE));
        }
        map.clearLayers();
        for (Feature feature : order) {
            if (!isEnabled(feature)) {
                continue;
            }
            MassifLayer layer = layers.get(feature);
            if (layer == null) {
                layer = createLayer(feature);
                if (layer == null) {
                    continue; // missing data file etc - already logged
                }
                layers.put(feature, layer);
            }
            map.add(layer);
        }
        // The sky objects are built with their layer, which happens here, so place them now that
        // they exist.
        updateSky();
        mapView.requestRender();
    }

    /** Puts every sky object where it really is for the configured date, hour and position. */
    public void updateSky() {
        celestial.update();
        double n = DemoAstro.daysSinceJ2000(DemoConfig.SUN_YEAR, DemoConfig.SUN_MONTH, DemoConfig.SUN_DAY, DemoConfig.currentHourUtc());
        stars.update(n, DemoConfig.START_LAT, DemoConfig.START_LON);
        mapView.requestRender();
    }

    private MassifLayer createLayer(Feature feature) {
        switch (feature) {
            case CELESTIAL: return celestial.createLayer(map, context);
            case STARS: return stars.createLayer(map, context);
            case BASE: return createBaseLayer();
            case SATELLITE:
                return layer(feature, Spec.of("raster").set("source", rasterSource().id())
                        .set("zoomLevelBias", DemoConfig.RASTER_ZOOM_BIAS));
            case HILLSHADE: return createHillshadeLayer();
            case HYPSO: return createHypsoLayer();
            case RADAR: return createRadarLayer();
            case CUSTOM_OVERLAY: return createHeatmapLegend();
            case CUSTOM_SCENE: return createHeatmapLayer();
            case CONTOUR: return createContourLayer();
            case CONTOUR_TILES: return createContourTilesLayer();
            case ROUTES: return createRoutesLayer();
            case ROUTE_TEST: return createRouteTestLayer();
            case ROUTE_SELECT: return createRouteSelectLayer();
            case MANEUVERS: return createManeuversLayer();
            case ELEMENTS: return createElementsLayer();
            case API_SOURCE: return createApiSourceLayer();
            case SPANS: return createSpansLayer();
            case BUGS: return createBugsLayer();
            case MODELS: return createModelsLayer();
            case PEAKS: return createPeaksLayer();
            default: return null;
        }
    }

    // --- base map --------------------------------------------------------------------------------

    /**
     * A compiled style's own switches (dir/zip/project). Live: a style parameter is a redraw, not
     * a re-decode, so this is called both when the layer is built and from DemoLive.
     */
    public void applyStyleParameters() {
        setStyleParameter("buildings", DemoConfig.STYLE_BUILDINGS);
        setStyleParameter("deck3d", DemoConfig.STYLE_DECK3D);
        setStyleParameter("building_tilt_drop", DemoConfig.STYLE_TILT_DROP);
        setStyleParameter("building_ao", DemoConfig.STYLE_AO);
        setStyleParameter("building_opacity", DemoConfig.STYLE_BUILDING_OPACITY);
        setStyleParameter("building_edge_radius", DemoConfig.STYLE_EDGE_RADIUS);
        setStyleParameter("building_rounded_roof", DemoConfig.STYLE_ROUNDED_ROOF);
    }

    private void setStyleParameter(String name, String value) {
        if (baseDecoder == null || value.isEmpty()) {
            return;
        }
        try {
            baseDecoder.call("setStyleParameter", name, value).close();
        } catch (MassifException e) {
            Log.w(TAG, "style has no '" + name + "' parameter: " + e.getMessage());
        }
    }

    /**
     * The base map: either a plain VectorTileLayer or a CompositeVectorTileLayer that weaves
     * hillshade / satellite / contour sources into the style's own layer order.
     */
    private MassifLayer createBaseLayer() {
        baseDecoder = style("demo-base-style", DemoStyles.create(context, DemoConfig.STYLE_SOURCE, dataPath));
        applyStyleParameters();
        // see BASE_TILE_CACHE_MB: the SDK default (10MB) is what makes a zoom step blank the map
        boolean composite = DemoConfig.BASE_MODE != DemoConfig.BaseMode.PLAIN;
        Spec spec = Spec.of(composite ? "composite-vector" : "vector")
                .set("source", vectorSource().id())
                .set("style", baseDecoder.id())
                .set("tileCacheCapacity", DemoConfig.BASE_TILE_CACHE_MB * 1024L * 1024L)
                .set("zoomLevelBias", DemoConfig.VECTOR_ZOOM_BIAS);
        if (composite) {
            spec.set("labelRenderOrder", RENDER_ORDER_LAST);
        }
        baseLayer = layer(Feature.BASE, spec);
        compositeLayer = composite ? baseLayer : null;
        if (baseLayer == null) {
            return null;
        }
        highlightModelsOnClick(baseLayer);
        if (composite) {
            syncCompositeSources();
            if (DemoConfig.STYLE_SOURCE == DemoConfig.StyleSource.PROJECT && DemoConfig.PARAM_TOGGLE_INTERVAL_MS > 0) {
                startParamToggleLoop();
            }
        }
        return baseLayer;
    }

    /** Adds/removes the composite slots to match the config. Safe to call at any time. */
    public void syncCompositeSources() {
        if (compositeLayer == null) {
            return;
        }
        // hillshade: the elevation decoder is resolved from the source's 'dem_encoding' meta data.
        if (DemoConfig.COMPOSITE_HILLSHADE) {
            compositeLayer.call("addExternalDataSource", "hillshade", demSource().handle(), COMPOSITE_HILLSHADE).close();
            if (DemoConfig.COMPOSITE_HILLSHADE_ZOOM_BIAS != 0) {
                compositeLayer.call("setExternalDataSourceZoomLevelBias", "hillshade", DemoConfig.COMPOSITE_HILLSHADE_ZOOM_BIAS).close();
            }
        } else {
            compositeLayer.call("removeExternalDataSource", "hillshade").close();
        }
        // satellite: a raster source drawn at the '#satellite' slot with the style's opacity.
        if (DemoConfig.COMPOSITE_SATELLITE) {
            compositeLayer.call("addExternalDataSource", "satellite", rasterSource().handle(), COMPOSITE_RASTER).close();
        } else {
            compositeLayer.call("removeExternalDataSource", "satellite").close();
        }
        // contour: merged INTO the master source, styled by the '#contour' rules.
        if (DemoConfig.COMPOSITE_CONTOUR) {
            compositeLayer.call("addVectorDataSource", "contour", contourSource().handle()).close();
        } else {
            compositeLayer.call("removeExternalDataSource", "contour").close();
        }
        // landform: a SECOND tileset merged in, for a style whose layers do not all come from one.
        if (!DemoConfig.LANDFORM_URL.isEmpty()) {
            compositeLayer.call("addVectorDataSource", DemoConfig.LANDFORM_SLOT, landformSource().handle()).close();
        } else if (cachedLandform != null) {
            compositeLayer.call("removeExternalDataSource", DemoConfig.LANDFORM_SLOT).close();
        }
        checkCompositeSlots();
        mapView.requestRender();
    }

    /**
     * WHY A COMPOSITE SLOT SILENTLY DOES NOTHING - the check to run first.
     *
     * A slot is the position of a style layer with the source's name. If the style does not
     * DECLARE a layer called 'hillshade' / 'satellite' / 'contour' (project.json "layers", or the
     * Layer elements of a Mapnik XML style), the source has nowhere to be drawn and the SDK only
     * warns in the log. The osm/alpimaps style, for instance, declares 'contour' but neither
     * 'hillshade' nor 'satellite', so those two slots do nothing until the style adds them.
     *
     * The result goes to the log AND to {@link #compositeStatus}, which the panel shows.
     */
    public void checkCompositeSlots() {
        if (compositeLayer == null || baseDecoder == null) {
            compositeStatus = "";
            return;
        }
        java.util.List<String> declared = jsonStrings(baseDecoder.getString("styleLayerNames", "[]"));
        java.util.List<String> registered = jsonStrings(compositeLayer.call("getExternalDataSourceNames"));

        StringBuilder status = new StringBuilder();
        boolean missing = false;
        for (String name : registered) {
            boolean ok = declared.contains(name);
            missing |= !ok;
            status.append(status.length() > 0 ? ", " : "").append(name).append(ok ? " OK" : " MISSING in style");
        }
        compositeStatus = status.length() > 0 ? "slots: " + status : "slots: none";
        Log.i(TAG, compositeStatus + " | style layers: " + declared);
        if (missing) {
            // The style simply does not name that layer. A COMPILED Mapnik XML style carries these
            // slots perfectly well - SymbolizerParser/SymbolizerGenerator both handle
            // HillshadeConfigSymbolizer and ContourConfigSymbolizer, and css2xml emits them - so
            // the fix is to ADD the layer, not to abandon the compiled style: put the name in the
            // project.json "layers" array and give it a '#name { hillshade-... }' block, then
            // recompile. A slot whose block sets no property at all resolves as invisible.
            Log.w(TAG, "a slot is missing: the style declares no layer with that name. Add it to "
                    + "the project.json \"layers\" array and give it a '#name { ... }' config block "
                    + "(compiled Mapnik XML supports these too), or switch the panel style to 'inline'");
        }
    }

    /** A JSON array of strings, as the facade returns a list. */
    private static java.util.List<String> jsonStrings(String json) {
        java.util.List<String> list = new java.util.ArrayList<String>();
        try {
            org.json.JSONArray array = new org.json.JSONArray(json);
            for (int i = 0; i < array.length(); i++) {
                list.add(array.getString(i));
            }
        } catch (org.json.JSONException e) {
            Log.w(TAG, "not a list of names: " + json);
        }
        return list;
    }

    private static java.util.List<String> jsonStrings(MassifObject result) {
        try {
            return jsonStrings(result.json());
        } finally {
            result.close();
        }
    }

    /** Rebuilds the base layer: needed after a style-source or base-mode change. */
    public void rebuildBaseLayer() {
        invalidate(Feature.BASE);
        rebuildLayers();
    }

    /**
     * The peak-finder view, in one switch. The pieces are independent SDK features, but each one
     * on its own looks like nothing happens: the shaded surface only shows where NO tile layer
     * paints, and summit names need a view that has summits in it - which a top-down city camera
     * has not. So the mode turns the map layers off, the relief and the names on, and tilts the
     * camera to a panorama (in this SDK tilt 90 is straight down).
     */
    /**
     * Enters the peak-finder view from wherever the map is, as ONE move: the camera flies to the
     * current focus at the panorama's zoom and tilt (MapView.flyTo, which pulls back over a long
     * move and comes down at the target), while the viewpoint climbs to PEAK_FINDER_FLY_ELEVATION
     * and the terrain, the relief and the names come up on the same clock. Everything here is
     * ordinary SDK API driven from the app - the SDK has no peak-finder mode.
     */
    public void flyToPeakFinder() {
        final Position focus = map.camera().position();
        // 3D terrain first, then the mode: the terrain, the relief surface and the names all have
        // the whole flight to load and fade in, instead of appearing when it lands.
        if (!DemoConfig.TERRAIN_ENABLED) {
            DemoConfig.TERRAIN_ENABLED = true;
            applyTerrainOptions();
        }
        setPeakFinderMode(true);
        // The tilt belongs to the flight, so undo what the mode did to it and let the one
        // animation carry it - together with the climb to the viewpoint's elevation.
        map.camera().tilt(savedTilt > 0 ? savedTilt : map.camera().tilt());
        Position target = new Position(focus.lng, focus.lat, DemoConfig.PEAK_FINDER_FLY_ELEVATION);
        map.camera().animate(DemoConfig.PEAK_FINDER_FLY_DURATION).climb(DemoConfig.PEAK_FINDER_FLY_CLIMB)
                .easing(easingName(DemoConfig.PEAK_FINDER_FLY_EASING))
                .moveTo(target, DemoConfig.PEAK_FINDER_FLY_ZOOM, map.camera().rotation(), DemoConfig.PEAK_FINDER_TILT);
        // The viewpoint's height is the flight's now (the target's Z, plus the climb over the way),
        // so the demo only has to follow it: read the flight's own progress rather than run a
        // second clock beside it, and keep the widget's number and the config in step.
        DemoConfig.PEAK_FINDER_ELEVATION = DemoConfig.PEAK_FINDER_FLY_ELEVATION;
        mapView.post(new Runnable() {
            @Override
            public void run() {
                if (map.camera().isMoving()) {
                    DemoPanel.refreshElevationLabel();
                    mapView.postDelayed(this, 16);
                }
            }
        });
    }

    /** FlightEasing as the facade takes it: the CSS name of the curve. */
    private static String easingName(int easing) {
        switch (easing) {
            case 1: return "linear";
            case 2: return "ease-in";
            case 3: return "ease-out";
            case 4: return "ease-in-out";
            default: return "ease";
        }
    }

    /**
     * AR: the relief view over the camera preview. Everything here is an SDK feature the app puts
     * together - a transparent clear colour (the frame becomes a hole), a translucent GL surface
     * (so the hole shows the preview behind it), the sky off, the dark palette, and the device's
     * orientation driving the camera. The terrain, the relief surface and the names stay exactly
     * as they are in the peak-finder view; only what is BEHIND them changes.
     */
    public void setArMode(boolean enabled) {
        DemoConfig.AR_MODE = enabled;
        if (enabled) {
            if (!DemoConfig.PEAK_FINDER) {
                setPeakFinderMode(true);
            }
            saveMapAppearance();
            map.set("clearColor", 0).set("skyColor", 0).set("backgroundBitmap", null);
            if (skyOptions != null) {
                skyOptions.set("enabled", false);
            }
            setReliefDark(true);
            setSurfaceTranslucent(true);
            setCameraPreviewEnabled(DemoConfig.AR_CAMERA);
            setOrientationFollowing(DemoConfig.AR_ORIENTATION);
        } else {
            setOrientationFollowing(false);
            setCameraPreviewEnabled(false);
            setSurfaceTranslucent(false);
            restoreMapAppearance();
            if (skyOptions != null) {
                skyOptions.set("enabled", DemoConfig.SKY_ENABLED);
            }
            setReliefDark(false);
        }
        mapView.requestRender();
    }

    public void setPeakFinderMode(boolean enabled) {
        DemoConfig.PEAK_FINDER = enabled;
        if (enabled) {
            savedLayerBase = DemoConfig.LAYER_BASE;
            savedLayerHillshade = DemoConfig.LAYER_HILLSHADE;
            savedLayerContour = DemoConfig.LAYER_CONTOUR;
            savedLayerContourTiles = DemoConfig.LAYER_CONTOUR_TILES;
            savedLayerSatellite = DemoConfig.LAYER_SATELLITE;
            savedLayerHypso = DemoConfig.LAYER_HYPSO;
            savedTilt = map.camera().tilt();
            DemoConfig.LAYER_BASE = false;
            DemoConfig.LAYER_HILLSHADE = false;
            DemoConfig.LAYER_CONTOUR = false;
            DemoConfig.LAYER_CONTOUR_TILES = false;
            DemoConfig.LAYER_SATELLITE = false;
            DemoConfig.LAYER_HYPSO = false;
            DemoConfig.LAYER_PEAKS = true;
            DemoConfig.RELIEF_SURFACE = true;
            savedOcclusionTolerance = DemoConfig.TERRAIN_OCCLUSION_TOLERANCE;
            // A summit sitting ON a ridge, or a metre behind it, is exactly what the view is for,
            // so the label occlusion is deliberately generous here.
            DemoConfig.TERRAIN_OCCLUSION_TOLERANCE = DemoConfig.PEAK_FINDER_OCCLUSION_TOLERANCE;
            // And a panorama wants the far ranges: tangram's rule stops the ground a few kilometres
            // out, which is most of what the view is about (see TerrainOptions.ViewDistanceFactor).
            savedViewDistance = DemoConfig.VIEW_DISTANCE_FACTOR;
            DemoConfig.VIEW_DISTANCE_FACTOR = DemoConfig.PEAK_FINDER_VIEW_DISTANCE;
            applyTerrainOptions();
            rebuildLayers();
            applyReliefSurface();
            setReliefOutlineEnabled(true);
            map.camera().animate(0.6f).tilt(DemoConfig.PEAK_FINDER_TILT);
            DemoPanel.setElevationWidgetVisible(true);
        } else {
            DemoConfig.LAYER_BASE = savedLayerBase;
            DemoConfig.LAYER_HILLSHADE = savedLayerHillshade;
            DemoConfig.LAYER_CONTOUR = savedLayerContour;
            DemoConfig.LAYER_CONTOUR_TILES = savedLayerContourTiles;
            DemoConfig.LAYER_SATELLITE = savedLayerSatellite;
            DemoConfig.LAYER_HYPSO = savedLayerHypso;
            DemoConfig.LAYER_PEAKS = false;
            DemoConfig.RELIEF_SURFACE = false;
            DemoConfig.TERRAIN_OCCLUSION_TOLERANCE = savedOcclusionTolerance;
            DemoConfig.VIEW_DISTANCE_FACTOR = savedViewDistance;
            DemoConfig.PEAK_FINDER_ELEVATION = 0;
            applyTerrainOptions();
            applyViewpointElevation();
            rebuildLayers();
            applyReliefSurface();
            setReliefOutlineEnabled(false);
            if (savedTilt > 0) {
                map.camera().animate(0.6f).tilt(savedTilt);
            }
            DemoPanel.setElevationWidgetVisible(false);
        }
        mapView.requestRender();
    }

    /**
     * Lifts the viewpoint by {@link DemoConfig#PEAK_FINDER_ELEVATION} metres. The focus position
     * carries a height and the camera rides on it, so raising the focus raises the eye - which is
     * what a peak-finder view wants: see over the ridge in front of you.
     * The z of a MapPos is in INTERNAL units, and one metre is worth more of them the further from
     * the equator (mercator), hence the latitude term.
     */
    public void applyViewpointElevation() {
        Position wgs = map.camera().position();
        double groundElevation = 0;
        if (terrainOptions != null) {
            double sample = getElevation(wgs);
            if (sample > -100000) {
                groundElevation = sample;
            }
        }
        // The projection converts metres to internal units itself (toInternal scales z with x/y),
        // so this stays in METRES - with the mercator stretch the terrain heights also carry, or
        // the viewpoint would sit lower than the mountains it is measured against.
        double meters = (groundElevation + DemoConfig.PEAK_FINDER_ELEVATION) / Math.cos(Math.toRadians(wgs.lat));
        map.camera().animate(0.3f).position(new Position(wgs.lng, wgs.lat, meters));
        mapView.requestRender();
    }

    /** The peak labels are style-driven, so every callout knob needs a new decoder. */
    public void rebuildPeaksLayer() {
        invalidate(Feature.PEAKS);
        rebuildLayers();
    }

    // --- hillshade -------------------------------------------------------------------------------

    private MassifLayer createHillshadeLayer() {
        MassifLayer layer = layer(Feature.HILLSHADE, Spec.of("hillshade").set("source", demSource().id())
                .set("preloading", true)
                .set("tileSubstitutionPolicy", TILE_SUBSTITUTION_VISIBLE)
                .set("tileFilterMode", RASTER_FILTER_BILINEAR));
        hillshadeLayer = layer;
        applyHillshadeConfig();
        return layer;
    }

    /** Pushes every HILLSHADE_* config value onto the stand-alone hillshade layer. */
    /** Debug overlays (DEBUG section of the panel). */
    public void applyDebugConfig() {
        map.set("debugTileBorders", DemoConfig.DEBUG_TILE_BORDERS);
    }

    public void applyHillshadeConfig() {
        MassifLayer layer = hillshadeLayer;
        if (layer == null) {
            return;
        }
        // The illumination direction is a vector; the panel/config express it in degrees. Its z is kept.
        double rad = Math.toRadians(DemoConfig.HILLSHADE_ILLUMINATION_DEGREES);
        double z = 0;
        try {
            z = new org.json.JSONArray(layer.getString("illuminationDirection", "[0,0,0]")).optDouble(2, 0);
        } catch (org.json.JSONException e) {
            Log.w(TAG, "illuminationDirection not readable: " + e.getMessage());
        }
        layer.apply(Spec.object()
                .set("hillshadeMethod", hillshadeMethod())
                .set("contrast", DemoConfig.HILLSHADE_CONTRAST)
                .set("heightScale", DemoConfig.HILLSHADE_HEIGHT_SCALE)
                .set("exaggeration", DemoConfig.HILLSHADE_EXAGGERATION)
                .set("illuminationMapRotationEnabled", DemoConfig.HILLSHADE_ILLUMINATION_FOLLOWS_MAP)
                .set("illuminationDirection", new double[] { Math.sin(rad), Math.cos(rad), z })
                .set("shadowColor", DemoConfig.HILLSHADE_SHADOW_COLOR_ARGB)
                .set("highlightColor", DemoConfig.HILLSHADE_HIGHLIGHT_COLOR_ARGB)
                .set("accentColor", DemoConfig.HILLSHADE_ACCENT_COLOR_ARGB)
                // GPU contour lines drawn in the hillshade pass: the normal map then encodes absolute
                // elevation, and the fragment shader draws anti-aliased lines from it. Unlike the geometry
                // contours below, these are per-fragment at a fixed metre interval (no labels, no CartoCSS).
                .set("contourEnabled", DemoConfig.HILLSHADE_CONTOUR_LINES)
                .set("contourInterval", DemoConfig.HILLSHADE_CONTOUR_INTERVAL)
                .set("contourWidth", DemoConfig.HILLSHADE_CONTOUR_WIDTH)
                .set("contourColor", DemoConfig.HILLSHADE_CONTOUR_COLOR_ARGB)
                // Slope colouring replaces the lighting shader entirely.
                .set("exagerateHeightScaleEnabled", !DemoConfig.HILLSHADE_SLOPES_SHADER)
                .set("normalMapLightingShader", DemoConfig.HILLSHADE_SLOPES_SHADER ? DemoStyles.slopesShader() : ""));
        mapView.requestRender();
    }

    private int hillshadeMethod() {
        switch (DemoConfig.HILLSHADE_METHOD) {
            case "STANDARD": return HILLSHADE_STANDARD;
            case "COMBINED": return HILLSHADE_COMBINED;
            case "MULTIDIRECTIONAL": return HILLSHADE_MULTIDIRECTIONAL;
            case "BASIC": return HILLSHADE_BASIC;
            default: return HILLSHADE_IGOR;
        }
    }

    // --- other layers ----------------------------------------------------------------------------

    /** CustomRasterTileLayer: any filter shader over any raster source (here: hypsometric tint). */
    /**
     * Facade API (#146): a layer whose whole stack - layer, nested sources, style - is ONE JSON
     * spec. Nothing below is constructed here.
     *
     * Only "type" and the constructor arguments are read by a factory; every other key
     * (opacity, visible, capacity, ...) is applied through the generated property table, and the
     * table walks base classes, so a layer's opacity comes from Layer and a cache's capacity from
     * CacheTileDataSource without either being declared on the concrete type.
     *
     * Override it to try another shape, composite-vector included:
     *   --es apiLayerSpec composite      (a preset: raster, composite, solid)
     *   --es apiLayerSpec '{"type":...}'  (raw JSON, if the shell lets the quotes through)
     */
    private MassifLayer createApiSourceLayer() {
        String spec = DemoConfig.apiLayerSpec();
        int handle;
        try {
            handle = MassifApi.create("layer", "demoApiLayer", spec);
        } catch (Exception e) {
            Log.w(TAG, "api create layer failed: " + e.getMessage());
            return null;
        }
        Log.i(TAG, "api create layer -> handle=" + handle + " spec=" + spec);

        // The reuse rule, where it can be seen: the same id with the SAME spec hands back the same
        // object, a different spec under that id is refused rather than replacing it.
        int again = MassifApi.create("layer", "demoApiLayer", spec);
        boolean conflictRefused = false;
        try {
            MassifApi.create("layer", "demoApiLayer", "{\"type\":\"solid\",\"color\":-65536}");
        } catch (Exception e) {
            conflictRefused = true;
        }
        Log.i(TAG, "api reuse=" + (again == handle) + " conflictRefused=" + conflictRefused
                + " opacity=" + MassifApi.getFloat(handle, "opacity", -1));

        return Massif.layer("demoApiLayer");
    }

    private MassifLayer createHypsoLayer() {
        return layer(Feature.HYPSO, Spec.of("custom-raster").set("source", demSource().id())
                .set("shaderSource", DemoStyles.hypsometricShader()));
    }

    private MassifLayer radarLayer;
    private long[] radarTimes = new long[0];
    private float radarPosition;
    private boolean radarDirty;

    /** Told the position, in steps, each frame the radar clock moves; the panel's slider and time follow it. */
    public interface RadarListener {
        void onRadarPosition(float position, int steps);
    }

    public RadarListener radarListener;

    /**
     * The custom-layer heatmap (customlayer/CustomHeatmap, drawn in C): the DEM sampled over an
     * 8 km square around the start position, off the UI thread because the read blocks on tile
     * loading, then handed to the layer. The camera has not flown to its start yet, so it is
     * centred on where the camera will be.
     */
    private MassifLayer createHeatmapLayer() {
        MassifLayer heat = CustomHeatmap.create("demo-custom-scene", true);
        if (heat == null) {
            return null;
        }
        final double lon = DemoConfig.START_LON, lat = DemoConfig.START_LAT;
        new Thread(new Runnable() {
            public void run() {
                double[] positions = CustomHeatmap.gridPositions(lon, lat);
                double exaggeration = terrainOptions != null && DemoConfig.TERRAIN_ENABLED ? terrainOptions.getDouble("exaggeration", 0) : 0;
                double[] heights = new double[positions.length / 2];
                if (exaggeration > 0) {
                    for (int k = 0; k < heights.length; k++) {
                        heights[k] = getElevation(new Position(positions[2 * k], positions[2 * k + 1])) * exaggeration;
                    }
                }
                CustomHeatmap.setHeatmap(lon, lat, heights);
                mapView.requestRender();
            }
        }, "demo-heatmap").start();
        return heat;
    }

    private MassifLayer createHeatmapLegend() {
        return CustomHeatmap.create("demo-custom-overlay", false);
    }

    /**
     * AnimatedRasterTileLayer over LibreWXR (RainViewer's API): the past hour and the nowcast, two
     * steps mixed per draw. The steps are listed once; a frame clock then advances the position at
     * radarFps steps a second, and the panel can pause it or scrub.
     */
    private MassifLayer createRadarLayer() {
        // The memory cache is what keeps a step fetched once: it is the second raster of one tile and the first of the next.
        MassifObject http = source("demo-radar-http", Spec.of("http").set("minZoom", 0).set("maxZoom", 12)
                .set("url", "https://api.librewxr.net/v2/radar/{time}/256/{z}/{x}/{y}/2/1_1.png"));
        MassifObject cache = source("demo-radar-cache", Spec.of("memory-cache").set("source", http.id())
                .set("capacity", 64L * 1024 * 1024));
        // radar is coarse data: a quarter of the tiles to wait for
        final MassifLayer layer = layer(Feature.RADAR, Spec.of("animated-raster").set("source", cache.id()).set("zoomLevelBias", -1f));
        if (layer == null) {
            return null;
        }
        radarLayer = layer;
        new Thread(() -> {
            try {
                java.net.HttpURLConnection connection = (java.net.HttpURLConnection) new java.net.URL("https://api.librewxr.net/public/weather-maps.json").openConnection();
                org.json.JSONObject radar;
                try (InputStream in = connection.getInputStream()) {
                    ByteArrayOutputStream out = new ByteArrayOutputStream();
                    byte[] buffer = new byte[4096];
                    for (int n; (n = in.read(buffer)) > 0; ) {
                        out.write(buffer, 0, n);
                    }
                    radar = new org.json.JSONObject(out.toString("UTF-8")).getJSONObject("radar");
                } finally {
                    connection.disconnect();
                }
                List<Long> times = new java.util.ArrayList<>();
                for (String key : new String[] { "past", "nowcast" }) {
                    org.json.JSONArray steps = radar.optJSONArray(key);
                    for (int i = 0; steps != null && i < steps.length(); i++) {
                        times.add(steps.getJSONObject(i).getLong("time"));
                    }
                }
                final long[] stepTimes = new long[times.size()];
                final String[] timestamps = new String[stepTimes.length];
                for (int i = 0; i < stepTimes.length; i++) {
                    stepTimes[i] = times.get(i);
                    timestamps[i] = String.valueOf(stepTimes[i]);
                }
                new Handler(Looper.getMainLooper()).post(() -> {
                    radarTimes = stepTimes;
                    layer.set("frameTimestamps", timestamps);
                    layer.set("prefetchFrames", stepTimes.length);
                    startRadarClock(layer);
                });
            } catch (Exception e) {
                Log.w(TAG, "radar steps not available: " + e.getMessage());
            }
        }, "radar-steps").start();
        return layer;
    }

    private void startRadarClock(final MassifLayer layer) {
        android.view.Choreographer.getInstance().postFrameCallback(new android.view.Choreographer.FrameCallback() {
            private long lastFrameNanos;

            @Override
            public void doFrame(long frameTimeNanos) {
                if (radarLayer != layer) {
                    return;
                }
                int steps = radarTimes.length;
                if (steps > 0 && (DemoConfig.RADAR_PLAYING || radarDirty)) {
                    if (DemoConfig.RADAR_PLAYING && lastFrameNanos != 0) {
                        radarPosition = (float) ((radarPosition + (frameTimeNanos - lastFrameNanos) / 1e9 * DemoConfig.RADAR_FPS) % steps);
                    }
                    radarDirty = false;
                    layer.set("animationPosition", radarPosition);
                    if (radarListener != null) {
                        radarListener.onRadarPosition(radarPosition, steps);
                    }
                }
                lastFrameNanos = frameTimeNanos;
                android.view.Choreographer.getInstance().postFrameCallback(this);
            }
        });
    }

    /** Scrubs the radar to a fraction of the loop, 0 = oldest step, 1 = last. */
    public void setRadarFraction(float fraction) {
        radarPosition = fraction * Math.max(0, radarTimes.length - 1);
        radarDirty = true;
    }

    /** The clock time and kind of the step the position is on, e.g. "14:30 UTC  forecast". */
    public String radarStepText(float position) {
        if (radarTimes.length == 0) {
            return "loading steps...";
        }
        int step = Math.min(radarTimes.length - 1, Math.max(0, (int) position));
        java.text.SimpleDateFormat format = new java.text.SimpleDateFormat("HH:mm", java.util.Locale.US);
        format.setTimeZone(java.util.TimeZone.getTimeZone("UTC"));
        boolean forecast = radarTimes[step] * 1000L > System.currentTimeMillis();
        return format.format(new java.util.Date(radarTimes[step] * 1000L)) + " UTC" + (forecast ? "  forecast" : "");
    }

    /**
     * Stand-alone contour layer: vector tiles GENERATED on the fly from the shared DEM, styled by
     * its own CartoCSS. This is the geometry path (labels possible, styled per 'div'), as opposed
     * to the shader contours of the hillshade layer.
     */
    private MassifLayer createContourLayer() {
        MassifObject decoder = style("demo-contour-style", Spec.of("mbvt").set("cartocss", Spec.of("cartocss").set("css", DemoStyles.contourStyle())));
        // A stand-in tile of another zoom carries a coarser grid AND a coarser interval, so while the
        // right tile loads the map shows angular chords instead of contours. "none" trades that for
        // empty space until the tile is there.
        Spec spec = Spec.of("vector").set("source", contourSource().id()).set("style", decoder.id())
                .set("maxStandInLevel", DemoConfig.CONTOUR_MAX_OVERZOOM_STANDIN)
                .set("tileCacheCapacity", DemoConfig.CONTOUR_TILE_CACHE_MB * 1024L * 1024L);
        if ("none".equalsIgnoreCase(DemoConfig.CONTOUR_TILE_SUBSTITUTION)) {
            spec.set("tileSubstitutionPolicy", TILE_SUBSTITUTION_NONE);
        } else if ("visible".equalsIgnoreCase(DemoConfig.CONTOUR_TILE_SUBSTITUTION)) {
            spec.set("tileSubstitutionPolicy", TILE_SUBSTITUTION_VISIBLE);
        }
        return layer(Feature.CONTOUR, spec);
    }

    /**
     * PRE-BAKED contour tiles over HTTP, styled with the '#contour' rules of the real style
     * (shared/terrain.less, variables inlined). This is the A/B reference: same rules, same
     * 'ele'/'div' attributes, but geometry baked at zoom 11..14 instead of traced from the DEM.
     */
    private MassifLayer createContourTilesLayer() {
        MassifObject decoder = style("demo-contour-tiles-style", Spec.of("mbvt").set("cartocss", Spec.of("cartocss").set("css", DemoStyles.contourTilesStyle())));
        return layer(Feature.CONTOUR_TILES, Spec.of("vector").set("source", contourTilesSource().id()).set("style", decoder.id()));
    }

    /** Offline vector tiles + packaged style; skipped (with a log) when the files are missing. */
    private MassifLayer createRoutesLayer() {
        Spec styleSpec = DemoStyles.createZipDecoder(dataPath + "/" + DemoConfig.ROUTES_STYLE_ZIP_NAME);
        if (styleSpec == null) {
            return null;
        }
        try {
            MassifObject mbtiles = source("demo-routes-mbtiles", Spec.of("mbtiles").set("path", dataPath + "/" + DemoConfig.ROUTES_MBTILES_NAME));
            MassifObject multi = source("demo-routes-multi", Spec.of("multi"));
            multi.call("add", mbtiles.handle(), "").close();
            return layer(Feature.ROUTES, Spec.of("vector").set("source", multi.id())
                    .set("style", style("demo-routes-style", styleSpec).id()));
        } catch (MassifException e) {
            Log.w(TAG, "routes layer unavailable: " + e.getMessage());
            return null;
        }
    }

    /**
     * The LINE JOIN bench: a synthetic mountain road served as GeoJSON vector tiles and styled with
     * CartoCSS, so it takes the SAME tesselator, shaders and drape path as the base map's roads - a
     * Line vector element would not (LineDrawData is a second, independent tesselator).
     *
     * Casing + fill, as a navigation app draws a route. What to look at, zooming OUT: the outside
     * of a sharp turn (miter needles), the inside of a turn with line-opacity below 1 (the join
     * blends twice where the triangles overlap) and the switchback ends (cap / split joins).
     */
    private MassifLayer createRouteTestLayer() {
        String geoJSON = readRouteTestGeoJSON();
        if (geoJSON == null) {
            return null; // already logged
        }
        MassifObject decoder = style("demo-route-test-style", Spec.of("mbvt").set("cartocss", Spec.of("cartocss").set("css", DemoStyles.routeTestStyle())));
        try {
            MassifSource source = source("demo-route-test-source", Spec.of("geojson").set("minZoom", 0).set("maxZoom", 24)
                    .set("simplifyTolerance", DemoConfig.ROUTE_TEST_SIMPLIFY));
            long importStart = System.nanoTime();
            // A FeatureCollection goes in whole (the many-objects bench); a bare Feature is the route.
            source.setLayerGeoJSON(source.createLayer("route"), geoJSON);
            Log.i(TAG, "route test geojson imported in " + ((System.nanoTime() - importStart) / 1000000L) + " ms");
            return layer(Feature.ROUTE_TEST, Spec.of("vector").set("source", source.id()).set("style", decoder.id()));
        } catch (MassifException e) {
            Log.w(TAG, "route test geojson rejected: " + e.getMessage());
            return null;
        }
    }

    /**
     * The STYLE REGRESSION repros: three synthetic features around the start position, one per
     * reported symptom, styled by DemoStyles.bugStyle. GeoJSON vector tiles rather than vector
     * elements, so everything goes through the SAME decoder, tesselator, label pipeline and
     * shaders the reported styles do.
     *
     *   bugpoints  3 points, each carrying an icon glyph and a short label -> the two-attachment
     *              label case. Drop 'bugLabelSize' to 10 and the label text goes.
     *   bugsel     one line -> the 'back/' instance case. 'bugBackOpacity' -1 removes the property.
     *   bugline    one zigzag -> the translucent-line join case, and the line-label case.
     */
    /**
     * Bridges and tunnels from a COARSER tile than the base map's.
     *
     * A chord is only right while the feature's own portals are in the tile, and a long bridge is
     * cut by the tile at every zoom you would actually look at it from - tiles SHRINK as the zoom
     * rises, so Millau (2.46 km) fits at z13 and below and is split from z14 up. Drawing the spans
     * from a lower zoom keeps them whole; the base map keeps its own detail.
     */
    private MassifLayer createSpansLayer() {
        return layer(Feature.SPANS, Spec.of("vector").set("source", vectorSource().id())
                .set("style", style("demo-spans-style", DemoStyles.createSpanDecoder()).id())
                .set("zoomLevelBias", DemoConfig.SPAN_ZOOM_BIAS));
    }

    /** A tap on a tree or wind turbine of the style's model rules tints that one model; other taps pass. */
    private static void highlightModelsOnClick(final MassifLayer layer) {
        layer.consumeFeatureClick(e -> {
            String cls = e.property("class");
            if (!"poi".equals(e.layerName()) || !("tree".equals(cls) || "wind_turbine".equals(cls))) {
                return false;
            }
            Log.i(TAG, "MODELPICK " + cls + " id " + e.featureId() + " point " + e.featurePosIndex());
            layer.call("setModelHighlight", (int) e.featureId(), e.featurePosIndex(), 0xffff8800).close();
            return true;
        });
    }

    /** Trees in a jittered grid as ONE MultiPoint, as OpenMapTiles packs them, and three turbines. */
    private MassifLayer createModelsLayer() {
        StringBuilder trees = new StringBuilder();
        java.util.Random random = new java.util.Random(131);
        for (int i = 0; i < 24; i++) {
            for (int j = 0; j < 24; j++) {
                double lon = DemoConfig.START_LON - 0.0030 + i * 0.00025 + random.nextGaussian() * 0.00004;
                double lat = DemoConfig.START_LAT - 0.0020 + j * 0.00018 + random.nextGaussian() * 0.00003;
                trees.append(trees.length() > 0 ? "," : "").append('[').append(lon).append(',').append(lat).append(']');
            }
        }
        StringBuilder turbines = new StringBuilder();
        for (int i = 0; i < 3; i++) {
            turbines.append(i > 0 ? "," : "").append("{\"type\":\"Feature\",\"id\":").append(100 + i)
                    .append(",\"properties\":{},\"geometry\":{\"type\":\"Point\",\"coordinates\":[")
                    .append(DemoConfig.START_LON + 0.006 + i * 0.004).append(',').append(DemoConfig.START_LAT + 0.004).append("]}}");
        }
        MassifSource source = source("demo-models-source", Spec.of("geojson").set("minZoom", 0).set("maxZoom", 24));
        try {
            source.setLayerGeoJSON(source.createLayer("trees"), "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\",\"id\":1,"
                    + "\"properties\":{},\"geometry\":{\"type\":\"MultiPoint\",\"coordinates\":[" + trees + "]}}]}");
            source.setLayerGeoJSON(source.createLayer("turbines"), "{\"type\":\"FeatureCollection\",\"features\":[" + turbines + "]}");
        } catch (MassifException e) {
            Log.w(TAG, "model geojson rejected: " + e.getMessage());
            return null;
        }
        final MassifLayer layer = layer(Feature.MODELS, Spec.of("vector").set("source", source.id())
                .set("style", style("demo-models-style", DemoStyles.createModelDecoder()).id()));
        if (layer != null) {
            layer.consumeFeatureClick(e -> {
                Log.i(TAG, "MODELPICK " + e.layerName() + " id " + e.featureId() + " point " + e.featurePosIndex());
                layer.call("setModelHighlight", (int) e.featureId(), e.featurePosIndex(), 0xffff8800).close();
                return true;
            });
        }
        return layer;
    }

    private MassifLayer createBugsLayer() {
        MassifObject decoder = style("demo-bugs-style", DemoStyles.createBugDecoder());
        // No simplification: a join artifact has to be looked at on the vertices the style gave.
        MassifSource source = source("demo-bugs-source", Spec.of("geojson").set("minZoom", 0).set("maxZoom", 24).set("simplifyTolerance", 0));
        try {
            source.setLayerGeoJSON(source.createLayer("bugline"), buildBugLineGeoJSON());
            source.setLayerGeoJSON(source.createLayer("bugsel"), buildBugSelGeoJSON());
            source.setLayerGeoJSON(source.createLayer("bugpoints"), buildBugPointsGeoJSON());
            source.setLayerGeoJSON(source.createLayer("bugbridge"), buildBugSpanGeoJSON("bridge", 0));
            source.setLayerGeoJSON(source.createLayer("bugtunnel"), buildBugSpanGeoJSON("tunnel", 0.0010));
        } catch (MassifException e) {
            Log.w(TAG, "bug repro geojson rejected: " + e.getMessage());
            return null;
        }
        return layer(Feature.BUGS, Spec.of("vector").set("source", source.id()).set("style", decoder.id()));
    }

    /**
     * The synthetic BRIDGE / TUNNEL span: one chord across the Bastille ridge (see
     * DemoConfig.BUG_SPAN_*). Draped, it climbs every metre of the 430 m the ground gains between
     * its ends; that is the "before" both behaviours are judged against.
     *
     * The vertices in BETWEEN are the point. A two-point line is already a straight chord once it
     * leaves the drape bake, so it would show a fix that is not there - real bridge geometry is
     * digitised, and each of those vertices is what currently gets pulled down onto the ground.
     *
     * @param brunnel carried as a feature property, so a style can filter on it the way a
     *                converted MapBox style filters [structure].
     * @param lonOffset moves the tunnel off the bridge so the two are separately readable.
     */
    private String buildBugSpanGeoJSON(String brunnel, double lonOffset) {
        double lon = DemoConfig.BUG_SPAN_LON + lonOffset;
        int n = Math.max(1, DemoConfig.BUG_SPAN_VERTICES);
        StringBuilder coords = new StringBuilder();
        for (int i = 0; i <= n; i++) {
            double lat = DemoConfig.BUG_SPAN_LAT_START
                    + (DemoConfig.BUG_SPAN_LAT_END - DemoConfig.BUG_SPAN_LAT_START) * i / (double) n;
            coords.append(i > 0 ? "," : "").append('[').append(lon).append(',').append(lat).append(']');
        }
        return "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\","
                + "\"properties\":{\"brunnel\":\"" + brunnel + "\"},"
                + "\"geometry\":{\"type\":\"LineString\",\"coordinates\":[" + coords + "]}}]}";
    }

    /** A zigzag across the view: every vertex is a join, which is where the break shows. */
    private String buildBugLineGeoJSON() {
        double lon = DemoConfig.START_LON, lat = DemoConfig.START_LAT - 0.0010;
        StringBuilder coords = new StringBuilder();
        for (int i = 0; i <= 6; i++) {
            coords.append(i > 0 ? "," : "")
                  .append('[').append(lon - 0.0030 + i * 0.0010).append(',')
                  .append(lat + (i % 2 == 0 ? 0.0 : 0.00045)).append(']');
        }
        return "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\","
                + "\"properties\":{\"class\":\"waypointline\",\"text\":\"367 m\"},"
                + "\"geometry\":{\"type\":\"LineString\",\"coordinates\":[" + coords + "]}}]}";
    }

    /** One straight line for the 'back/' instance case - no joins, so only the instance is in play. */
    private String buildBugSelGeoJSON() {
        double lon = DemoConfig.START_LON, lat = DemoConfig.START_LAT + 0.0012;
        return "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\",\"properties\":{\"id\":1},"
                + "\"geometry\":{\"type\":\"LineString\",\"coordinates\":["
                + "[" + (lon - 0.0025) + "," + lat + "],"
                + "[" + (lon + 0.0025) + "," + (lat + 0.0002) + "]]}}]}";
    }

    /** Three points, far enough apart that no culling decision can be mistaken for the bug. */
    private String buildBugPointsGeoJSON() {
        double lon = DemoConfig.START_LON, lat = DemoConfig.START_LAT + 0.0004;
        StringBuilder json = new StringBuilder("{\"type\":\"FeatureCollection\",\"features\":[");
        String[] labels = { "12", "34", "56" };
        for (int i = 0; i < labels.length; i++) {
            json.append(i > 0 ? "," : "")
                .append("{\"type\":\"Feature\",\"properties\":{\"icon\":\"")
                .append(DemoStyles.BUG_ICON_GLYPH).append("\",\"label\":\"").append(labels[i]).append("\"},")
                .append("\"geometry\":{\"type\":\"Point\",\"coordinates\":[")
                .append(lon - 0.0018 + i * 0.0018).append(',').append(lat).append("]}}");
        }
        return json.append("]}").toString();
    }

    /**
     * The SELECTION bench: many routes as GeoJSON vector tiles, one of them selected through a
     * style parameter compared with the feature's own osmid - what every real route style does.
     *
     * Tap a route to select it (or let it cycle with routeSelectCycle). What to watch in logcat is
     * not the paint but the reload: the tile load listener fires again for every visible tile when
     * the change forced the tiles to be decoded again, and stays quiet when it did not.
     */
    private MassifLayer createRouteSelectLayer() {
        Spec styleSpec = DemoStyles.createRouteSelectDecoder();
        if (styleSpec == null) {
            return null; // already logged
        }
        MassifObject decoder = style("demo-route-select-style", styleSpec);
        MassifSource source = source("demo-route-select-source", Spec.of("geojson").set("minZoom", 0).set("maxZoom", 24));
        try {
            source.setLayerGeoJSON(source.createLayer("routes"), buildRouteSelectGeoJSON());
        } catch (MassifException e) {
            Log.w(TAG, "route selection geojson rejected: " + e.getMessage());
            return null;
        }
        MassifLayer layer = layer(Feature.ROUTE_SELECT, Spec.of("vector").set("source", source.id()).set("style", decoder.id()));
        if (layer == null) {
            return null;
        }
        layer.consumeFeatureClick(e -> {
            selectRoute((long) e.propertyDouble("osmid", 0));
            return true;
        });
        routeSelectDecoder = decoder;
        selectRoute(routeSelectId); // the parameter has to carry the current selection into a fresh decoder
        startRouteSelectCycle();
        return layer;
    }

    /** Selects one route by its osmid, which is all a real app does to change the selection. */
    public void selectRoute(long osmid) {
        routeSelectId = osmid;
        if (routeSelectDecoder == null) {
            return;
        }
        long start = System.nanoTime();
        routeSelectDecoder.call("setStyleParameter", DemoConfig.ROUTE_SELECT_PARAMETER, Long.toString(osmid)).close();
        Log.i(TAG, "route " + osmid + " selected in " + ((System.nanoTime() - start) / 1000000.0) + " ms (mode "
                + DemoConfig.ROUTE_SELECT_MODE + ")");
    }

    public void selectNextRoute() {
        selectRoute(routeSelectId % Math.max(1, DemoConfig.ROUTE_SELECT_COUNT) + 1);
    }

    private void startRouteSelectCycle() {
        if (routeSelectCycle != null) {
            mapView.removeCallbacks(routeSelectCycle);
            routeSelectCycle = null;
        }
        if (DemoConfig.ROUTE_SELECT_CYCLE_MS <= 0) {
            return;
        }
        routeSelectCycle = new Runnable() {
            public void run() {
                if (!DemoConfig.LAYER_ROUTE_SELECT || DemoConfig.ROUTE_SELECT_CYCLE_MS <= 0) {
                    return;
                }
                selectNextRoute();
                mapView.postDelayed(this, DemoConfig.ROUTE_SELECT_CYCLE_MS);
            }
        };
        mapView.postDelayed(routeSelectCycle, DemoConfig.ROUTE_SELECT_CYCLE_MS);
    }

    /**
     * A fan of routes around the start position, each one a feature with its own 'osmid'. They
     * overlap near the centre so a tap has something to disambiguate, and every route carries the
     * same vertex count - the geometry a selection change would have to repoint.
     */
    private String buildRouteSelectGeoJSON() {
        int count = Math.max(1, DemoConfig.ROUTE_SELECT_COUNT);
        int vertices = Math.max(2, DemoConfig.ROUTE_SELECT_VERTICES);
        double span = DemoConfig.ROUTE_SELECT_SPAN;
        StringBuilder json = new StringBuilder("{\"type\":\"FeatureCollection\",\"features\":[");
        for (int route = 0; route < count; route++) {
            double angle = Math.PI * route / count;
            double dx = Math.cos(angle) * span, dy = Math.sin(angle) * span;
            if (route > 0) {
                json.append(',');
            }
            json.append("{\"type\":\"Feature\",\"properties\":{\"osmid\":").append(route + 1)
                .append(",\"name\":\"route ").append(route + 1).append("\"},")
                .append("\"geometry\":{\"type\":\"LineString\",\"coordinates\":[");
            for (int i = 0; i < vertices; i++) {
                double t = (double) i / (vertices - 1) - 0.5;
                // a gentle wave along the route, so the line has joins to tesselate
                double wave = Math.sin(t * Math.PI * 6) * span * 0.05;
                double lon = DemoConfig.START_LON + dx * t * 2 - dy * wave;
                double lat = DemoConfig.START_LAT + dy * t * 2 + dx * wave;
                if (i > 0) {
                    json.append(',');
                }
                json.append('[').append(lon).append(',').append(lat).append(']');
            }
            json.append("]}}");
        }
        json.append("]}");
        return json.toString();
    }

    /**
     * The maneuver arrows. The source is served as a normal vector tile layer, so the arrow goes
     * through the same tesselator, drape and terrain occlusion as the base map's roads - and, as a
     * layer of its own, it draws over everything below it in {@link #LAYER_ORDER} and under every
     * Marker (billboards are drawn last, after all layers).
     *
     * In an app whose base map is a CompositeVectorTileLayer, the same source can go INSIDE the
     * style's layer order instead - compositeLayer.addVectorDataSource("maneuver", source) with a
     * 'maneuver' entry in the style's layers array - and then it draws over the roads but under the
     * labels. That needs the style to carry the rules, which the demo's inline CartoCSS does here.
     */
    private MassifLayer createManeuversLayer() {
        MassifObject decoder = style("demo-maneuver-style", Spec.of("mbvt").set("cartocss",
                Spec.of("cartocss").set("css", DemoStyles.maneuverStyle(maneuverHeadPath()))));
        MassifSource source = maneuverSource();
        if (!seededManeuvers) {
            // So the layer shows something without an offline routing package on the device. The
            // routing test overwrites these with the real turns of the route it computes.
            seededManeuvers = true;
            DemoTests.seedManeuverArrows(this);
        }
        return layer(Feature.MANEUVERS, Spec.of("vector").set("source", source.id()).set("style", decoder.id()));
    }

    /**
     * The head outline the maneuver style should use: the explicit path if one was given, else the
     * first 'd' attribute of the selected SVG, else empty for the built-in triangle. Only the path
     * DATA is read - fills, strokes, transforms and the rest of SVG are ignored, because what the
     * renderer wants is a contour, not a picture.
     */
    public String maneuverHeadPath() {
        if (!DemoConfig.MANEUVER_ARROW_PATH.isEmpty()) {
            return DemoConfig.MANEUVER_ARROW_PATH;
        }
        String name = DemoConfig.MANEUVER_ARROW_SVG;
        if (name == null || name.isEmpty()) {
            return "";
        }
        String svg = readDataOrAsset(name.startsWith("asset:") ? name.substring(6) : name);
        if (svg == null) {
            Log.w(TAG, "maneuver svg not readable: " + name);
            return "";
        }
        java.util.regex.Matcher matcher = java.util.regex.Pattern.compile("\\bd\\s*=\\s*\"([^\"]+)\"").matcher(svg);
        if (!matcher.find()) {
            Log.w(TAG, "maneuver svg carries no path: " + name);
            return "";
        }
        // The parser reads a 'd' as written; the spaces only have to go because an intent extra
        // would be cut at the first one, and it costs nothing to normalise here as well.
        return matcher.group(1).replaceAll("\\s+", ",");
    }

    /**
     * The heads to cycle through: the built-in triangle, every .svg pushed to the data directory
     * (adb push my-head.svg /sdcard/alpimaps_mbtiles/), and the one bundled in the APK.
     */
    public java.util.List<String> maneuverHeadChoices() {
        java.util.List<String> choices = new java.util.ArrayList<String>();
        choices.add("");
        File[] files = new File(dataPath).listFiles();
        if (files != null) {
            java.util.Arrays.sort(files);
            for (File file : files) {
                if (file.getName().toLowerCase().endsWith(".svg")) {
                    choices.add(file.getName());
                }
            }
        }
        choices.add("asset:" + DemoConfig.MANEUVER_SVG_ASSET);
        return choices;
    }

    /** Moves to the next head and rebuilds the layer - the style holds the outline, so it must go. */
    public String cycleManeuverHead() {
        java.util.List<String> choices = maneuverHeadChoices();
        int index = choices.indexOf(DemoConfig.MANEUVER_ARROW_SVG);
        DemoConfig.MANEUVER_ARROW_SVG = choices.get((index + 1) % choices.size());
        DemoConfig.MANEUVER_ARROW_PATH = ""; // an explicit path would win over the file
        invalidate(Feature.MANEUVERS);
        rebuildLayers();
        return DemoConfig.MANEUVER_ARROW_SVG.isEmpty() ? "built-in triangle" : DemoConfig.MANEUVER_ARROW_SVG;
    }

    /** The maneuver arrow source, kept across layer rebuilds so the arrows survive a toggle. */
    public MassifSource maneuverSource() {
        if (cachedManeuvers == null) {
            MassifSource source = source("demo-maneuver-source", Spec.of("geojson").set("minZoom", 0).set("maxZoom", 24));
            cachedManeuvers = source;
            try {
                maneuverLayerIndex = source.createLayer("maneuver");
            } catch (MassifException e) {
                Log.w(TAG, "maneuver layer unavailable: " + e.getMessage());
            }
            maneuverBuilder().set("lengthBefore", DemoConfig.MANEUVER_LENGTH_BEFORE).set("lengthAfter", DemoConfig.MANEUVER_LENGTH_AFTER);
        }
        return (MassifSource) cachedManeuvers;
    }

    /** Cuts the arrows; the lengths are its settings, so callers can widen them per arrow. */
    public MassifObject maneuverBuilder() {
        if (maneuverBuilder == null) {
            Massif.destroy("geometry", "demo-maneuver-arrows");
            maneuverBuilder = Massif.object("geometry", "demo-maneuver-arrows", Spec.of("maneuver-arrow"));
        }
        return maneuverBuilder;
    }

    /**
     * Shows one arrow under an id, or removes it when the arrow is null or empty.
     *
     * The source serves a WHOLE layer at a time, so several arrows are kept here and the layer is
     * rebuilt from them - which is all the removed ManeuverArrowDataSource used to do, and belongs
     * in the app rather than in the SDK now that an arrow is a single line feature.
     */
    public void setManeuverArrow(int arrowId, String arrowGeoJson) {
        maneuverSource();
        if (arrowGeoJson == null || arrowGeoJson.isEmpty()) {
            maneuverArrows.remove(arrowId);
        } else {
            maneuverArrows.put(arrowId, arrowGeoJson);
        }
        rebuildManeuverLayer();
    }

    public void clearManeuverArrows() {
        if (maneuverArrows.isEmpty()) {
            return;
        }
        maneuverArrows.clear();
        rebuildManeuverLayer();
    }

    private void rebuildManeuverLayer() {
        if (maneuverLayerIndex < 0) {
            return;
        }
        try {
            org.json.JSONArray features = new org.json.JSONArray();
            for (String arrow : maneuverArrows.values()) {
                org.json.JSONArray part = new org.json.JSONObject(arrow).getJSONArray("features");
                for (int i = 0; i < part.length(); i++) {
                    features.put(part.get(i));
                }
            }
            maneuverSource().setLayerGeoJSON(maneuverLayerIndex,
                    new org.json.JSONObject().put("type", "FeatureCollection").put("features", features).toString());
        } catch (org.json.JSONException | MassifException e) {
            Log.w(TAG, "maneuver arrows rejected: " + e.getMessage());
        }
    }

    /**
     * A real Valhalla route (1200 points around Grenoble): dense source geometry, switchbacks,
     * roundabouts and a handful of near-reversals - which is what a join has to survive, and what
     * a synthetic test line does not reproduce.
     *
     * The DATA DIRECTORY wins over the APK asset, so another route can be tried without rebuilding:
     *   adb push my-route.geojson /sdcard/alpimaps_mbtiles/route-test.geojson
     */
    private String readRouteTestGeoJSON() {
        return readDataOrAsset(DemoConfig.ROUTE_TEST_GEOJSON_NAME);
    }

    /** The data directory wins over the APK asset, so a dataset can be swapped with a push. */
    public String readDataOrAsset(String name) {
        File file = new File(dataPath + "/" + name);
        if (file.exists()) {
            try {
                return readStream(new FileInputStream(file));
            } catch (Exception e) {
                Log.w(TAG, "geojson not readable (" + file + "): " + e.getMessage());
            }
        }
        try {
            return readStream(context.getAssets().open(name));
        } catch (Exception e) {
            Log.w(TAG, "geojson asset unavailable (" + name + "): " + e.getMessage());
            return null;
        }
    }

    private static String readStream(InputStream stream) throws IOException {
        try {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] chunk = new byte[16384];
            int read;
            while ((read = stream.read(chunk)) > 0) {
                out.write(chunk, 0, read);
            }
            return out.toString("UTF-8");
        } finally {
            stream.close();
        }
    }

    /**
     * Summit names as callout labels: their own vector tile layer on the base source, with a
     * peaks-only style (see DemoStyles.peaksStyle). Clicking one reports it through the standard
     * vector tile click path - a callout label is picked where it is DRAWN, at the end of its
     * leader line.
     */
    private MassifLayer createPeaksLayer() {
        // Out of the post-process pass: the relief effect reads the terrain depth but paints over
        // the whole frame, so a ridge line drawn after the names crosses them. Opting the layer out
        // draws it AFTER the effect has resolved (Layer.postProcessed), which is also what keeps
        // the glyphs crisp - they are not resampled by the effect's half-resolution buffer.
        MassifLayer layer = layer(Feature.PEAKS, Spec.of("vector").set("source", vectorSource().id())
                .set("style", style("demo-peaks-style", Spec.of("mbvt").set("cartocss", Spec.of("cartocss").set("css", DemoStyles.peaksStyle()))).id())
                .set("labelRenderOrder", RENDER_ORDER_LAST)
                .set("postProcessed", false));
        if (layer != null) {
            layer.consumeFeatureClick(e -> {
                final String name = e.property("name") + " " + e.property("ele") + " m";
                Log.i(TAG, "peak clicked: " + name);
                mapView.post(new Runnable() {
                    public void run() { android.widget.Toast.makeText(context, name, android.widget.Toast.LENGTH_SHORT).show(); }
                });
                return true;
            });
        }
        return layer;
    }

    /**
     * Test elements draped on the terrain: markers on summits (billboard occlusion test - orbit
     * around a ridge and watch them disappear) and a line crossing the Isere valley (drape-lines
     * and element-vs-terrain depth test).
     */
    private MassifLayer createElementsLayer() {
        MassifSource source = source("demo-elements-source", Spec.of("local").set("projection", Spec.of("EPSG:4326")));
        Massif.destroy("elementstyle", "demo-marker-style");
        Massif.destroy("elementstyle", "demo-line-style");
        MassifObject markerStyle = Massif.object("elementstyle", "demo-marker-style", Spec.of("marker").set("size", 24).set("color", 0xFFFF0000));
        double[][] peaks = {
                { 5.7869, 45.2876 }, // Chamechaude
                { 5.9207, 45.2989 }, // Dent de Crolles
                { 5.5433, 45.1861 }, // Le Moucherotte
                { 5.7247, 45.1988 }, // Bastille above Grenoble
        };
        int n = 0;
        for (double[] p : peaks) {
            String id = "demo-peak-marker-" + (n++);
            Massif.destroy("element", id);
            MassifObject marker = Massif.object("element", id, Spec.of("marker").set("position", p).set("style", markerStyle.id()));
            source.call("add", marker.handle()).close();
        }
        MassifObject lineStyle = Massif.object("elementstyle", "demo-line-style", Spec.of("line").set("width", 8).set("color", 0xFF005AFF));
        Massif.destroy("element", "demo-valley-line");
        MassifObject line = Massif.object("element", "demo-valley-line", Spec.of("line")
                .set("poses", new double[][] { { 5.6800, 45.1600 }, { 5.7247, 45.1927 }, { 5.7869, 45.2876 } })
                .set("style", lineStyle.id()));
        source.call("add", line.handle()).close();
        return layer(Feature.ELEMENTS, Spec.of("elements").set("source", source.id()));
    }

    // =============================================================================================
    // SHARED TILE SOURCES (created once, used by several layers)
    // =============================================================================================

    /**
     * The extra tileset a multi-source style needs. MapTiler's topo draws peaks and volcanoes from
     * a 'landform' tileset while everything else comes from the planet one, and a z13 planet tile
     * carries no peak layer at all - which is why they drew nothing until this existed.
     */
    private MassifObject httpCached(String id, int minZoom, int maxZoom, String url, String cacheDb, long capacityMb, String demEncoding) {
        Spec http = Spec.of("http").set("minZoom", minZoom).set("maxZoom", maxZoom).set("url", url)
                .set("HTTPHeaders", Spec.object().set("User-Agent", DemoConfig.HTTP_USER_AGENT));
        MassifObject httpSource = source(id + "-http", http);
        if (demEncoding != null) {
            httpSource.call("setMetaDataElement", "dem_encoding", demEncoding).close();
        }
        return source(id, Spec.of("persistent-cache").set("source", httpSource.id())
                .set("databasePath", cacheDbPath(cacheDb)).set("capacity", capacityMb * 1024L * 1024L));
    }

    public MassifObject landformSource() {
        if (cachedLandform == null) {
            cachedLandform = httpCached("demo-landform", DemoConfig.LANDFORM_MIN_ZOOM, DemoConfig.LANDFORM_MAX_ZOOM,
                    DemoConfig.LANDFORM_URL, DemoConfig.LANDFORM_CACHE_DB, DemoConfig.PERSISTENT_CACHE_MB, null);
        }
        return cachedLandform;
    }

    /** Master vector tiles of the base map, persistently cached. */
    public MassifObject vectorSource() {
        if (cachedVector == null && !DemoConfig.VECTOR_FILE.isEmpty()) {
            String file = dataPath + "/" + DemoConfig.VECTOR_FILE;
            try {
                cachedVector = source("demo-vector", Spec.of(file.endsWith(".mbtiles") ? "mbtiles" : "pmtiles").set("path", file));
            } catch (MassifException e) {
                Log.w(TAG, "vectorFile not readable, using vectorUrl: " + e.getMessage());
            }
        }
        if (cachedVector == null) {
            cachedVector = httpCached("demo-vector", DemoConfig.VECTOR_MIN_ZOOM, DemoConfig.VECTOR_MAX_ZOOM,
                    DemoConfig.VECTOR_URL, DemoConfig.VECTOR_CACHE_DB, DemoConfig.PERSISTENT_CACHE_MB, null);
        }
        return cachedVector;
    }

    /**
     * The elevation source. EVERYTHING elevation-related uses this one instance (3D terrain,
     * hillshade, contours, hypsometric tint), so each DEM tile is fetched and decoded once.
     */
    public MassifObject demSource() {
        if (cachedDem == null) {
            MassifObject dem = cachedDemSource("demo-dem", DemoConfig.DEM_MIN_ZOOM, DemoConfig.DEM_MAX_ZOOM,
                    DemoConfig.DEM_URL, DemoConfig.DEM_ENCODING, DemoConfig.DEM_CACHE_DB);
            if (!DemoConfig.DEM2_URL.isEmpty()) {
                // Two encodings behind one Ordered source: each tile carries the encoding of the
                // source that answered for it, so terrain/hillshade/contours decode it per tile.
                MassifObject dem2 = cachedDemSource("demo-dem2", DemoConfig.DEM2_MIN_ZOOM, DemoConfig.DEM2_MAX_ZOOM,
                        DemoConfig.DEM2_URL, DemoConfig.DEM2_ENCODING, DemoConfig.DEM2_CACHE_DB);
                dem = source("demo-dem-ordered", Spec.of("ordered").set("source", dem.id()).set("source2", dem2.id()));
            }
            cachedDem = dem;
        }
        return cachedDem;
    }

    /** One HTTP DEM source, tagged with its encoding, behind its own persistent cache. */
    private MassifObject cachedDemSource(String id, int minZoom, int maxZoom, String url, String encoding, String cacheDb) {
        return httpCached(id, minZoom, maxZoom, url, cacheDb, DemoConfig.DEM_PERSISTENT_CACHE_MB, encoding);
    }

    public MassifObject rasterSource() {
        if (cachedRaster == null) {
            cachedRaster = httpCached("demo-raster", DemoConfig.RASTER_MIN_ZOOM, DemoConfig.RASTER_MAX_ZOOM,
                    DemoConfig.RASTER_URL, DemoConfig.RASTER_CACHE_DB, DemoConfig.PERSISTENT_CACHE_MB, null);
        }
        return cachedRaster;
    }

    /** Pre-baked contour vector tiles, persistently cached (Feature.CONTOUR_TILES only). */
    public MassifObject contourTilesSource() {
        if (cachedContourTiles == null) {
            cachedContourTiles = httpCached("demo-contour-tiles", DemoConfig.CONTOUR_TILES_MIN_ZOOM, DemoConfig.CONTOUR_TILES_MAX_ZOOM,
                    DemoConfig.CONTOUR_TILES_URL, DemoConfig.CONTOUR_TILES_CACHE_DB, DemoConfig.PERSISTENT_CACHE_MB, null);
        }
        return cachedContourTiles;
    }

    /** Contours generated from the shared DEM; the same instance feeds layer and composite slot. */
    public MassifObject contourSource() {
        if (contourSource == null) {
            // No encoding here: the contour source reads it off each DEM tile it decodes.
            contourSource = source("demo-contour", Spec.of("contour").set("source", demSource().id()));
            applyContourConfig();
        }
        return contourSource;
    }

    /** Pushes every CONTOUR_* config value onto the shared contour source. */
    public void applyContourConfig() {
        if (contourSource == null) {
            return;
        }
        contourSource.apply(Spec.object()
                .set("baseInterval", DemoConfig.CONTOUR_BASE_INTERVAL)
                // Perf knobs: the DEM is subsampled to at most 'resolution' samples/side before tracing,
                // and geometry is simplified by 'simplifyTolerance' tile pixels.
                .set("resolution", DemoConfig.CONTOUR_RESOLUTION)
                .set("simplifyTolerance", DemoConfig.CONTOUR_SIMPLIFY_TOLERANCE)
                // Fetch neighbour DEM tiles so lines meet across tile boundaries (removes seams).
                .set("seamlessEdgesEnabled", DemoConfig.CONTOUR_SEAMLESS_EDGES)
                // NOTE: in CartoCSS 'zoom' is the TILE zoom. Contour tiles are generated at the DEM zoom,
                // so per-zoom style rules only fire if the DEM source max zoom is high enough.
                .set("minVisibleZoom", DemoConfig.CONTOUR_MIN_VISIBLE_ZOOM)
                .set("maxOverzoomLevel", DemoConfig.CONTOUR_MAX_OVERZOOM)
                // Labels only: the lines come from the hillshade shader, so the tile carries a handful of
                // stubs to lay the text along instead of the traced geometry.
                .set("labelStubsEnabled", DemoConfig.CONTOUR_LABEL_STUBS)
                .set("labelInterval", DemoConfig.CONTOUR_LABEL_INTERVAL));
        // Per-zoom detail: how fine the traced interval is, and how big the tracing grid is. Both
        // are cost knobs and both must match what the style actually draws - see contourWidthByDiv.
        applyContourLadders();
        // Stubs off the terrain's own elevation: no DEM tile of the contour source's own to fetch
        // and decode, which is tangram's arrangement. Same DEM source on both sides, so the labels
        // state the heights the terrain draws.
        contourSource.set("terrainOptions", DemoConfig.CONTOUR_STUBS_FROM_TERRAIN ? terrainObject : null);
        mapView.requestRender();
    }

    /** "maxZoom:value,..." rungs onto the source. -1 as a zoom means every zoom above the others. */
    private void applyContourLadders() {
        if (!DemoConfig.CONTOUR_INTERVAL_LADDER.isEmpty()) {
            contourSource.call("clearIntervalMultipliers").close();
            for (String rung : DemoConfig.CONTOUR_INTERVAL_LADDER.split(",")) {
                String[] parts = rung.split(":");
                if (parts.length == 2) {
                    contourSource.call("setIntervalMultiplier", Integer.parseInt(parts[0].trim()), Float.parseFloat(parts[1].trim())).close();
                }
            }
        }
        if (!DemoConfig.CONTOUR_RESOLUTION_LADDER.isEmpty()) {
            contourSource.call("clearResolutionsForZoom").close();
            for (String rung : DemoConfig.CONTOUR_RESOLUTION_LADDER.split(",")) {
                String[] parts = rung.split(":");
                if (parts.length == 2) {
                    contourSource.call("setResolutionForZoom", Integer.parseInt(parts[0].trim()), Integer.parseInt(parts[1].trim())).close();
                }
            }
        }
    }


    private String cacheDbPath(String name) {
        return context.getExternalFilesDir(null) + "/" + name;
    }

    // =============================================================================================
    // OPTIONS: TERRAIN / LIGHT / SKY
    // =============================================================================================

    /** The tile LOD numbers: a named profile if one was asked for, then the individual knobs. */
    public void applyTileLODConfig() {
        if ("reference".equals(DemoConfig.TILE_LOD_PROFILE)) {
            map.set("tileLODProfile", LOD_PROFILE_REFERENCE);
        } else if ("mobile".equals(DemoConfig.TILE_LOD_PROFILE)) {
            map.set("tileLODProfile", LOD_PROFILE_MOBILE);
        } else if ("desktop".equals(DemoConfig.TILE_LOD_PROFILE)) {
            map.set("tileLODProfile", LOD_PROFILE_DESKTOP);
        } else {
            map.set("tileLODFactor", DemoConfig.TILE_LOD_FACTOR);
            map.set("tileStyleZoomLift", DemoConfig.TILE_STYLE_ZOOM_LIFT);
        }
        map.set("tileLODMaxZoomLevelsOnScreen", DemoConfig.TILE_LOD_LEVELS_ON_SCREEN);
        map.set("tileLODTileCountRatio", DemoConfig.TILE_LOD_COUNT_RATIO);
        if (DemoConfig.FIELD_OF_VIEW_Y > 0) {
            map.set("fieldOfViewY", DemoConfig.FIELD_OF_VIEW_Y);
        }
    }

    /** Plane or sphere. Rebuilds every tile layer's transformer, so the tile caches drop and refill. */
    public void applyGlobeMode() {
        map.set("renderProjectionMode", DemoConfig.GLOBE_MODE ? 1 : 0);
    }

    /** Creates the TerrainOptions on first call, then pushes every terrain value onto it. */
    public void applyTerrainOptions() {
        applyTileLODConfig();
        if (terrainOptions == null) {
            Massif.destroy("options", "demo-terrain");
            terrainObject = Massif.object("options", "demo-terrain", Spec.of("terrain").set("source", demSource().id()));
            map.set("terrainOptions", terrainObject);
            terrainOptions = map.terrain();
        }
        Spec terrain = Spec.object()
                .set("alwaysDecode", DemoConfig.TERRAIN_ALWAYS_DECODE)
                .set("enabled", DemoConfig.TERRAIN_ENABLED)
                .set("exaggeration", DemoConfig.TERRAIN_EXAGGERATION)
                .set("autoFlattenParallax", DemoConfig.AUTO_FLATTEN_PARALLAX)
                .set("autoFlattenTilt", DemoConfig.AUTO_FLATTEN_TILT)
                .set("autoFlattenDuration", DemoConfig.AUTO_FLATTEN_MS / 1000f)
                .set("flattenMode", DemoConfig.TERRAIN_FULL_SWITCH ? FLATTEN_FULL : FLATTEN_RENDER)
                .set("meshResolution", DemoConfig.TERRAIN_MESH_RESOLUTION)
                .set("cameraClearance", DemoConfig.TERRAIN_CAMERA_CLEARANCE)
                .set("drapeFillsEnabled", DemoConfig.TERRAIN_DRAPE_FILLS)
                .set("drapeLinesEnabled", DemoConfig.TERRAIN_DRAPE_LINES)
                .set("bridges3DEnabled", DemoConfig.TERRAIN_BRIDGES_3D)
                .set("noDrapeLayerFilter", DemoConfig.TERRAIN_NO_DRAPE_FILTER)
                .set("drapeResolution", DemoConfig.TERRAIN_DRAPE_RESOLUTION)
                .set("drapeCacheSize", DemoConfig.TERRAIN_DRAPE_CACHE_SIZE)
                .set("drapeWorkingSet", DemoConfig.TERRAIN_DRAPE_WORKING_SET)
                .set("tileEdgeStitchingEnabled", DemoConfig.TERRAIN_TILE_EDGE_STITCHING)
                .set("seamlessTileEdgesEnabled", DemoConfig.TERRAIN_SEAMLESS_TILE_EDGES)
                .set("elevationPrefetchEnabled", DemoConfig.TERRAIN_ELEVATION_PREFETCH)
                .set("billboardOcclusionEnabled", DemoConfig.TERRAIN_BILLBOARD_OCCLUSION)
                .set("billboardOcclusionTolerance", DemoConfig.TERRAIN_OCCLUSION_TOLERANCE)
                .set("textOcclusionOpacity", DemoConfig.TERRAIN_TEXT_OCCLUSION_OPACITY)
                .set("backgroundBitmapEnabled", DemoConfig.TERRAIN_BACKGROUND_BITMAP)
                .set("viewDistanceFactor", DemoConfig.VIEW_DISTANCE_FACTOR)
                .set("maxTileZoomCoarsening", DemoConfig.TERRAIN_MAX_TILE_ZOOM_COARSENING);
        if (!DemoConfig.TERRAIN_FLATTENED.isEmpty()) {
            terrain.set("flattened", Boolean.parseBoolean(DemoConfig.TERRAIN_FLATTENED));
        }
        if (DemoConfig.TERRAIN_MAX_TILE_ZOOM_OFFSET_ENABLED) {
            terrain.set("maxTileZoomOffset", DemoConfig.TERRAIN_MAX_TILE_ZOOM_OFFSET);
        }
        terrainOptions.apply(terrain);
        // Fog and view distance go together: the distance ENDS the ground, the fog is what makes
        // it fade out instead of being cut off. The fog itself lives on FogOptions now.
        applyFogOptions();
        applyReliefSurface();
        mapView.requestRender();
    }

    /**
     * The shaded terrain surface of the relief look. The surface shader replaces the terrain
     * background fill, so it is what shows wherever no tile layer paints - switch the base map
     * off to see it.
     */
    /** The relief palette, in one switch: the shaded surface, the ink lines, the names, the plate
     *  behind them and the sky all come from the same pair of colours (see DemoConfig). The names
     *  need their layer rebuilt because a CartoCSS style bakes its colours in. */
    public void setReliefDark(boolean dark) {
        DemoConfig.RELIEF_DARK = dark;
        applyReliefSurface();
        applyReliefOutlineParameters();
        applySkyOptions();
        rebuildPeaksLayer();
    }

    public static int reliefInk() { return DemoConfig.RELIEF_DARK ? DemoConfig.RELIEF_INK_DARK : DemoConfig.RELIEF_INK_LIGHT; }
    public static int reliefPaper() { return DemoConfig.RELIEF_DARK ? DemoConfig.RELIEF_PAPER_DARK : DemoConfig.RELIEF_PAPER_LIGHT; }
    public static int reliefShade() { return DemoConfig.RELIEF_DARK ? DemoConfig.RELIEF_SHADE_DARK : DemoConfig.RELIEF_SHADE_LIGHT; }
    public static int reliefSky() { return DemoConfig.RELIEF_DARK ? DemoConfig.RELIEF_SKY_DARK : DemoConfig.RELIEF_SKY_LIGHT; }

    public void applyReliefSurface() {
        if (terrainOptions == null) {
            return;
        }
        terrainOptions.apply(Spec.object()
            .set("surfaceShaderSource", DemoConfig.RELIEF_SURFACE ? DemoStyles.reliefSurfaceShader() : "")
            .set("surfaceColorParams.uPaperColor", reliefPaper())
            .set("surfaceColorParams.uShadeColor", reliefShade())
            .set("surfaceParams.uShadeStrength", DemoConfig.RELIEF_SHADE_STRENGTH)
            .set("surfaceParams.uAmbient", DemoConfig.RELIEF_AMBIENT)
            .set("surfaceParams.uHaze", DemoConfig.RELIEF_HAZE)
            .set("surfaceParams.uHazeDistance", DemoConfig.RELIEF_HAZE_DISTANCE));
        mapView.requestRender();
    }

    /** Creates the LightOptions on first call, then pushes every sun/shadow value onto it. */
    public void applyLightOptions() {
        if (lightOptions == null) {
            lightOptions = map.light(Spec.of("light"));
        }
        if (DemoConfig.SUN_HOUR_UTC >= 0) {
            int hour = (int) DemoConfig.SUN_HOUR_UTC;
            int minute = (int) ((DemoConfig.SUN_HOUR_UTC - hour) * 60);
            callOptions("light.setSunPositionFromTime", DemoConfig.SUN_YEAR, DemoConfig.SUN_MONTH, DemoConfig.SUN_DAY,
                    hour, minute, DemoConfig.START_LAT, DemoConfig.START_LON);
        } else {
            lightOptions.set("sunAzimuth", DemoConfig.SUN_AZIMUTH);
            lightOptions.set("sunAltitude", DemoConfig.SUN_ALTITUDE);
        }
        lightOptions.apply(Spec.object()
                .set("sunOverridingStyle", DemoConfig.APP_SUN)
                .set("dayCycleLightsEnabled", DemoConfig.DAY_CYCLE_LIGHTS)
                .set("sunIntensity", DemoConfig.SUN_INTENSITY)
                .set("ambientIntensity", DemoConfig.AMBIENT_INTENSITY)
                .set("ambientColor", DemoConfig.AMBIENT_COLOR_ARGB)
                .set("sunColor", DemoConfig.SUN_COLOR_ARGB)
                .set("terrainLightingEnabled", DemoConfig.TERRAIN_LIGHTING)
                .set("terrainShadowCastingEnabled", DemoConfig.TERRAIN_SHADOWS)
                .set("shadowFollowsBuildingHeight", DemoConfig.SHADOW_FOLLOWS_HEIGHT)
                .set("shadowStrength", DemoConfig.SHADOW_STRENGTH)
                .set("shadowSoftness", DemoConfig.SHADOW_SOFTNESS)
                .set("shadowMapSize", DemoConfig.SHADOW_MAP_SIZE)
                .set("shadowCascades", DemoConfig.SHADOW_CASCADES)
                .set("shadowBias", DemoConfig.SHADOW_BIAS)
                .set("shadowDistance", DemoConfig.SHADOW_DISTANCE)
                .set("shadowNormalOffset", DemoConfig.SHADOW_NORMAL_OFFSET)
                .set("shadowCasterMargin", DemoConfig.SHADOW_CASTER_MARGIN));
        updateSky();
    }

    /** Fog is its own options object and is independent of the terrain - it fogs a plain 2D map too. */
    public void applyFogOptions() {
        if (fogOptions == null) {
            fogOptions = map.fog(Spec.of("fog"));
        }
        // STYLE mode leaves every value at its default so nothing but the style's Map block can be
        // responsible for what is on screen - the point being to show the style winning, and that
        // it can make the fog zoom-dependent, which the options cannot.
        boolean fromStyle = DemoConfig.FOG_SOURCE_STYLE.equals(DemoConfig.FOG_SOURCE);
        fogOptions.apply(Spec.object()
                .set("enabled", DemoConfig.FOG_ENABLED)
                .set("color", fromStyle ? 0 : DemoConfig.FOG_COLOR_ARGB)
                .set("rangeStart", DemoConfig.FOG_RANGE_START)
                .set("rangeEnd", DemoConfig.FOG_RANGE_END)
                .set("highColor", fromStyle ? 0 : DemoConfig.FOG_HIGH_COLOR_ARGB)
                .set("spaceColor", fromStyle ? 0 : DemoConfig.FOG_SPACE_COLOR_ARGB)
                .set("starIntensity", fromStyle ? 0 : DemoConfig.FOG_STAR_INTENSITY)
                // How much of the sky the haze takes. The SAME term scales the ground, so the two meet at
                // the skyline with no seam at any tilt - there is no angle to reconcile any more.
                .set("horizonBlend", DemoConfig.FOG_HORIZON_BLEND)
                // Peaks poking out of a valley haze (mapbox vertical-range).
                .set("verticalRangeStart", DemoConfig.FOG_VERTICAL_START)
                .set("verticalRangeEnd", DemoConfig.FOG_VERTICAL_END));
        mapView.requestRender();
    }

    /** The sky is always attached so the panel can toggle it live; disabled = no sky at all. */
    public void applySkyOptions() {
        if (skyOptions == null) {
            skyOptions = map.sky(Spec.of("sky"));
        }
        skyOptions.apply(Spec.object()
                .set("enabled", DemoConfig.SKY_ENABLED)
                .set("type", "gradient".equals(DemoConfig.SKY_TYPE) ? SKY_GRADIENT : SKY_ATMOSPHERE)
                .set("quality", "low".equals(DemoConfig.SKY_QUALITY) ? SKY_LOW : "high".equals(DemoConfig.SKY_QUALITY) ? SKY_HIGH : SKY_MEDIUM)
                .set("atmosphereSunIntensity", DemoConfig.SKY_ATMO_SUN)
                .set("atmosphereColor", DemoConfig.SKY_ATMO_COLOR_ARGB)
                .set("haloColor", DemoConfig.SKY_ATMO_HALO_ARGB)
                .set("atmosphereLuminance", DemoConfig.SKY_ATMO_LUMINANCE));
        // In the relief view the sky is part of the palette: a light one over the paper, a night
        // one over the ink. Alpha 0 makes it see-through, which is what an AR overlay wants.
        if (DemoConfig.RELIEF_SURFACE || DemoConfig.PEAK_FINDER) {
            // The generated day-cycle shader owns the sky's colours, so it has to go for the
            // palette's own sky to be visible at all (SkyOptions falls back to the built-in sky).
            if (!DemoConfig.DAY_CYCLE) {
                skyOptions.set("shaderSource", "");
            }
            skyOptions.set("skyColor", reliefSky());
            map.set("skyColor", reliefSky());
        }
        mapView.requestRender();
    }

    /**
     * Day cycle: one hour value drives sun position, sky colours, shadow strength and a generated
     * sky shader. Turning it off restores the plain sky.
     */
    public void applyDayCycle(float hourUtc) {
        DemoConfig.DAY_CYCLE_HOUR = hourUtc;
        updateSky(); // the hour is also what places the sun, the moon and the stars
        if (!DemoConfig.DAY_CYCLE) {
            if (skyOptions != null) {
                skyOptions.set("shaderSource", "");
            }
            return;
        }
        DemoConfig.TERRAIN_LIGHTING = true;
        DemoConfig.SKY_ENABLED = true;
        lightOptions.set("terrainLightingEnabled", true);
        skyOptions.set("enabled", true);
        // The sun is computed for the CURRENT map centre, not the start position.
        Position centre = map.camera().position();
        DemoSky.applyHour(map, hourUtc, centre.lat, centre.lng);
        mapView.requestRender();
    }

    // =============================================================================================
    // CAMERA / DEBUG HELPERS
    // =============================================================================================

    /** Note: setFocusPos expects BASE PROJECTION coordinates, so WGS84 must be converted first. */
    public void applyCamera() {
        applyLookRange();
        map.camera().moveTo(new Position(DemoConfig.START_LON, DemoConfig.START_LAT), DemoConfig.START_ZOOM,
                DemoConfig.START_ROTATION, DemoConfig.START_TILT);
    }

    /**
     * Free roam and how far above the horizon the view may look.
     *
     * A NEGATIVE tilt is the look up: the camera stays where the tilt geometry put it and only the
     * view pitches, so nothing about zoom or the visible tiles changes.
     *
     * A map stops at 30: below that the camera grazes the terrain and at 0 it looks along the
     * ground from under it. Only the two modes that are ABOUT looking up - free roam and the star
     * sky - get the full range.
     */
    public void applyLookRange() {
        boolean lookUp = !"off".equals(DemoConfig.FREE_ROAM_MODE) || DemoConfig.STAR_SKY;
        float minTilt = lookUp ? -Math.max(10f, DemoConfig.LOOK_UP_LIMIT) : 10f;
        map.apply(Spec.object()
                .set("freeRoamMode", freeRoamMode(DemoConfig.FREE_ROAM_MODE))
                .set("panningSpeedMode", panningSpeedMode(DemoConfig.PANNING_SPEED_MODE))
                .set("orbitAroundPivot", DemoConfig.ORBIT_AROUND_PIVOT)
                .set("freeRoamLookSensitivity", DemoConfig.FREE_ROAM_LOOK_SENSITIVITY)
                .set("freeRoamMoveSpeed", DemoConfig.FREE_ROAM_MOVE_SPEED)
                .set("tiltRange", new double[] { minTilt, 90 }));
    }

    /** "map" / "anchored" / "constant" -> the SDK constant. */
    public static int panningSpeedMode(String name) {
        if ("map".equals(name)) {
            return PANNING_SPEED_MAP;
        }
        if ("constant".equals(name)) {
            return PANNING_SPEED_CONSTANT;
        }
        return PANNING_SPEED_ANCHORED;
    }

    /** "off" / "look" / "fps" -> the SDK constant. */
    public static int freeRoamMode(String name) {
        if ("look".equals(name)) {
            return FREE_ROAM_LOOK;
        }
        if ("fps".equals(name)) {
            return FREE_ROAM_FIRST_PERSON;
        }
        return FREE_ROAM_OFF;
    }

    // =============================================================================================
    // STAR SKY: the map removed, the background cleared to nothing, only the sky left
    // =============================================================================================

    private long savedClearColor;
    private long savedSkyColor;
    private int savedBackgroundBitmap;
    private boolean starSkySaved;

    /**
     * Switches the whole map off and leaves the sky.
     *
     * "Not drawn" here means NOT BUILT: the map layers leave the layer list (see isEnabled), the
     * terrain is disabled and the background is cleared to a fully transparent black, so the frame
     * costs an empty map plus the sky objects. The transparency is the point - with a translucent
     * surface, whatever is behind the view (a camera preview) shows through it.
     *
     * The map fades out before it is dropped and fades back in after it returns, so the switch is
     * not a pop.
     */
    public void applyStarSky(final boolean enabled) {
        if (enabled == DemoConfig.STAR_SKY && starSkySaved == enabled) {
            return;
        }
        long duration = (long) Math.max(0f, DemoConfig.STAR_SKY_FADE_MS);
        if (enabled) {
            saveMapAppearance();
            fadeMapLayers(1f, 0f, duration, new Runnable() {
                public void run() {
                    enterStarSky();
                }
            });
        } else {
            leaveStarSky();
            fadeMapLayers(0f, 1f, duration, null);
        }
    }

    private void saveMapAppearance() {
        if (starSkySaved) {
            return;
        }
        savedClearColor = map.options().getLong("clearColor", 0xFFFFFFFFL);
        savedSkyColor = map.options().getLong("skyColor", 0xFFFFFFFFL);
        savedBackgroundBitmap = MassifApi.getObject(map.options().handle(), "backgroundBitmap");
        starSkySaved = true;
    }

    private void applySavedAppearance() {
        map.set("clearColor", savedClearColor).set("skyColor", savedSkyColor);
        MassifApi.setObject(map.options().handle(), "backgroundBitmap", savedBackgroundBitmap);
    }

    /** Puts back what saveMapAppearance kept - the clear colour, the sky and the background. */
    private void restoreMapAppearance() {
        if (!starSkySaved) {
            return;
        }
        applySavedAppearance();
        starSkySaved = false;
    }

    private void enterStarSky() {
        DemoConfig.STAR_SKY = true;
        // Transparent, not black: the frame is then a hole that whatever is behind the surface
        // shows through, and it looks black on its own anyway.
        map.set("clearColor", 0).set("skyColor", 0).set("backgroundBitmap", null);
        if (terrainOptions != null) {
            terrainOptions.set("enabled", false);
        }
        if (skyOptions != null) {
            skyOptions.set("enabled", false);
        }
        setSurfaceTranslucent(DemoConfig.STAR_SKY_TRANSLUCENT);
        setCameraPreviewEnabled(DemoConfig.STAR_SKY_CAMERA);
        rebuildLayers();
        Log.i(TAG, "star sky on: " + map.layerCount() + " layers, clear " + map.options().getLong("clearColor", -1));
        setMapLayerOpacity(1f); // the layers are out of the list now: leave them ready to come back
        applyLookRange();
        setOrientationFollowing(DemoConfig.STAR_SKY_ORIENTATION);
        mapView.requestRender();
    }

    private void leaveStarSky() {
        setOrientationFollowing(false);
        setCameraPreviewEnabled(false);
        DemoConfig.STAR_SKY = false;
        if (starSkySaved) {
            applySavedAppearance();
            starSkySaved = false;
        }
        if (terrainOptions != null) {
            terrainOptions.set("enabled", DemoConfig.TERRAIN_ENABLED);
        }
        if (skyOptions != null) {
            skyOptions.set("enabled", DemoConfig.SKY_ENABLED);
        }
        if (DemoConfig.STAR_SKY_TRANSLUCENT) {
            setSurfaceTranslucent(false);
        }
        setMapLayerOpacity(0f);
        rebuildLayers();
        applyLookRange(); // back to a map: the tilt stops at 30 again
        mapView.requestRender();
    }

    /** Turning the device turns the view, raising it looks up - the negative tilt in action. */
    public void setOrientationFollowing(boolean enabled) {
        DemoConfig.STAR_SKY_ORIENTATION = enabled;
        if (enabled) {
            if (orientation == null) {
                orientation = new DemoOrientation(context, map);
            }
            orientation.start();
        } else if (orientation != null) {
            orientation.stop();
        }
    }

    /**
     * The live camera behind the map: what the transparent clear colour is FOR. Only meaningful
     * with a translucent surface, and only in star sky mode - there is nothing to see through
     * otherwise.
     */
    public void setCameraPreviewEnabled(boolean enabled) {
        DemoConfig.STAR_SKY_CAMERA = enabled;
        if (enabled) {
            if (!(mapView.getParent() instanceof androidx.constraintlayout.widget.ConstraintLayout)) {
                Log.w(TAG, "the map is not in a ConstraintLayout: no place to put the preview");
                return;
            }
            if (cameraPreview == null) {
                cameraPreview = new DemoCameraPreview(context, (androidx.constraintlayout.widget.ConstraintLayout) mapView.getParent());
            }
            cameraPreview.start();
        } else if (cameraPreview != null) {
            cameraPreview.stop();
        }
    }

    /**
     * A translucent GL surface, which is what makes a transparent clear colour visible: the map is
     * then composited over whatever is behind it (with setZOrderMediaOverlay, a camera preview).
     * Without this the transparency is real but the surface is still opaque, so it just looks black.
     */
    private void setSurfaceTranslucent(final boolean translucent) {
        // Touches the view, and the demo builds on a worker thread.
        mapView.post(new Runnable() {
            public void run() {
                try {
                    mapView.setTranslucent(translucent);
                } catch (Exception e) {
                    Log.w(TAG, "could not change the surface format: " + e);
                }
            }
        });
    }

    /** Opacity of every layer that is NOT the sky. */
    private void setMapLayerOpacity(float opacity) {
        for (Map.Entry<Feature, MassifLayer> entry : layers.entrySet()) {
            if (entry.getKey() != Feature.CELESTIAL && entry.getKey() != Feature.STARS) {
                entry.getValue().opacity(opacity);
            }
        }
        mapView.requestRender();
    }

    private void fadeMapLayers(final float from, final float to, long durationMs, final Runnable onEnd) {
        if (durationMs <= 0) {
            setMapLayerOpacity(to);
            if (onEnd != null) {
                onEnd.run();
            }
            return;
        }
        android.animation.ValueAnimator animator = android.animation.ValueAnimator.ofFloat(from, to);
        animator.setDuration(durationMs);
        animator.addUpdateListener(new android.animation.ValueAnimator.AnimatorUpdateListener() {
            public void onAnimationUpdate(android.animation.ValueAnimator a) {
                setMapLayerOpacity(((Float) a.getAnimatedValue()).floatValue());
            }
        });
        if (onEnd != null) {
            animator.addListener(new android.animation.AnimatorListenerAdapter() {
                public void onAnimationEnd(android.animation.Animator a) {
                    onEnd.run();
                }
            });
        }
        animator.start();
    }

    /**
     * '--es anim zoom|pan|rotate|zoomseq' drives a scripted camera move, so animation artifacts
     * (which still frames never show) can be captured with adb screenrecord without touch input.
     */
    private void startScriptedAnimation() {
        final String anim = DemoConfig.ANIM;
        if (anim == null || anim.isEmpty()) {
            return;
        }
        final float duration = DemoConfig.ANIM_DURATION_S;
        handler.postDelayed(new Runnable() {
            public void run() {
                MapCamera camera = map.camera();
                if ("zoom".equals(anim)) {
                    camera.animate(duration).zoom(DemoConfig.START_ZOOM + DemoConfig.ANIM_ZOOM_DELTA);
                } else if ("pan".equals(anim)) {
                    camera.animate(duration).position(new Position(DemoConfig.START_LON + DemoConfig.ANIM_LON_DELTA, DemoConfig.START_LAT + DemoConfig.ANIM_LAT_DELTA));
                } else if ("rotate".equals(anim)) {
                    camera.animate(duration).rotation(DemoConfig.ANIM_ROTATION);
                } else if ("zoomseq".equals(anim)) {
                    // zoom out, back in, out again - each step after the map settled, which is the
                    // repro shape for "stale content stays on screen" bugs.
                    final float zoomOut = DemoConfig.ANIM_ZOOM_OUT;
                    final float zoomIn = DemoConfig.START_ZOOM;
                    final float settle = DemoConfig.ANIM_SETTLE_MS;
                    handler.postDelayed(new Runnable() { public void run() { Log.i("zoomseq", "step1 out " + zoomOut); map.camera().zoom(zoomOut); } }, 0);
                    handler.postDelayed(new Runnable() { public void run() { Log.i("zoomseq", "step2 in " + zoomIn); map.camera().zoom(zoomIn); } }, (long) settle);
                    handler.postDelayed(new Runnable() { public void run() { Log.i("zoomseq", "step3 out " + zoomOut); map.camera().zoom(zoomOut); } }, (long) (2 * settle));
                } else if ("approach".equals(anim)) {
                    // The wrong-scale repro shape: dive close enough for the terrain camera
                    // clearance to engage, pan along the slope, then pull back out.
                    final float close = DemoConfig.ANIM_APPROACH_ZOOM;
                    final float back = DemoConfig.ANIM_ZOOM_OUT;
                    final float settle = DemoConfig.ANIM_SETTLE_MS;
                    final Position panTo = new Position(DemoConfig.START_LON + DemoConfig.ANIM_LON_DELTA, DemoConfig.START_LAT + DemoConfig.ANIM_LAT_DELTA);
                    handler.postDelayed(new Runnable() { public void run() { Log.i("approach", "step1 close " + close); map.camera().animate(duration).zoom(close); } }, 0);
                    handler.postDelayed(new Runnable() { public void run() { Log.i("approach", "step2 pan"); map.camera().animate(duration).position(panTo); } }, (long) settle);
                    handler.postDelayed(new Runnable() { public void run() { Log.i("approach", "step3 out " + back); map.camera().animate(duration).zoom(back); } }, (long) (2 * settle));
                }
            }
        }, (long) DemoConfig.ANIM_DELAY_MS);
    }

    /** PeakFinder-style relief outline post-process effect. */
    public void setReliefOutlineEnabled(boolean enabled) {
        DemoConfig.RELIEF_OUTLINE = enabled;
        MassifObject renderer = map.camera().renderer();
        if (enabled) {
            // The SDK provides the mechanism - an offscreen frame, the packed terrain depth and
            // named parameters - and the app provides the look, as a fragment shader string. There
            // is no relief effect in the SDK.
            Massif.destroy("effect", "demo-relief-outline");
            reliefEffect = Massif.object("effect", "demo-relief-outline",
                    Spec.of("postprocess").set("name", "relief_outline").set("fragmentShader", DemoStyles.reliefOutlineShader()));
            reliefEffect.set("terrainDepthRequired", true);
            applyReliefOutlineParameters();
            renderer.set("postProcessEffect", reliefEffect);
        } else {
            reliefEffect = null;
            renderer.set("postProcessEffect", null);
        }
        mapView.requestRender();
    }

    /** Pushes the outline knobs (and the light/dark palette) onto the attached effect. */
    public void applyReliefOutlineParameters() {
        if (reliefEffect == null) {
            return;
        }
        // The depth texture is half resolution (TerrainRenderer::BUFFER_DOWNSCALE), and the two
        // below are what keep the horizon the boldest line: the silhouette test is relaxed by the
        // grazing angle, and terrain-vs-terrain lines fade with distance while the sky's do not.
        reliefEffect.apply(Spec.object()
            .set("floatParams.uIntensity", 1.0)
            .set("floatParams.uOutlineWidth", DemoConfig.RELIEF_OUTLINE_WIDTH)
            .set("floatParams.uHorizonBoost", DemoConfig.RELIEF_HORIZON_BOOST)
            .set("floatParams.uDepthThreshold", DemoConfig.RELIEF_DEPTH_THRESHOLD)
            .set("floatParams.uCreaseStrength", DemoConfig.RELIEF_CREASE_STRENGTH)
            .set("floatParams.uHaze", DemoConfig.RELIEF_HAZE)
            .set("floatParams.uDepthTexelSize", 2.0)
            .set("floatParams.uGrazingFloor", 0.15)
            .set("floatParams.uDistanceFade", 0.45)
            .set("colorParams.uInkColor", reliefInk())
            .set("colorParams.uPaperColor", reliefPaper()));
        mapView.requestRender();
    }

    /**
     * Demonstrates a 'param::' user setting driving the style at runtime: flips the parameter every
     * PARAM_TOGGLE_INTERVAL_MS, which fades the hillshade slot in and out.
     */
    private void startParamToggleLoop() {
        handler.postDelayed(new Runnable() {
            public void run() {
                if (baseDecoder == null || DemoConfig.STYLE_SOURCE != DemoConfig.StyleSource.PROJECT) {
                    return; // the style was switched away: stop the loop
                }
                paramOn = !paramOn;
                baseDecoder.call("setStyleParameter", DemoStyles.BOOL_PARAMETER, Boolean.toString(paramOn)).close();
                // The colour parameter takes the live path: no tile is decoded for it
                baseDecoder.call("setStyleParameter", DemoStyles.COLOR_PARAMETER, paramOn ? "#9cc3e0" : "#2f6f4f").close();
                // The table parameter is set as JSON; the style reads it per road class with get()
                baseDecoder.call("setStyleParameter", DemoStyles.TABLE_PARAMETER, paramOn
                    ? "{\"motorway\":\"#e27d60\",\"trunk\":\"#f0a868\",\"primary\":\"#d9b382\"}"
                    : "{\"motorway\":\"#7048e8\",\"trunk\":\"#9775fa\",\"primary\":\"#b197fc\"}").close();
                Log.d(TAG, "param " + DemoStyles.BOOL_PARAMETER + "=" + paramOn);
                handler.postDelayed(this, DemoConfig.PARAM_TOGGLE_INTERVAL_MS);
            }
        }, DemoConfig.PARAM_TOGGLE_INTERVAL_MS);
    }

    /** Elevation under a map position; blocks on tile loading, so call it off the UI thread. */
    public double getElevation(Position wgs84Pos) {
        com.massifmaps.api.MassifObject result = map.call("terrain.getElevation", wgs84Pos);
        try {
            return Double.parseDouble(result.json());
        } finally {
            result.close();
        }
    }

    private static int color(int argb) {
        return argb;
    }
    /**
     * Terrain on/off as an EXPAND animation instead of a pop. Enabling flips the flag first and
     * ramps the exaggeration 0 -> target, disabling ramps it to 0 and only then flips the flag, so
     * the tile re-decode that a flag change forces happens while the map is already flat and is not
     * seen. Only the exaggeration moves per frame, and that no longer invalidates the tile cache.
     */
    public void animateTerrain(final boolean enabled) {
        final float target = DemoConfig.TERRAIN_EXAGGERATION;
        final long durationMs = DemoConfig.TERRAIN_ANIM_MS;
        if (durationMs <= 0) {
            terrainOptions.set("enabled", enabled);
            terrainOptions.set("exaggeration", target);
            return;
        }
        if (enabled) {
            // Flat first, and hold the ramp until the terrain-decoded tiles are in. Flipping the
            // flag re-decodes every tile, and the old FLAT ones are tesselated without terrain
            // subdivision: displacing those chords a road straight between its endpoints, which
            // over a valley rides well above the ground - roads in the sky. At exaggeration 0
            // nothing of that is visible, so waiting costs nothing to look at.
            terrainOptions.set("exaggeration", 0f);
            terrainOptions.set("enabled", true);
            startRampWhenTilesLoaded();
            return;
        }
        final android.animation.ValueAnimator animator =
            android.animation.ValueAnimator.ofFloat(enabled ? 0f : target, enabled ? target : 0f);
        animator.setDuration(durationMs);
        animator.setInterpolator(new android.view.animation.DecelerateInterpolator());
        animator.addUpdateListener(new android.animation.ValueAnimator.AnimatorUpdateListener() {
            public void onAnimationUpdate(android.animation.ValueAnimator a) {
                terrainOptions.set("exaggeration", ((Float) a.getAnimatedValue()).floatValue());
            }
        });
        if (!enabled) {
            animator.addListener(new android.animation.AnimatorListenerAdapter() {
                public void onAnimationEnd(android.animation.Animator a) {
                    terrainOptions.set("enabled", false);
                    terrainOptions.set("exaggeration", target);
                }
            });
        }
        animator.start();
    }

    /** Ramps the terrain in once the visible tiles have been re-decoded for it (see animateTerrain). */
    private void startRampWhenTilesLoaded() {
        final float target = DemoConfig.TERRAIN_EXAGGERATION;
        final long durationMs = DemoConfig.TERRAIN_ANIM_MS;
        final android.os.Handler handler = new android.os.Handler(android.os.Looper.getMainLooper());
        final com.massifmaps.api.Subscription[] loaded = new com.massifmaps.api.Subscription[1];
        final Runnable ramp = new Runnable() {
            private boolean done = false;
            public void run() {
                if (done) {
                    return;
                }
                done = true;
                if (loaded[0] != null) {
                    loaded[0].close();
                }
                android.animation.ValueAnimator a = android.animation.ValueAnimator.ofFloat(0f, target);
                a.setDuration(durationMs);
                a.setInterpolator(new android.view.animation.DecelerateInterpolator());
                a.addUpdateListener(new android.animation.ValueAnimator.AnimatorUpdateListener() {
                    public void onAnimationUpdate(android.animation.ValueAnimator anim) {
                        terrainOptions.set("exaggeration", ((Float) anim.getAnimatedValue()).floatValue());
                    }
                });
                a.start();
            }
        };
        if (baseLayer != null) {
            loaded[0] = baseLayer.onTilesLoaded(e -> handler.post(ramp));
        }
        // The listener can not fire when every visible tile is already decoded for the terrain
        // (toggling back and forth), so a timeout is what actually starts it in that case.
        handler.postDelayed(ramp, DemoConfig.TERRAIN_ANIM_TILE_TIMEOUT_MS);
    }

}
