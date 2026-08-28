package com.massifmaps.MassifDemo.demo;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import com.massifmaps.components.LightOptions;
import com.massifmaps.components.Options;
import com.massifmaps.components.SkyOptions;
import com.massifmaps.components.SkyQuality;
import com.massifmaps.components.SkyType;
import com.massifmaps.components.FogOptions;
import com.massifmaps.components.TerrainFlattenMode;
import com.massifmaps.components.TerrainOptions;
import com.massifmaps.api.MassifApi;
import com.massifmaps.api.MassifInterop;
import com.massifmaps.core.MapPos;
import com.massifmaps.core.MapPosVector;
import com.massifmaps.core.MapVec;
import com.massifmaps.core.StringMap;
import com.massifmaps.core.StringVector;
import com.massifmaps.core.Variant;
import com.massifmaps.datasources.ContourTileDataSource;
import com.massifmaps.datasources.GeoJSONVectorTileDataSource;
import com.massifmaps.datasources.HTTPTileDataSource;
import com.massifmaps.datasources.LocalVectorDataSource;
import com.massifmaps.datasources.MBTilesTileDataSource;
import com.massifmaps.datasources.MultiTileDataSource;
import com.massifmaps.datasources.OrderedTileDataSource;
import com.massifmaps.datasources.PersistentCacheTileDataSource;
import com.massifmaps.datasources.TileDataSource;
import com.massifmaps.geometry.FeatureCollection;
import com.massifmaps.geometry.FeatureVector;
import com.massifmaps.geometry.ManeuverArrowBuilder;
import com.massifmaps.graphics.Color;
import com.massifmaps.layers.CompositeSourceType;
import com.massifmaps.layers.CompositeVectorTileLayer;
import com.massifmaps.layers.CustomRasterTileLayer;
import com.massifmaps.layers.HillshadeMethod;
import com.massifmaps.layers.HillshadeRasterTileLayer;
import com.massifmaps.layers.Layer;
import com.massifmaps.layers.LayerVector;
import com.massifmaps.layers.RasterTileFilterMode;
import com.massifmaps.layers.RasterTileLayer;
import com.massifmaps.layers.TileSubstitutionPolicy;
import com.massifmaps.layers.VectorLayer;
import com.massifmaps.layers.VectorTileEventListener;
import com.massifmaps.layers.VectorTileLayer;
import com.massifmaps.layers.VectorTileRenderOrder;
import com.massifmaps.projections.Projection;
import com.massifmaps.renderers.MapRenderer;
import com.massifmaps.renderers.PostProcessEffect;
import com.massifmaps.styles.CartoCSSStyleSet;
import com.massifmaps.styles.LineStyleBuilder;
import com.massifmaps.styles.MarkerStyleBuilder;
import com.massifmaps.ui.MapView;
import com.massifmaps.ui.VectorTileClickInfo;
import com.massifmaps.vectorelements.Line;
import com.massifmaps.vectorelements.Marker;
import com.massifmaps.vectortiles.MBVectorTileDecoder;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.LinkedHashMap;
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
        CELESTIAL, STARS, BASE, SATELLITE, API_SOURCE, HILLSHADE, HYPSO, CONTOUR, CONTOUR_TILES, ROUTES, ROUTE_TEST, ROUTE_SELECT, MANEUVERS, ELEMENTS, BUGS, PEAKS
    }

    /** Bottom -> top draw order. Toggling a layer never reorders the others. */
    private static final Feature[] LAYER_ORDER = {
        // The sky goes FIRST, so the map and the terrain draw over it and a ridge hides what is
        // behind it - which is what a body in the sky should do.
        Feature.CELESTIAL, Feature.STARS,
        Feature.BASE, Feature.SATELLITE, Feature.API_SOURCE, Feature.HILLSHADE, Feature.HYPSO,
        Feature.CONTOUR, Feature.CONTOUR_TILES, Feature.ROUTES, Feature.ROUTE_TEST, Feature.ROUTE_SELECT, Feature.MANEUVERS, Feature.ELEMENTS,
        Feature.BUGS,
        // Last: the summit names go over everything the map draws.
        Feature.PEAKS
    };

    /** Sun, moon and their daily paths - demo content built on the generic celestial API. */
    public final DemoCelestial celestial = new DemoCelestial();
    /** The bright-star catalogue, the constellation figures and the planets - same API. */
    public final DemoStars stars = new DemoStars();
    /** Device orientation driving the camera, for the star sky mode. */
    private DemoOrientation orientation;
    /** Decoder of the route selection bench - the selection is one style parameter on it. */
    private MBVectorTileDecoder routeSelectDecoder;
    private long routeSelectId = 1;
    private Runnable routeSelectCycle;
    /** Live camera preview behind the transparent map, for the star sky mode. */
    private DemoCameraPreview cameraPreview;

    private final Context context;
    public final MapView mapView;
    public final String dataPath;

    // --- map objects the panel needs to reach ---------------------------------------------------
    public TerrainOptions terrainOptions;
    public LightOptions lightOptions;
    public SkyOptions skyOptions;
    public FogOptions fogOptions;
    public HillshadeRasterTileLayer hillshadeLayer;      // stand-alone hillshade layer, when built
    public VectorTileLayer baseLayer;                    // base map layer, whatever the mode
    public CompositeVectorTileLayer compositeLayer;      // same object as baseLayer in COMPOSITE mode
    public MBVectorTileDecoder baseDecoder;              // decoder of the base layer
    public ContourTileDataSource contourSource;          // shared by the layer and the composite slot
    public PostProcessEffect reliefEffect;               // the attached relief outline effect, if any
    // What the peak-finder mode switched off, so leaving it puts the map back as it was.
    private boolean savedLayerBase, savedLayerHillshade, savedLayerContour, savedLayerContourTiles, savedLayerSatellite, savedLayerHypso;
    private float savedTilt;
    private float savedOcclusionTolerance;
    private float savedViewDistance = 1;
    /** Result of the last {@link #checkCompositeSlots()}: which slots the style really has. */
    public String compositeStatus = "";

    private final Map<Feature, Layer> layers = new LinkedHashMap<Feature, Layer>();

    // --- shared, lazily created tile sources -----------------------------------------------------
    private TileDataSource cachedDem;
    private TileDataSource cachedVector;
    private TileDataSource cachedRaster;
    private TileDataSource cachedContourTiles;
    private TileDataSource cachedLandform;
    private GeoJSONVectorTileDataSource cachedManeuvers;
    private int maneuverLayerIndex = -1;
    private final ManeuverArrowBuilder maneuverBuilder = new ManeuverArrowBuilder();
    private final Map<Integer, FeatureCollection> maneuverArrows = new LinkedHashMap<Integer, FeatureCollection>();
    private boolean seededManeuvers;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private boolean paramOn = true;

    public DemoMap(Context context, MapView mapView, String dataPath) {
        this.context = context;
        this.mapView = mapView;
        this.dataPath = dataPath;
    }

    // =============================================================================================
    // BUILD
    // =============================================================================================

    /** Applies the whole {@link DemoConfig} to a fresh map. */
    public void build() {
        // Options first: layers created afterwards pick up the terrain/light state immediately.
        mapView.getOptions().setTileThreadPoolSize(DemoConfig.TILE_THREAD_POOL_SIZE);
        mapView.getOptions().setEnvelopeThreadPoolSize(DemoConfig.TILE_THREAD_POOL_SIZE);
        mapView.getOptions().setTileLODFactor(DemoConfig.TILE_LOD_FACTOR);
        applyDebugConfig();
        applyTerrainOptions();
        applyLightOptions();
        applySkyOptions();

        rebuildLayers();
        applyCamera();

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
                    new MapPos(DemoConfig.START_LON, DemoConfig.START_LAT));
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
            case CONTOUR: return DemoConfig.LAYER_CONTOUR;
            case CONTOUR_TILES: return DemoConfig.LAYER_CONTOUR_TILES;
            case ROUTES: return DemoConfig.LAYER_ROUTES;
            case ROUTE_TEST: return DemoConfig.LAYER_ROUTE_TEST;
            case ROUTE_SELECT: return DemoConfig.LAYER_ROUTE_SELECT;
            case MANEUVERS: return DemoConfig.LAYER_MANEUVERS;
            case ELEMENTS: return DemoConfig.LAYER_ELEMENTS;
            case API_SOURCE: return DemoConfig.LAYER_API_SOURCE;
            case BUGS: return DemoConfig.LAYER_BUGS;
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
            case CONTOUR: DemoConfig.LAYER_CONTOUR = enabled; break;
            case CONTOUR_TILES: DemoConfig.LAYER_CONTOUR_TILES = enabled; break;
            case ROUTES: DemoConfig.LAYER_ROUTES = enabled; break;
            case ROUTE_TEST: DemoConfig.LAYER_ROUTE_TEST = enabled; break;
            case ROUTE_SELECT: DemoConfig.LAYER_ROUTE_SELECT = enabled; break;
            case MANEUVERS: DemoConfig.LAYER_MANEUVERS = enabled; break;
            case ELEMENTS: DemoConfig.LAYER_ELEMENTS = enabled; break;
            case API_SOURCE: DemoConfig.LAYER_API_SOURCE = enabled; break;
            case BUGS: DemoConfig.LAYER_BUGS = enabled; break;
            case PEAKS: DemoConfig.LAYER_PEAKS = enabled; break;
        }
        rebuildLayers();
    }

    /** Drops the cached instance of a layer so the next rebuild constructs it from the config. */
    public void invalidate(Feature feature) {
        layers.remove(feature);
    }

    /**
     * Rebuilds the layer list from the config. Layer objects are cached, so switching one off and
     * on again does not refetch anything; use {@link #invalidate} when a layer's own settings
     * changed in a way that needs a new object (style source, base mode).
     */
    public void rebuildLayers() {
        LayerVector vector = new LayerVector();
        for (Feature feature : LAYER_ORDER) {
            if (!isEnabled(feature)) {
                continue;
            }
            Layer layer = layers.get(feature);
            if (layer == null) {
                layer = createLayer(feature);
                if (layer == null) {
                    continue; // missing data file etc - already logged
                }
                layers.put(feature, layer);
            }
            vector.add(layer);
        }
        mapView.getLayers().setAll(vector);
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

    private Layer createLayer(Feature feature) {
        switch (feature) {
            case CELESTIAL: return celestial.createLayer(mapView);
            case STARS: return stars.createLayer(mapView);
            case BASE: return createBaseLayer();
            case SATELLITE: return new RasterTileLayer(rasterSource());
            case HILLSHADE: return createHillshadeLayer();
            case HYPSO: return createHypsoLayer();
            case CONTOUR: return createContourLayer();
            case CONTOUR_TILES: return createContourTilesLayer();
            case ROUTES: return createRoutesLayer();
            case ROUTE_TEST: return createRouteTestLayer();
            case ROUTE_SELECT: return createRouteSelectLayer();
            case MANEUVERS: return createManeuversLayer();
            case ELEMENTS: return createElementsLayer();
            case API_SOURCE: return createApiSourceLayer();
            case BUGS: return createBugsLayer();
            case PEAKS: return createPeaksLayer();
            default: return null;
        }
    }

    // --- base map --------------------------------------------------------------------------------

    /**
     * The base map: either a plain VectorTileLayer or a CompositeVectorTileLayer that weaves
     * hillshade / satellite / contour sources into the style's own layer order.
     */
    private Layer createBaseLayer() {
        baseDecoder = DemoStyles.create(DemoConfig.STYLE_SOURCE, dataPath);
        // A compiled style's own switch, where it has one - see DemoConfig.STYLE_BUILDINGS.
        if (!DemoConfig.STYLE_BUILDINGS.isEmpty()) {
            try {
                baseDecoder.setStyleParameter("buildings", DemoConfig.STYLE_BUILDINGS);
            } catch (Exception e) {
                Log.w(TAG, "style has no 'buildings' parameter: " + e.getMessage());
            }
        }
        // see BASE_TILE_CACHE_MB: the SDK default (10MB) is what makes a zoom step blank the map
        if (DemoConfig.BASE_MODE == DemoConfig.BaseMode.PLAIN) {
            compositeLayer = null;
            baseLayer = new VectorTileLayer(vectorSource(), baseDecoder);
            baseLayer.setTileCacheCapacity(DemoConfig.BASE_TILE_CACHE_MB * 1024L * 1024L);
            baseLayer.setZoomLevelBias(DemoConfig.VECTOR_ZOOM_BIAS);
            return baseLayer;
        }

        CompositeVectorTileLayer layer = new CompositeVectorTileLayer(vectorSource(), baseDecoder);
        layer.setLabelRenderOrder(VectorTileRenderOrder.VECTOR_TILE_RENDER_ORDER_LAST);
        layer.setSinglePassRenderingEnabled(DemoConfig.COMPOSITE_SINGLE_PASS);
        compositeLayer = layer;
        baseLayer = layer;
        layer.setTileCacheCapacity(DemoConfig.BASE_TILE_CACHE_MB * 1024L * 1024L);
        layer.setZoomLevelBias(DemoConfig.VECTOR_ZOOM_BIAS);
        syncCompositeSources();
        if (DemoConfig.STYLE_SOURCE == DemoConfig.StyleSource.PROJECT && DemoConfig.PARAM_TOGGLE_INTERVAL_MS > 0) {
            startParamToggleLoop();
        }
        return layer;
    }

    /** Adds/removes the composite slots to match the config. Safe to call at any time. */
    public void syncCompositeSources() {
        if (compositeLayer == null) {
            return;
        }
        // hillshade: the elevation decoder is resolved from the source's 'dem_encoding' meta data.
        if (DemoConfig.COMPOSITE_HILLSHADE) {
            compositeLayer.addExternalDataSource("hillshade", demSource(), CompositeSourceType.COMPOSITE_SOURCE_TYPE_HILLSHADE);
            if (DemoConfig.COMPOSITE_HILLSHADE_ZOOM_BIAS != 0) {
                compositeLayer.setExternalDataSourceZoomLevelBias("hillshade", DemoConfig.COMPOSITE_HILLSHADE_ZOOM_BIAS);
            }
        } else {
            compositeLayer.removeExternalDataSource("hillshade");
        }
        // satellite: a raster source drawn at the '#satellite' slot with the style's opacity.
        if (DemoConfig.COMPOSITE_SATELLITE) {
            compositeLayer.addExternalDataSource("satellite", rasterSource(), CompositeSourceType.COMPOSITE_SOURCE_TYPE_RASTER);
        } else {
            compositeLayer.removeExternalDataSource("satellite");
        }
        // contour: merged INTO the master source, styled by the '#contour' rules.
        if (DemoConfig.COMPOSITE_CONTOUR) {
            compositeLayer.addVectorDataSource("contour", contourSource());
        } else {
            compositeLayer.removeExternalDataSource("contour");
        }
        // landform: a SECOND tileset merged in, for a style whose layers do not all come from one.
        if (!DemoConfig.LANDFORM_URL.isEmpty()) {
            compositeLayer.addVectorDataSource(DemoConfig.LANDFORM_SLOT, landformSource());
        } else if (cachedLandform != null) {
            compositeLayer.removeExternalDataSource(DemoConfig.LANDFORM_SLOT);
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
        StringVector styleLayers = baseDecoder.getStyleLayerNames();
        java.util.List<String> declared = new java.util.ArrayList<String>();
        for (int i = 0; i < styleLayers.size(); i++) {
            declared.add(styleLayers.get(i));
        }

        StringVector registered = compositeLayer.getExternalDataSourceNames();
        StringBuilder status = new StringBuilder();
        boolean missing = false;
        for (int i = 0; i < registered.size(); i++) {
            String name = registered.get(i);
            boolean ok = declared.contains(name);
            missing |= !ok;
            status.append(status.length() > 0 ? ", " : "").append(name).append(ok ? " OK" : " MISSING in style");
        }
        compositeStatus = status.length() > 0 ? "slots: " + status : "slots: none";
        Log.i(TAG, compositeStatus + " | style layers: " + declared);
        if (missing) {
            // A COMPILED Mapnik XML style (what a packaged osm.zip / osm folder usually contains)
            // cannot declare these slots at all: the XML symbolizer set has no hillshade/raster
            // config symbolizer, only CartoCSS has. Either ship the style as a CartoCSS PROJECT
            // (project.json "layers" + '#hillshade { hillshade-... }' in the .mss, which
            // DirAssetPackage reads straight from the folder), or use the inline style to test.
            Log.w(TAG, "a slot is missing: the style declares no layer with that name. Compiled "
                    + "Mapnik XML styles cannot declare hillshade/satellite slots - use a CartoCSS "
                    + "project style (project.json + .mss) or switch the panel style to 'inline'");
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
        final MapPos focus = mapView.getFocusPos();
        // 3D terrain first, then the mode: the terrain, the relief surface and the names all have
        // the whole flight to load and fade in, instead of appearing when it lands.
        if (!DemoConfig.TERRAIN_ENABLED) {
            DemoConfig.TERRAIN_ENABLED = true;
            applyTerrainOptions();
        }
        setPeakFinderMode(true);
        // The tilt belongs to the flight, so undo what the mode did to it and let the one
        // animation carry it - together with the climb to the viewpoint's elevation.
        mapView.setTilt(savedTilt > 0 ? savedTilt : mapView.getTilt(), 0);
        MapPos target = new MapPos(focus.getX(), focus.getY(), DemoConfig.PEAK_FINDER_FLY_ELEVATION);
        mapView.flyTo(target, DemoConfig.PEAK_FINDER_FLY_ZOOM, mapView.getRotation(),
                DemoConfig.PEAK_FINDER_TILT, DemoConfig.PEAK_FINDER_FLY_CLIMB,
                DemoConfig.PEAK_FINDER_FLY_DURATION);
        // The viewpoint's height is the flight's now (the target's Z, plus the climb over the way),
        // so the demo only has to follow it: read the flight's own progress rather than run a
        // second clock beside it, and keep the widget's number and the config in step.
        DemoConfig.PEAK_FINDER_ELEVATION = DemoConfig.PEAK_FINDER_FLY_ELEVATION;
        mapView.post(new Runnable() {
            @Override
            public void run() {
                float progress = mapView.getFlightProgress();
                if (progress >= 0) {
                    DemoPanel.refreshElevationLabel();
                    mapView.postDelayed(this, 16);
                }
            }
        });
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
        Options options = mapView.getOptions();
        if (enabled) {
            if (!DemoConfig.PEAK_FINDER) {
                setPeakFinderMode(true);
            }
            saveMapAppearance();
            options.setClearColor(new Color((short) 0, (short) 0, (short) 0, (short) 0));
            options.setSkyColor(new Color((short) 0, (short) 0, (short) 0, (short) 0));
            options.setBackgroundBitmap(null);
            if (skyOptions != null) {
                skyOptions.setEnabled(false);
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
                skyOptions.setEnabled(DemoConfig.SKY_ENABLED);
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
            savedTilt = mapView.getTilt();
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
            mapView.setTilt(DemoConfig.PEAK_FINDER_TILT, 0.6f);
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
                mapView.setTilt(savedTilt, 0.6f);
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
        Projection proj = mapView.getOptions().getBaseProjection();
        MapPos wgs = proj.toWgs84(mapView.getFocusPos());
        double groundElevation = 0;
        if (terrainOptions != null) {
            double sample = terrainOptions.getElevation(wgs);
            if (sample > -100000) {
                groundElevation = sample;
            }
        }
        // The projection converts metres to internal units itself (toInternal scales z with x/y),
        // so this stays in METRES - with the mercator stretch the terrain heights also carry, or
        // the viewpoint would sit lower than the mountains it is measured against.
        double meters = (groundElevation + DemoConfig.PEAK_FINDER_ELEVATION) / Math.cos(Math.toRadians(wgs.getY()));
        mapView.setFocusPos(proj.fromWgs84(new MapPos(wgs.getX(), wgs.getY(), meters)), 0.3f);
        mapView.requestRender();
    }

    /** The peak labels are style-driven, so every callout knob needs a new decoder. */
    public void rebuildPeaksLayer() {
        invalidate(Feature.PEAKS);
        rebuildLayers();
    }

    // --- hillshade -------------------------------------------------------------------------------

    private Layer createHillshadeLayer() {
        HillshadeRasterTileLayer layer = new HillshadeRasterTileLayer(demSource());
        layer.setPreloading(true);
        layer.setTileSubstitutionPolicy(TileSubstitutionPolicy.TILE_SUBSTITUTION_POLICY_VISIBLE);
        layer.setTileFilterMode(RasterTileFilterMode.RASTER_TILE_FILTER_MODE_BILINEAR);
        hillshadeLayer = layer;
        applyHillshadeConfig();
        return layer;
    }

    /** Pushes every HILLSHADE_* config value onto the stand-alone hillshade layer. */
    /** Debug overlays (DEBUG section of the panel). */
    public void applyDebugConfig() {
        mapView.getOptions().setDebugTileBorders(DemoConfig.DEBUG_TILE_BORDERS);
    }

    public void applyHillshadeConfig() {
        HillshadeRasterTileLayer layer = hillshadeLayer;
        if (layer == null) {
            return;
        }
        layer.setHillshadeMethod(hillshadeMethod());
        layer.setContrast(DemoConfig.HILLSHADE_CONTRAST);
        layer.setHeightScale(DemoConfig.HILLSHADE_HEIGHT_SCALE);
        layer.setExaggeration(DemoConfig.HILLSHADE_EXAGGERATION);
        layer.setIlluminationMapRotationEnabled(DemoConfig.HILLSHADE_ILLUMINATION_FOLLOWS_MAP);
        // The illumination direction is a vector; the panel/config express it in degrees.
        double rad = Math.toRadians(DemoConfig.HILLSHADE_ILLUMINATION_DEGREES);
        layer.setIlluminationDirection(new MapVec(Math.sin(rad), Math.cos(rad), layer.getIlluminationDirection().getZ()));
        layer.setShadowColor(color(DemoConfig.HILLSHADE_SHADOW_COLOR_ARGB));
        layer.setHighlightColor(color(DemoConfig.HILLSHADE_HIGHLIGHT_COLOR_ARGB));
        layer.setAccentColor(color(DemoConfig.HILLSHADE_ACCENT_COLOR_ARGB));

        // GPU contour lines drawn in the hillshade pass: the normal map then encodes absolute
        // elevation, and the fragment shader draws anti-aliased lines from it. Unlike the geometry
        // contours below, these are per-fragment at a fixed metre interval (no labels, no CartoCSS).
        layer.setContourEnabled(DemoConfig.HILLSHADE_CONTOUR_LINES);
        layer.setContourInterval(DemoConfig.HILLSHADE_CONTOUR_INTERVAL);
        layer.setContourWidth(DemoConfig.HILLSHADE_CONTOUR_WIDTH);
        layer.setContourColor(color(DemoConfig.HILLSHADE_CONTOUR_COLOR_ARGB));

        // Slope colouring replaces the lighting shader entirely.
        layer.setExagerateHeightScaleEnabled(!DemoConfig.HILLSHADE_SLOPES_SHADER);
        layer.setNormalMapLightingShader(DemoConfig.HILLSHADE_SLOPES_SHADER ? DemoStyles.slopesShader() : "");
        mapView.requestRender();
    }

    private @HillshadeMethod.Value int hillshadeMethod() {
        switch (DemoConfig.HILLSHADE_METHOD) {
            case "STANDARD": return HillshadeMethod.STANDARD;
            case "COMBINED": return HillshadeMethod.COMBINED;
            case "MULTIDIRECTIONAL": return HillshadeMethod.MULTIDIRECTIONAL;
            case "BASIC": return HillshadeMethod.BASIC;
            default: return HillshadeMethod.IGOR;
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
    private Layer createApiSourceLayer() {
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

        return MassifInterop.getLayer("demoApiLayer");
    }

    private Layer createHypsoLayer() {
        CustomRasterTileLayer layer = new CustomRasterTileLayer(demSource());
        layer.setShaderSource(DemoStyles.hypsometricShader());
        return layer;
    }

    /**
     * Stand-alone contour layer: vector tiles GENERATED on the fly from the shared DEM, styled by
     * its own CartoCSS. This is the geometry path (labels possible, styled per 'div'), as opposed
     * to the shader contours of the hillshade layer.
     */
    private Layer createContourLayer() {
        MBVectorTileDecoder decoder = new MBVectorTileDecoder(new CartoCSSStyleSet(DemoStyles.contourStyle()));
        VectorTileLayer layer = new VectorTileLayer(contourSource(), decoder);
        // A stand-in tile of another zoom carries a coarser grid AND a coarser interval, so while the
        // right tile loads the map shows angular chords instead of contours. "none" trades that for
        // empty space until the tile is there.
        layer.setMaxStandInLevel(DemoConfig.CONTOUR_MAX_OVERZOOM_STANDIN);
        layer.setTileCacheCapacity(DemoConfig.CONTOUR_TILE_CACHE_MB * 1024L * 1024L);
        if ("none".equalsIgnoreCase(DemoConfig.CONTOUR_TILE_SUBSTITUTION)) {
            layer.setTileSubstitutionPolicy(TileSubstitutionPolicy.TILE_SUBSTITUTION_POLICY_NONE);
        } else if ("visible".equalsIgnoreCase(DemoConfig.CONTOUR_TILE_SUBSTITUTION)) {
            layer.setTileSubstitutionPolicy(TileSubstitutionPolicy.TILE_SUBSTITUTION_POLICY_VISIBLE);
        }
        return layer;
    }

    /**
     * PRE-BAKED contour tiles over HTTP, styled with the '#contour' rules of the real style
     * (shared/terrain.less, variables inlined). This is the A/B reference: same rules, same
     * 'ele'/'div' attributes, but geometry baked at zoom 11..14 instead of traced from the DEM.
     */
    private Layer createContourTilesLayer() {
        MBVectorTileDecoder decoder = new MBVectorTileDecoder(new CartoCSSStyleSet(DemoStyles.contourTilesStyle()));
        return new VectorTileLayer(contourTilesSource(), decoder);
    }

    /** Offline vector tiles + packaged style; skipped (with a log) when the files are missing. */
    private Layer createRoutesLayer() {
        try {
            MBTilesTileDataSource source = new MBTilesTileDataSource(dataPath + "/" + DemoConfig.ROUTES_MBTILES_NAME);
            MBVectorTileDecoder decoder = DemoStyles.createZipDecoder(dataPath + "/" + DemoConfig.ROUTES_STYLE_ZIP_NAME);
            if (decoder == null) {
                return null;
            }
            MultiTileDataSource multi = new MultiTileDataSource();
            multi.add(source);
            return new VectorTileLayer(multi, decoder);
        } catch (Exception e) {
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
    private Layer createRouteTestLayer() {
        String geoJSON = readRouteTestGeoJSON();
        if (geoJSON == null) {
            return null; // already logged
        }
        MBVectorTileDecoder decoder = new MBVectorTileDecoder(new CartoCSSStyleSet(DemoStyles.routeTestStyle()));
        GeoJSONVectorTileDataSource source = new GeoJSONVectorTileDataSource(0, 24);
        source.setSimplifyTolerance(DemoConfig.ROUTE_TEST_SIMPLIFY);
        try {
            int layerIndex = source.createLayer("route");
            long importStart = System.nanoTime();
            // A FeatureCollection goes in whole (the many-objects bench); a bare Feature is the route.
            if (geoJSON.contains("\"FeatureCollection\"")) {
                source.setLayerGeoJSONString(layerIndex, geoJSON);
            } else {
                source.addGeoJSONStringFeature(layerIndex, geoJSON);
            }
            Log.i(TAG, "route test geojson imported in " + ((System.nanoTime() - importStart) / 1000000L) + " ms");
        } catch (IOException e) {
            Log.w(TAG, "route test geojson rejected: " + e.getMessage());
            return null;
        }
        return new VectorTileLayer(source, decoder);
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
    private Layer createBugsLayer() {
        MBVectorTileDecoder decoder = DemoStyles.createBugDecoder();
        GeoJSONVectorTileDataSource source = new GeoJSONVectorTileDataSource(0, 24);
        // No simplification: a join artifact has to be looked at on the vertices the style gave.
        source.setSimplifyTolerance(0);
        try {
            source.setLayerGeoJSONString(source.createLayer("bugline"), buildBugLineGeoJSON());
            source.setLayerGeoJSONString(source.createLayer("bugsel"), buildBugSelGeoJSON());
            source.setLayerGeoJSONString(source.createLayer("bugpoints"), buildBugPointsGeoJSON());
        } catch (IOException e) {
            Log.w(TAG, "bug repro geojson rejected: " + e.getMessage());
            return null;
        }
        return new VectorTileLayer(source, decoder);
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
    private Layer createRouteSelectLayer() {
        MBVectorTileDecoder decoder = DemoStyles.createRouteSelectDecoder();
        if (decoder == null) {
            return null; // already logged
        }
        GeoJSONVectorTileDataSource source = new GeoJSONVectorTileDataSource(0, 24);
        try {
            int layerIndex = source.createLayer("routes");
            source.setLayerGeoJSONString(layerIndex, buildRouteSelectGeoJSON());
        } catch (IOException e) {
            Log.w(TAG, "route selection geojson rejected: " + e.getMessage());
            return null;
        }
        VectorTileLayer layer = new VectorTileLayer(source, decoder);
        layer.setVectorTileEventListener(new VectorTileEventListener() {
            @Override
            public boolean onVectorTileClicked(VectorTileClickInfo clickInfo) {
                Variant properties = clickInfo.getFeature().getProperties();
                selectRoute((long) properties.getObjectElement("osmid").getLong());
                return true;
            }
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
        routeSelectDecoder.setStyleParameter(DemoConfig.ROUTE_SELECT_PARAMETER, Long.toString(osmid));
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
    private Layer createManeuversLayer() {
        MBVectorTileDecoder decoder = new MBVectorTileDecoder(new CartoCSSStyleSet(DemoStyles.maneuverStyle(maneuverHeadPath())));
        GeoJSONVectorTileDataSource source = maneuverSource();
        if (!seededManeuvers) {
            // So the layer shows something without an offline routing package on the device. The
            // routing test overwrites these with the real turns of the route it computes.
            seededManeuvers = true;
            DemoTests.seedManeuverArrows(this);
        }
        return new VectorTileLayer(source, decoder);
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
    public GeoJSONVectorTileDataSource maneuverSource() {
        if (cachedManeuvers == null) {
            cachedManeuvers = new GeoJSONVectorTileDataSource(0, 24);
            try {
                maneuverLayerIndex = cachedManeuvers.createLayer("maneuver");
            } catch (IOException e) {
                Log.w(TAG, "maneuver layer unavailable: " + e.getMessage());
            }
            maneuverBuilder.setLengthBefore(DemoConfig.MANEUVER_LENGTH_BEFORE);
            maneuverBuilder.setLengthAfter(DemoConfig.MANEUVER_LENGTH_AFTER);
        }
        return cachedManeuvers;
    }

    /** Cuts the arrows; the lengths are its settings, so callers can widen them per arrow. */
    public ManeuverArrowBuilder maneuverBuilder() {
        return maneuverBuilder;
    }

    /**
     * Shows one arrow under an id, or removes it when the arrow is null or empty.
     *
     * The source serves a WHOLE layer at a time, so several arrows are kept here and the layer is
     * rebuilt from them - which is all the removed ManeuverArrowDataSource used to do, and belongs
     * in the app rather than in the SDK now that an arrow is a single line feature.
     */
    public void setManeuverArrow(int arrowId, FeatureCollection arrow) {
        maneuverSource();
        if (arrow == null || arrow.getFeatureCount() == 0) {
            maneuverArrows.remove(arrowId);
        } else {
            maneuverArrows.put(arrowId, arrow);
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
        FeatureVector features = new FeatureVector();
        for (FeatureCollection arrow : maneuverArrows.values()) {
            for (int i = 0; i < arrow.getFeatureCount(); i++) {
                features.add(arrow.getFeature(i));
            }
        }
        try {
            cachedManeuvers.setLayerFeatureCollection(maneuverLayerIndex, null, new FeatureCollection(features));
        } catch (IOException e) {
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
    private Layer createPeaksLayer() {
        VectorTileLayer layer = new VectorTileLayer(vectorSource(), new MBVectorTileDecoder(new CartoCSSStyleSet(DemoStyles.peaksStyle())));
        layer.setLabelRenderOrder(VectorTileRenderOrder.VECTOR_TILE_RENDER_ORDER_LAST);
        // Out of the post-process pass: the relief effect reads the terrain depth but paints over
        // the whole frame, so a ridge line drawn after the names crosses them. Opting the layer out
        // draws it AFTER the effect has resolved (Layer.setPostProcessed), which is also what keeps
        // the glyphs crisp - they are not resampled by the effect's half-resolution buffer.
        layer.setPostProcessed(false);
        layer.setVectorTileEventListener(new VectorTileEventListener() {
            @Override
            public boolean onVectorTileClicked(VectorTileClickInfo clickInfo) {
                Variant properties = clickInfo.getFeature().getProperties();
                final String name = properties.getObjectElement("name").getString()
                        + " " + properties.getObjectElement("ele").toString() + " m";
                Log.i(TAG, "peak clicked: " + name);
                mapView.post(new Runnable() {
                    public void run() { android.widget.Toast.makeText(context, name, android.widget.Toast.LENGTH_SHORT).show(); }
                });
                return true;
            }
        });
        return layer;
    }

    /**
     * Test elements draped on the terrain: markers on summits (billboard occlusion test - orbit
     * around a ridge and watch them disappear) and a line crossing the Isere valley (drape-lines
     * and element-vs-terrain depth test).
     */
    private Layer createElementsLayer() {
        Projection proj = mapView.getOptions().getBaseProjection();
        LocalVectorDataSource source = new LocalVectorDataSource(proj);

        MarkerStyleBuilder markerStyle = new MarkerStyleBuilder();
        markerStyle.setSize(24);
        markerStyle.setColor(new Color((short) 255, (short) 0, (short) 0, (short) 255));
        double[][] peaks = {
                { 5.7869, 45.2876 }, // Chamechaude
                { 5.9207, 45.2989 }, // Dent de Crolles
                { 5.5433, 45.1861 }, // Le Moucherotte
                { 5.7247, 45.1988 }, // Bastille above Grenoble
        };
        for (double[] p : peaks) {
            source.add(new Marker(proj.fromWgs84(new MapPos(p[0], p[1])), markerStyle.buildStyle()));
        }

        LineStyleBuilder lineStyle = new LineStyleBuilder();
        lineStyle.setWidth(8);
        lineStyle.setColor(new Color((short) 0, (short) 90, (short) 255, (short) 255));
        MapPosVector linePoses = new MapPosVector();
        linePoses.add(proj.fromWgs84(new MapPos(5.6800, 45.1600)));
        linePoses.add(proj.fromWgs84(new MapPos(5.7247, 45.1927)));
        linePoses.add(proj.fromWgs84(new MapPos(5.7869, 45.2876)));
        source.add(new Line(linePoses, lineStyle.buildStyle()));

        return new VectorLayer(source);
    }

    // =============================================================================================
    // SHARED TILE SOURCES (created once, used by several layers)
    // =============================================================================================

    /**
     * The extra tileset a multi-source style needs. MapTiler's topo draws peaks and volcanoes from
     * a 'landform' tileset while everything else comes from the planet one, and a z13 planet tile
     * carries no peak layer at all - which is why they drew nothing until this existed.
     */
    public TileDataSource landformSource() {
        if (cachedLandform == null) {
            HTTPTileDataSource source = new HTTPTileDataSource(DemoConfig.LANDFORM_MIN_ZOOM, DemoConfig.LANDFORM_MAX_ZOOM, DemoConfig.LANDFORM_URL);
            source.setHTTPHeaders(userAgentHeaders());
            PersistentCacheTileDataSource cache = new PersistentCacheTileDataSource(source, cacheDbPath(DemoConfig.LANDFORM_CACHE_DB));
            cache.setCapacity(DemoConfig.PERSISTENT_CACHE_MB * 1024L * 1024L);
            cachedLandform = cache;
        }
        return cachedLandform;
    }

    /** Master vector tiles of the base map, persistently cached. */
    public TileDataSource vectorSource() {
        if (cachedVector == null) {
            HTTPTileDataSource source = new HTTPTileDataSource(DemoConfig.VECTOR_MIN_ZOOM, DemoConfig.VECTOR_MAX_ZOOM, DemoConfig.VECTOR_URL);
            source.setHTTPHeaders(userAgentHeaders());
            PersistentCacheTileDataSource cache = new PersistentCacheTileDataSource(source, cacheDbPath(DemoConfig.VECTOR_CACHE_DB));
            cache.setCapacity(DemoConfig.PERSISTENT_CACHE_MB * 1024L * 1024L);
            cachedVector = cache;
        }
        return cachedVector;
    }

    /**
     * The elevation source. EVERYTHING elevation-related uses this one instance (3D terrain,
     * hillshade, contours, hypsometric tint), so each DEM tile is fetched and decoded once.
     */
    public TileDataSource demSource() {
        if (cachedDem == null) {
            TileDataSource dem = cachedDemSource(DemoConfig.DEM_MIN_ZOOM, DemoConfig.DEM_MAX_ZOOM,
                    DemoConfig.DEM_URL, DemoConfig.DEM_ENCODING, DemoConfig.DEM_CACHE_DB);
            if (!DemoConfig.DEM2_URL.isEmpty()) {
                // Two encodings behind one Ordered source: each tile carries the encoding of the
                // source that answered for it, so terrain/hillshade/contours decode it per tile.
                TileDataSource dem2 = cachedDemSource(DemoConfig.DEM2_MIN_ZOOM, DemoConfig.DEM2_MAX_ZOOM,
                        DemoConfig.DEM2_URL, DemoConfig.DEM2_ENCODING, DemoConfig.DEM2_CACHE_DB);
                dem = new OrderedTileDataSource(dem, dem2);
            }
            cachedDem = dem;
        }
        return cachedDem;
    }

    /** One HTTP DEM source, tagged with its encoding, behind its own persistent cache. */
    private TileDataSource cachedDemSource(int minZoom, int maxZoom, String url, String encoding, String cacheDb) {
        HTTPTileDataSource source = new HTTPTileDataSource(minZoom, maxZoom, url);
        source.setMetaDataElement("dem_encoding", new Variant(encoding));
        PersistentCacheTileDataSource cache = new PersistentCacheTileDataSource(source, cacheDbPath(cacheDb));
        cache.setCapacity(DemoConfig.DEM_PERSISTENT_CACHE_MB * 1024L * 1024L);
        return cache;
    }

    public TileDataSource rasterSource() {
        if (cachedRaster == null) {
            HTTPTileDataSource source = new HTTPTileDataSource(DemoConfig.RASTER_MIN_ZOOM, DemoConfig.RASTER_MAX_ZOOM, DemoConfig.RASTER_URL);
            source.setHTTPHeaders(userAgentHeaders());
            PersistentCacheTileDataSource cache = new PersistentCacheTileDataSource(source, cacheDbPath(DemoConfig.RASTER_CACHE_DB));
            cache.setCapacity(DemoConfig.PERSISTENT_CACHE_MB * 1024L * 1024L);
            cachedRaster = cache;
        }
        return cachedRaster;
    }

    /** Pre-baked contour vector tiles, persistently cached (Feature.CONTOUR_TILES only). */
    public TileDataSource contourTilesSource() {
        if (cachedContourTiles == null) {
            HTTPTileDataSource source = new HTTPTileDataSource(DemoConfig.CONTOUR_TILES_MIN_ZOOM, DemoConfig.CONTOUR_TILES_MAX_ZOOM, DemoConfig.CONTOUR_TILES_URL);
            source.setHTTPHeaders(userAgentHeaders());
            PersistentCacheTileDataSource cache = new PersistentCacheTileDataSource(source, cacheDbPath(DemoConfig.CONTOUR_TILES_CACHE_DB));
            cache.setCapacity(DemoConfig.PERSISTENT_CACHE_MB * 1024L * 1024L);
            cachedContourTiles = cache;
        }
        return cachedContourTiles;
    }

    /** Contours generated from the shared DEM; the same instance feeds layer and composite slot. */
    public ContourTileDataSource contourSource() {
        if (contourSource == null) {
            // No encoding here: the contour source reads it off each DEM tile it decodes.
            contourSource = new ContourTileDataSource(demSource());
            applyContourConfig();
        }
        return contourSource;
    }

    /** Pushes every CONTOUR_* config value onto the shared contour source. */
    public void applyContourConfig() {
        if (contourSource == null) {
            return;
        }
        contourSource.setBaseInterval(DemoConfig.CONTOUR_BASE_INTERVAL);
        // Perf knobs: the DEM is subsampled to at most 'resolution' samples/side before tracing,
        // and geometry is simplified by 'simplifyTolerance' tile pixels.
        contourSource.setResolution(DemoConfig.CONTOUR_RESOLUTION);
        contourSource.setSimplifyTolerance(DemoConfig.CONTOUR_SIMPLIFY_TOLERANCE);
        // Fetch neighbour DEM tiles so lines meet across tile boundaries (removes seams).
        contourSource.setSeamlessEdgesEnabled(DemoConfig.CONTOUR_SEAMLESS_EDGES);
        // NOTE: in CartoCSS 'zoom' is the TILE zoom. Contour tiles are generated at the DEM zoom,
        // so per-zoom style rules only fire if the DEM source max zoom is high enough.
        contourSource.setMinVisibleZoom(DemoConfig.CONTOUR_MIN_VISIBLE_ZOOM);
        // Per-zoom detail: how fine the traced interval is, and how big the tracing grid is. Both
        // are cost knobs and both must match what the style actually draws - see contourWidthByDiv.
        applyContourLadders();
        contourSource.setMaxOverzoomLevel(DemoConfig.CONTOUR_MAX_OVERZOOM);
        // Labels only: the lines come from the hillshade shader, so the tile carries a handful of
        // stubs to lay the text along instead of the traced geometry.
        contourSource.setLabelStubsEnabled(DemoConfig.CONTOUR_LABEL_STUBS);
        contourSource.setLabelInterval(DemoConfig.CONTOUR_LABEL_INTERVAL);
        // Stubs off the terrain's own elevation: no DEM tile of the contour source's own to fetch
        // and decode, which is tangram's arrangement. Same DEM source on both sides, so the labels
        // state the heights the terrain draws.
        contourSource.setTerrainOptions(DemoConfig.CONTOUR_STUBS_FROM_TERRAIN ? terrainOptions : null);
        mapView.requestRender();
    }

    /** "maxZoom:value,..." rungs onto the source. -1 as a zoom means every zoom above the others. */
    private void applyContourLadders() {
        if (!DemoConfig.CONTOUR_INTERVAL_LADDER.isEmpty()) {
            contourSource.clearIntervalMultipliers();
            for (String rung : DemoConfig.CONTOUR_INTERVAL_LADDER.split(",")) {
                String[] parts = rung.split(":");
                if (parts.length == 2) {
                    contourSource.setIntervalMultiplier(Integer.parseInt(parts[0].trim()), Float.parseFloat(parts[1].trim()));
                }
            }
        }
        if (!DemoConfig.CONTOUR_RESOLUTION_LADDER.isEmpty()) {
            contourSource.clearResolutionsForZoom();
            for (String rung : DemoConfig.CONTOUR_RESOLUTION_LADDER.split(",")) {
                String[] parts = rung.split(":");
                if (parts.length == 2) {
                    contourSource.setResolutionForZoom(Integer.parseInt(parts[0].trim()), Integer.parseInt(parts[1].trim()));
                }
            }
        }
    }

    private StringMap userAgentHeaders() {
        StringMap headers = new StringMap();
        headers.set("User-Agent", DemoConfig.HTTP_USER_AGENT);
        return headers;
    }

    private String cacheDbPath(String name) {
        return context.getExternalFilesDir(null) + "/" + name;
    }

    // =============================================================================================
    // OPTIONS: TERRAIN / LIGHT / SKY
    // =============================================================================================

    /** Creates the TerrainOptions on first call, then pushes every terrain value onto it. */
    public void applyTerrainOptions() {
        mapView.getOptions().setTileLODFactor(DemoConfig.TILE_LOD_FACTOR);
        mapView.getOptions().setTileLODForeshorteningLimit(DemoConfig.TILE_LOD_GRAZING);
        if (terrainOptions == null) {
            terrainOptions = new TerrainOptions(demSource());
            mapView.getOptions().setTerrainOptions(terrainOptions);
        }
        terrainOptions.setEnabled(DemoConfig.TERRAIN_ENABLED);
        terrainOptions.setExaggeration(DemoConfig.TERRAIN_EXAGGERATION);
        terrainOptions.setAutoFlattenParallax(DemoConfig.AUTO_FLATTEN_PARALLAX);
        terrainOptions.setAutoFlattenTilt(DemoConfig.AUTO_FLATTEN_TILT);
        terrainOptions.setAutoFlattenDuration(DemoConfig.AUTO_FLATTEN_MS / 1000f);
        terrainOptions.setFlattenMode(DemoConfig.TERRAIN_FULL_SWITCH
            ? TerrainFlattenMode.TERRAIN_FLATTEN_MODE_FULL
            : TerrainFlattenMode.TERRAIN_FLATTEN_MODE_RENDER);
        terrainOptions.setMeshResolution(DemoConfig.TERRAIN_MESH_RESOLUTION);
        terrainOptions.setCameraClearance(DemoConfig.TERRAIN_CAMERA_CLEARANCE);
        terrainOptions.setDrapeFillsEnabled(DemoConfig.TERRAIN_DRAPE_FILLS);
        terrainOptions.setDrapeLinesEnabled(DemoConfig.TERRAIN_DRAPE_LINES);
        terrainOptions.setNoDrapeLayerFilter(DemoConfig.TERRAIN_NO_DRAPE_FILTER);
        terrainOptions.setDrapeResolution(DemoConfig.TERRAIN_DRAPE_RESOLUTION);
        terrainOptions.setTileEdgeStitchingEnabled(DemoConfig.TERRAIN_TILE_EDGE_STITCHING);
        terrainOptions.setSeamlessTileEdgesEnabled(DemoConfig.TERRAIN_SEAMLESS_TILE_EDGES);
        terrainOptions.setElevationPrefetchEnabled(DemoConfig.TERRAIN_ELEVATION_PREFETCH);
        terrainOptions.setBillboardOcclusionEnabled(DemoConfig.TERRAIN_BILLBOARD_OCCLUSION);
        terrainOptions.setBillboardOcclusionTolerance(DemoConfig.TERRAIN_OCCLUSION_TOLERANCE);
        terrainOptions.setTextOcclusionOpacity(DemoConfig.TERRAIN_TEXT_OCCLUSION_OPACITY);
        terrainOptions.setBackgroundBitmapEnabled(DemoConfig.TERRAIN_BACKGROUND_BITMAP);
        if (DemoConfig.TERRAIN_MAX_TILE_ZOOM_OFFSET_ENABLED) {
            terrainOptions.setMaxTileZoomOffset(DemoConfig.TERRAIN_MAX_TILE_ZOOM_OFFSET);
        }
        // Fog and view distance go together: the distance ENDS the ground, the fog is what makes
        // it fade out instead of being cut off. The fog itself lives on FogOptions now.
        applyFogOptions();
        terrainOptions.setViewDistanceFactor(DemoConfig.VIEW_DISTANCE_FACTOR);
//        terrainOptions.setViewDistance(DemoConfig.VIEW_DISTANCE_METERS);
        terrainOptions.setMaxTileZoomCoarsening(DemoConfig.TERRAIN_MAX_TILE_ZOOM_COARSENING);
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
        terrainOptions.setSurfaceShaderSource(DemoConfig.RELIEF_SURFACE ? DemoStyles.reliefSurfaceShader() : "");
        terrainOptions.setSurfaceColorParameter("uPaperColor", color(reliefPaper()));
        terrainOptions.setSurfaceColorParameter("uShadeColor", color(reliefShade()));
        terrainOptions.setSurfaceParameter("uShadeStrength", DemoConfig.RELIEF_SHADE_STRENGTH);
        terrainOptions.setSurfaceParameter("uAmbient", DemoConfig.RELIEF_AMBIENT);
        terrainOptions.setSurfaceParameter("uHaze", DemoConfig.RELIEF_HAZE);
        terrainOptions.setSurfaceParameter("uHazeDistance", DemoConfig.RELIEF_HAZE_DISTANCE);
        mapView.requestRender();
    }

    /** Creates the LightOptions on first call, then pushes every sun/shadow value onto it. */
    public void applyLightOptions() {
        if (lightOptions == null) {
            lightOptions = new LightOptions();
            mapView.getOptions().setLightOptions(lightOptions);
        }
        if (DemoConfig.SUN_HOUR_UTC >= 0) {
            int hour = (int) DemoConfig.SUN_HOUR_UTC;
            int minute = (int) ((DemoConfig.SUN_HOUR_UTC - hour) * 60);
            lightOptions.setSunPositionFromTime(DemoConfig.SUN_YEAR, DemoConfig.SUN_MONTH, DemoConfig.SUN_DAY,
                    hour, minute, DemoConfig.START_LAT, DemoConfig.START_LON);
        } else {
            lightOptions.setSunAzimuth(DemoConfig.SUN_AZIMUTH);
            lightOptions.setSunAltitude(DemoConfig.SUN_ALTITUDE);
        }
        lightOptions.setSunIntensity(DemoConfig.SUN_INTENSITY);
        lightOptions.setAmbientIntensity(DemoConfig.AMBIENT_INTENSITY);
        lightOptions.setAmbientColor(new Color(DemoConfig.AMBIENT_COLOR_ARGB));
        lightOptions.setSunColor(new Color(DemoConfig.SUN_COLOR_ARGB));
        lightOptions.setTerrainLightingEnabled(DemoConfig.TERRAIN_LIGHTING);
        lightOptions.setShadowStrength(DemoConfig.SHADOW_STRENGTH);
        lightOptions.setShadowSoftness(DemoConfig.SHADOW_SOFTNESS);
        lightOptions.setShadowMapSize(DemoConfig.SHADOW_MAP_SIZE);
        lightOptions.setShadowCascades(DemoConfig.SHADOW_CASCADES);
        lightOptions.setShadowBias(DemoConfig.SHADOW_BIAS);
        lightOptions.setShadowDistance(DemoConfig.SHADOW_DISTANCE);
        lightOptions.setShadowNormalOffset(DemoConfig.SHADOW_NORMAL_OFFSET);
        lightOptions.setShadowCasterMargin(DemoConfig.SHADOW_CASTER_MARGIN);
        updateSky();
    }

    /** Fog is its own options object and is independent of the terrain - it fogs a plain 2D map too. */
    public void applyFogOptions() {
        if (fogOptions == null) {
            fogOptions = new FogOptions();
            mapView.getOptions().setFogOptions(fogOptions);
        }
        fogOptions.setEnabled(DemoConfig.FOG_ENABLED);
        // STYLE mode leaves every value at its default so nothing but the style's Map block can be
        // responsible for what is on screen - the point being to show the style winning, and that
        // it can make the fog zoom-dependent, which the options cannot.
        boolean fromStyle = DemoConfig.FOG_SOURCE_STYLE.equals(DemoConfig.FOG_SOURCE);
        fogOptions.setColor(new Color(fromStyle ? 0 : DemoConfig.FOG_COLOR_ARGB));
        fogOptions.setRangeStart(DemoConfig.FOG_RANGE_START);
        fogOptions.setRangeEnd(DemoConfig.FOG_RANGE_END);
        fogOptions.setHighColor(new Color(fromStyle ? 0 : DemoConfig.FOG_HIGH_COLOR_ARGB));
        fogOptions.setSpaceColor(new Color(fromStyle ? 0 : DemoConfig.FOG_SPACE_COLOR_ARGB));
        fogOptions.setStarIntensity(fromStyle ? 0 : DemoConfig.FOG_STAR_INTENSITY);
        // How much of the sky the haze takes. The SAME term scales the ground, so the two meet at
        // the skyline with no seam at any tilt - there is no angle to reconcile any more.
        fogOptions.setHorizonBlend(DemoConfig.FOG_HORIZON_BLEND);
        // Peaks poking out of a valley haze (mapbox vertical-range).
        fogOptions.setVerticalRangeStart(DemoConfig.FOG_VERTICAL_START);
        fogOptions.setVerticalRangeEnd(DemoConfig.FOG_VERTICAL_END);
        mapView.requestRender();
    }

    /** The sky is always attached so the panel can toggle it live; disabled = no sky at all. */
    public void applySkyOptions() {
        if (skyOptions == null) {
            skyOptions = new SkyOptions();
            mapView.getOptions().setSkyOptions(skyOptions);
        }
        skyOptions.setEnabled(DemoConfig.SKY_ENABLED);
        skyOptions.setType("gradient".equals(DemoConfig.SKY_TYPE) ? SkyType.SKY_TYPE_GRADIENT : SkyType.SKY_TYPE_ATMOSPHERE);
        skyOptions.setQuality("low".equals(DemoConfig.SKY_QUALITY) ? SkyQuality.SKY_QUALITY_LOW
                : "high".equals(DemoConfig.SKY_QUALITY) ? SkyQuality.SKY_QUALITY_HIGH : SkyQuality.SKY_QUALITY_MEDIUM);
        skyOptions.setAtmosphereSunIntensity(DemoConfig.SKY_ATMO_SUN);
        skyOptions.setAtmosphereColor(new Color(DemoConfig.SKY_ATMO_COLOR_ARGB));
        skyOptions.setHaloColor(new Color(DemoConfig.SKY_ATMO_HALO_ARGB));
        skyOptions.setAtmosphereLuminance(DemoConfig.SKY_ATMO_LUMINANCE);
        // In the relief view the sky is part of the palette: a light one over the paper, a night
        // one over the ink. Alpha 0 makes it see-through, which is what an AR overlay wants.
        if (DemoConfig.RELIEF_SURFACE || DemoConfig.PEAK_FINDER) {
            // The generated day-cycle shader owns the sky's colours, so it has to go for the
            // palette's own sky to be visible at all (SkyOptions falls back to the built-in sky).
            if (!DemoConfig.DAY_CYCLE) {
                skyOptions.setShaderSource("");
            }
            skyOptions.setSkyColor(color(reliefSky()));
            mapView.getOptions().setSkyColor(color(reliefSky()));
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
                skyOptions.setShaderSource("");
            }
            return;
        }
        DemoConfig.TERRAIN_LIGHTING = true;
        DemoConfig.SKY_ENABLED = true;
        lightOptions.setTerrainLightingEnabled(true);
        skyOptions.setEnabled(true);
        // The sun is computed for the CURRENT map centre, not the start position.
        MapPos centre = mapView.getOptions().getBaseProjection().toWgs84(mapView.getFocusPos());
        DemoSky.applyHour(lightOptions, skyOptions, hourUtc, centre.getY(), centre.getX());
        mapView.requestRender();
    }

    // =============================================================================================
    // CAMERA / DEBUG HELPERS
    // =============================================================================================

    /** Note: setFocusPos expects BASE PROJECTION coordinates, so WGS84 must be converted first. */
    public void applyCamera() {
        Projection proj = mapView.getOptions().getBaseProjection();
        mapView.setFocusPos(proj.fromWgs84(new MapPos(DemoConfig.START_LON, DemoConfig.START_LAT)), 0);
        mapView.setZoom(DemoConfig.START_ZOOM, 0);
        applyLookRange();
        mapView.setTilt(DemoConfig.START_TILT, 0);
        mapView.setMapRotation(DemoConfig.START_ROTATION, 0);
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
        Options options = mapView.getOptions();
        options.setFreeRoamMode(freeRoamMode(DemoConfig.FREE_ROAM_MODE));
        options.setPanningSpeedMode(panningSpeedMode(DemoConfig.PANNING_SPEED_MODE));
        options.setFreeRoamLookSensitivity(DemoConfig.FREE_ROAM_LOOK_SENSITIVITY);
        options.setFreeRoamMoveSpeed(DemoConfig.FREE_ROAM_MOVE_SPEED);
        boolean lookUp = !"off".equals(DemoConfig.FREE_ROAM_MODE) || DemoConfig.STAR_SKY;
        float minTilt = lookUp ? -Math.max(10f, DemoConfig.LOOK_UP_LIMIT) : 10f;
        options.setTiltRange(new com.massifmaps.core.MapRange(minTilt, 90f));
    }

    /** "map" / "anchored" / "constant" -> the SDK constant. */
    public static @com.massifmaps.components.PanningSpeedMode.Value int panningSpeedMode(String name) {
        if ("map".equals(name)) {
            return com.massifmaps.components.PanningSpeedMode.PANNING_SPEED_MODE_MAP;
        }
        if ("constant".equals(name)) {
            return com.massifmaps.components.PanningSpeedMode.PANNING_SPEED_MODE_CONSTANT;
        }
        return com.massifmaps.components.PanningSpeedMode.PANNING_SPEED_MODE_ANCHORED;
    }

    /** "off" / "look" / "fps" -> the SDK constant. */
    public static @com.massifmaps.components.FreeRoamMode.Value int freeRoamMode(String name) {
        if ("look".equals(name)) {
            return com.massifmaps.components.FreeRoamMode.FREE_ROAM_MODE_LOOK;
        }
        if ("fps".equals(name)) {
            return com.massifmaps.components.FreeRoamMode.FREE_ROAM_MODE_FIRST_PERSON;
        }
        return com.massifmaps.components.FreeRoamMode.FREE_ROAM_MODE_OFF;
    }

    // =============================================================================================
    // STAR SKY: the map removed, the background cleared to nothing, only the sky left
    // =============================================================================================

    private Color savedClearColor;
    private Color savedSkyColor;
    private com.massifmaps.graphics.Bitmap savedBackgroundBitmap;
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
        Options options = mapView.getOptions();
        savedClearColor = options.getClearColor();
        savedSkyColor = options.getSkyColor();
        savedBackgroundBitmap = options.getBackgroundBitmap();
        starSkySaved = true;
    }

    /** Puts back what saveMapAppearance kept - the clear colour, the sky and the background. */
    private void restoreMapAppearance() {
        if (!starSkySaved) {
            return;
        }
        Options options = mapView.getOptions();
        options.setClearColor(savedClearColor);
        options.setSkyColor(savedSkyColor);
        options.setBackgroundBitmap(savedBackgroundBitmap);
        starSkySaved = false;
    }

    private void enterStarSky() {
        DemoConfig.STAR_SKY = true;
        Options options = mapView.getOptions();
        // Transparent, not black: the frame is then a hole that whatever is behind the surface
        // shows through, and it looks black on its own anyway.
        options.setClearColor(new Color((short) 0, (short) 0, (short) 0, (short) 0));
        options.setSkyColor(new Color((short) 0, (short) 0, (short) 0, (short) 0));
        options.setBackgroundBitmap(null);
        if (terrainOptions != null) {
            terrainOptions.setEnabled(false);
        }
        if (skyOptions != null) {
            skyOptions.setEnabled(false);
        }
        setSurfaceTranslucent(DemoConfig.STAR_SKY_TRANSLUCENT);
        setCameraPreviewEnabled(DemoConfig.STAR_SKY_CAMERA);
        rebuildLayers();
        Log.i(TAG, "star sky on: " + mapView.getLayers().count() + " layers, clear "
                + options.getClearColor().getARGB() + ", background " + options.getBackgroundBitmap());
        setMapLayerOpacity(1f); // the layers are out of the list now: leave them ready to come back
        applyLookRange();
        setOrientationFollowing(DemoConfig.STAR_SKY_ORIENTATION);
        mapView.requestRender();
    }

    private void leaveStarSky() {
        setOrientationFollowing(false);
        setCameraPreviewEnabled(false);
        DemoConfig.STAR_SKY = false;
        Options options = mapView.getOptions();
        if (starSkySaved) {
            options.setClearColor(savedClearColor);
            options.setSkyColor(savedSkyColor);
            options.setBackgroundBitmap(savedBackgroundBitmap);
            starSkySaved = false;
        }
        if (terrainOptions != null) {
            terrainOptions.setEnabled(DemoConfig.TERRAIN_ENABLED);
        }
        if (skyOptions != null) {
            skyOptions.setEnabled(DemoConfig.SKY_ENABLED);
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
                orientation = new DemoOrientation(context, mapView);
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
        for (Map.Entry<Feature, Layer> entry : layers.entrySet()) {
            if (entry.getKey() != Feature.CELESTIAL && entry.getKey() != Feature.STARS) {
                entry.getValue().setOpacity(opacity);
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
                Projection proj = mapView.getOptions().getBaseProjection();
                if ("zoom".equals(anim)) {
                    mapView.setZoom(DemoConfig.START_ZOOM + DemoConfig.ANIM_ZOOM_DELTA, duration);
                } else if ("pan".equals(anim)) {
                    mapView.setFocusPos(proj.fromWgs84(new MapPos(DemoConfig.START_LON + DemoConfig.ANIM_LON_DELTA, DemoConfig.START_LAT + DemoConfig.ANIM_LAT_DELTA)), duration);
                } else if ("rotate".equals(anim)) {
                    mapView.setMapRotation(DemoConfig.ANIM_ROTATION, duration);
                } else if ("zoomseq".equals(anim)) {
                    // zoom out, back in, out again - each step after the map settled, which is the
                    // repro shape for "stale content stays on screen" bugs.
                    final float zoomOut = DemoConfig.ANIM_ZOOM_OUT;
                    final float zoomIn = DemoConfig.START_ZOOM;
                    final float settle = DemoConfig.ANIM_SETTLE_MS;
                    handler.postDelayed(new Runnable() { public void run() { Log.i("zoomseq", "step1 out " + zoomOut); mapView.setZoom(zoomOut, 0); } }, 0);
                    handler.postDelayed(new Runnable() { public void run() { Log.i("zoomseq", "step2 in " + zoomIn); mapView.setZoom(zoomIn, 0); } }, (long) settle);
                    handler.postDelayed(new Runnable() { public void run() { Log.i("zoomseq", "step3 out " + zoomOut); mapView.setZoom(zoomOut, 0); } }, (long) (2 * settle));
                } else if ("approach".equals(anim)) {
                    // The wrong-scale repro shape: dive close enough for the terrain camera
                    // clearance to engage, pan along the slope, then pull back out.
                    final float close = DemoConfig.ANIM_APPROACH_ZOOM;
                    final float back = DemoConfig.ANIM_ZOOM_OUT;
                    final float settle = DemoConfig.ANIM_SETTLE_MS;
                    final MapPos panTo = proj.fromWgs84(new MapPos(DemoConfig.START_LON + DemoConfig.ANIM_LON_DELTA, DemoConfig.START_LAT + DemoConfig.ANIM_LAT_DELTA));
                    handler.postDelayed(new Runnable() { public void run() { Log.i("approach", "step1 close " + close); mapView.setZoom(close, duration); } }, 0);
                    handler.postDelayed(new Runnable() { public void run() { Log.i("approach", "step2 pan"); mapView.setFocusPos(panTo, duration); } }, (long) settle);
                    handler.postDelayed(new Runnable() { public void run() { Log.i("approach", "step3 out " + back); mapView.setZoom(back, duration); } }, (long) (2 * settle));
                }
            }
        }, (long) DemoConfig.ANIM_DELAY_MS);
    }

    /** PeakFinder-style relief outline post-process effect. */
    public void setReliefOutlineEnabled(boolean enabled) {
        DemoConfig.RELIEF_OUTLINE = enabled;
        MapRenderer renderer = mapView.getMapRenderer();
        if (enabled) {
            // The SDK provides the mechanism - an offscreen frame, the packed terrain depth and
            // named parameters - and the app provides the look, as a fragment shader string. There
            // is no relief effect in the SDK.
            reliefEffect = new PostProcessEffect("relief_outline", DemoStyles.reliefOutlineShader());
            reliefEffect.setTerrainDepthRequired(true);
            applyReliefOutlineParameters();
            renderer.setPostProcessEffect(reliefEffect);
        } else {
            reliefEffect = null;
            renderer.setPostProcessEffect(null);
        }
        mapView.requestRender();
    }

    /** Pushes the outline knobs (and the light/dark palette) onto the attached effect. */
    public void applyReliefOutlineParameters() {
        if (reliefEffect == null) {
            return;
        }
        reliefEffect.setFloatParameter("uIntensity", 1.0f);
        reliefEffect.setFloatParameter("uOutlineWidth", DemoConfig.RELIEF_OUTLINE_WIDTH);
        reliefEffect.setFloatParameter("uHorizonBoost", DemoConfig.RELIEF_HORIZON_BOOST);
        reliefEffect.setFloatParameter("uDepthThreshold", DemoConfig.RELIEF_DEPTH_THRESHOLD);
        reliefEffect.setFloatParameter("uCreaseStrength", DemoConfig.RELIEF_CREASE_STRENGTH);
        reliefEffect.setFloatParameter("uHaze", DemoConfig.RELIEF_HAZE);
        // The depth texture is half resolution (TerrainRenderer::BUFFER_DOWNSCALE), and the two
        // below are what keep the horizon the boldest line: the silhouette test is relaxed by the
        // grazing angle, and terrain-vs-terrain lines fade with distance while the sky's do not.
        reliefEffect.setFloatParameter("uDepthTexelSize", 2.0f);
        reliefEffect.setFloatParameter("uGrazingFloor", 0.15f);
        reliefEffect.setFloatParameter("uDistanceFade", 0.45f);
        reliefEffect.setColorParameter("uInkColor", color(reliefInk()));
        reliefEffect.setColorParameter("uPaperColor", color(reliefPaper()));
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
                baseDecoder.setStyleParameter(DemoStyles.BOOL_PARAMETER, Boolean.toString(paramOn));
                // The colour parameter takes the live path: no tile is decoded for it
                baseDecoder.setStyleParameter(DemoStyles.COLOR_PARAMETER, paramOn ? "#9cc3e0" : "#2f6f4f");
                // The table parameter is set as JSON; the style reads it per road class with get()
                baseDecoder.setStyleParameter(DemoStyles.TABLE_PARAMETER, paramOn
                    ? "{\"motorway\":\"#e27d60\",\"trunk\":\"#f0a868\",\"primary\":\"#d9b382\"}"
                    : "{\"motorway\":\"#7048e8\",\"trunk\":\"#9775fa\",\"primary\":\"#b197fc\"}");
                Log.d(TAG, "param " + DemoStyles.BOOL_PARAMETER + "=" + paramOn);
                handler.postDelayed(this, DemoConfig.PARAM_TOGGLE_INTERVAL_MS);
            }
        }, DemoConfig.PARAM_TOGGLE_INTERVAL_MS);
    }

    /** Elevation under a map position; blocks on tile loading, so call it off the UI thread. */
    public double getElevation(MapPos wgs84Pos) {
        return terrainOptions != null ? terrainOptions.getElevation(wgs84Pos) : 0;
    }

    private static Color color(int argb) {
        return new Color(argb);
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
            terrainOptions.setEnabled(enabled);
            terrainOptions.setExaggeration(target);
            return;
        }
        if (enabled) {
            // Flat first, and hold the ramp until the terrain-decoded tiles are in. Flipping the
            // flag re-decodes every tile, and the old FLAT ones are tesselated without terrain
            // subdivision: displacing those chords a road straight between its endpoints, which
            // over a valley rides well above the ground - roads in the sky. At exaggeration 0
            // nothing of that is visible, so waiting costs nothing to look at.
            terrainOptions.setExaggeration(0f);
            terrainOptions.setEnabled(true);
            startRampWhenTilesLoaded();
            return;
        }
        final android.animation.ValueAnimator animator =
            android.animation.ValueAnimator.ofFloat(enabled ? 0f : target, enabled ? target : 0f);
        animator.setDuration(durationMs);
        animator.setInterpolator(new android.view.animation.DecelerateInterpolator());
        animator.addUpdateListener(new android.animation.ValueAnimator.AnimatorUpdateListener() {
            public void onAnimationUpdate(android.animation.ValueAnimator a) {
                terrainOptions.setExaggeration(((Float) a.getAnimatedValue()).floatValue());
            }
        });
        if (!enabled) {
            animator.addListener(new android.animation.AnimatorListenerAdapter() {
                public void onAnimationEnd(android.animation.Animator a) {
                    terrainOptions.setEnabled(false);
                    terrainOptions.setExaggeration(target);
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
        final Runnable ramp = new Runnable() {
            private boolean done = false;
            public void run() {
                if (done) {
                    return;
                }
                done = true;
                if (baseLayer != null) {
                    baseLayer.setTileLoadListener(null);
                }
                android.animation.ValueAnimator a = android.animation.ValueAnimator.ofFloat(0f, target);
                a.setDuration(durationMs);
                a.setInterpolator(new android.view.animation.DecelerateInterpolator());
                a.addUpdateListener(new android.animation.ValueAnimator.AnimatorUpdateListener() {
                    public void onAnimationUpdate(android.animation.ValueAnimator anim) {
                        terrainOptions.setExaggeration(((Float) anim.getAnimatedValue()).floatValue());
                    }
                });
                a.start();
            }
        };
        if (baseLayer != null) {
            baseLayer.setTileLoadListener(new com.massifmaps.layers.TileLoadListener() {
                public void onVisibleTilesLoaded() {
                    handler.post(ramp);
                }
            });
        }
        // The listener can not fire when every visible tile is already decoded for the terrain
        // (toggling back and forth), so a timeout is what actually starts it in that case.
        handler.postDelayed(ramp, DemoConfig.TERRAIN_ANIM_TILE_TIMEOUT_MS);
    }

}
