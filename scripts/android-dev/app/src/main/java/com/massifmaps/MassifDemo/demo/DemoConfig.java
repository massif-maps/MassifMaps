package com.massifmaps.MassifDemo.demo;

/**
 * EVERY default of the demo app, in one place.
 *
 * HOW TO USE THIS FILE
 *  - to change what the app shows on launch: edit the values below, nothing else;
 *  - to change something WITHOUT rebuilding: pass the matching intent extra (see
 *    {@link #applyIntentOverrides()} at the bottom for the key of every field);
 *  - to change something while the app runs: use the on-screen panel (gear button), which
 *    writes back into these very fields and then asks {@link DemoMap} to apply/rebuild.
 *
 * The fields are NOT final on purpose: the panel mutates them, so at any moment this class is
 * the single source of truth for "what is the demo currently configured to do".
 */
public final class DemoConfig {

    // =============================================================================================
    // WHAT THE BASE MAP IS
    // =============================================================================================

    /** How the base map layer is built. */
    public enum BaseMode {
        /** Plain VectorTileLayer: master vector source + style decoder, nothing woven in. */
        PLAIN,
        /** CompositeVectorTileLayer: hillshade / satellite / contour sources woven INTO the style. */
        COMPOSITE
    }

    /** Where the style (CartoCSS / compiled style project) of the base map comes from. */
    public enum StyleSource {
        /** DirAssetPackage over a PLAIN FOLDER on the device - edit the style, restart, done. */
        DIR,
        /** ZippedAssetPackage over osm.zip - the classic packaged style. */
        ZIP,
        /** A CartoCSS string built in DemoStyles - self-contained, no file needed. */
        INLINE,
        /** In-memory project bundle declaring a 'param::' parameter (see DemoStyles.createProjectDecoder). */
        PROJECT,
        /** AndroidAssetPackage over the style project bundled in the APK assets (assets/style).
         *  The smallest complete example of a style a composite layer can weave sources into. */
        ASSETS,
        /** Shield test style: a CartoCSS string that uses the APK asset package for its FONTS, so
         *  it can put a font icon next to a name and let the culler pick the side (see
         *  DemoStyles.poiTestStyle). Dense on purpose - every POI and every place carries one. */
        POI
    }

    public static BaseMode BASE_MODE = BaseMode.COMPOSITE;
    public static StyleSource STYLE_SOURCE = StyleSource.INLINE;

    // =============================================================================================
    // FILES ON THE DEVICE
    // Data root is <external-storage>/alpimaps_mbtiles (same convention as before).
    // =============================================================================================

    /** Data root, resolved as <sd-card>/DATA_DIR_NAME (shared with the other test apps). */
    public static String DATA_DIR_NAME = "alpimaps_mbtiles";
    /** Folder (relative to the data root) read by DirAssetPackage for StyleSource.DIR. */
    public static String STYLE_DIR_NAME = "osm";
    /** Zip (relative to the data root) read by ZippedAssetPackage for StyleSource.ZIP, and the
     *  automatic fallback when STYLE_DIR_NAME does not exist on the device. */
    public static String STYLE_ZIP_NAME = "osm.zip";
    /** Style project inside the APK assets, read by AndroidAssetPackage for StyleSource.ASSETS. */
    public static String STYLE_ASSETS_PATH = "style";
    /** Which project of that package to compile: assets/style/<name>.json. 'eink' is the one that
     *  turns every polygon pattern on (forest, scrub, rock, scree, wetland, vineyard). */
    public static String STYLE_ASSETS_NAME = "osm";
    /** Which project of a DIR/ZIP package to compile, when it carries more than one. A converted
     *  Mapbox Standard writes day/dawn/dusk/night.json over one style.mss, so this is the
     *  time-of-day switch. Empty lets CompiledStyleSet pick the first project it finds. */
    public static String LIGHT_PRESET = "";
    /** Style zip used by the offline "routes" layer. */
    public static String ROUTES_STYLE_ZIP_NAME = "inner.zip";
    /** MBTiles used by the offline "routes" layer. */
    public static String ROUTES_MBTILES_NAME = "france/france_routes.mbtiles";
    /** Valhalla tiles used by the offline routing test action. */
    public static String ROUTING_VTILES_NAME = "rhone-alpes.vtiles";

    // =============================================================================================
    // WHICH LAYERS ARE ADDED (each one is independent and can be toggled live from the panel)
    // Draw order is the order of DemoMap.LAYER_ORDER, not the order of these fields.
    // =============================================================================================

    /** The base map (vector tiles + style). */
    public static boolean LAYER_BASE = true;
    /** Stand-alone HillshadeRasterTileLayer over the shared DEM (independent of the composite slot). */
    public static boolean LAYER_HILLSHADE = false;
    /** Stand-alone contour layer: ContourTileDataSource + its own CartoCSS. */
    public static boolean LAYER_CONTOUR = false;
    /** PRE-BAKED contour vector tiles fetched over HTTP, styled like the real style's '#contour'
     *  rules. The A/B reference for LAYER_CONTOUR / the '#contour' composite slot. */
    public static boolean LAYER_CONTOUR_TILES = false;
    /** Stand-alone raster layer (OSM raster tiles by default). */
    public static boolean LAYER_SATELLITE = false;
    /** CustomRasterTileLayer running a hypsometric-tint shader over the raw DEM tiles. */
    public static boolean LAYER_HYPSO = false;
    /** Markers on summits + a line across the valley: the terrain occlusion / drape test set. */
    public static boolean LAYER_ELEMENTS = true;
    /** Offline routes layer (needs ROUTES_MBTILES_NAME + ROUTES_STYLE_ZIP_NAME on the device). */
    public static boolean LAYER_ROUTES = false;
    /** Synthetic mountain-road route (GeoJSON tiles + CartoCSS): the line join / cap / opacity bench. */
    public static boolean LAYER_ROUTE_TEST = false;
    /** Many routes, one of them selected through a style parameter - the selection-cost bench. */
    public static boolean LAYER_ROUTE_SELECT = false;
    /** Navigation maneuver arrows (ManeuverArrowBuilder + CartoCSS). Filled by the routing test. */
    public static boolean LAYER_MANEUVERS = false;

    // =============================================================================================
    // COMPOSITE SLOTS (BaseMode.COMPOSITE only)
    // These are sources woven into the master style at the position of their '#name' rule.
    // =============================================================================================

    public static boolean COMPOSITE_HILLSHADE = false;
    public static boolean COMPOSITE_SATELLITE = false;
    public static boolean COMPOSITE_CONTOUR = true;
    /**
     * A SECOND vector tileset merged into the master style. A MapBox style may draw from several
     * tilesets - MapTiler's topo keeps its peaks and volcanoes in a 'landform' one - and a CartoCSS
     * project has a single datasource, so the extra tileset needs its own composite slot here.
     * Empty = off.
     */
    public static String LANDFORM_URL = "";
    public static String LANDFORM_SLOT = "peak";
    public static String LANDFORM_CACHE_DB = "landform.db";
    public static int LANDFORM_MIN_ZOOM = 0;
    public static int LANDFORM_MAX_ZOOM = 14;
    /** Single-pass segmented rendering (A/B switch of the composite renderer). */
    public static boolean COMPOSITE_SINGLE_PASS = true;
    /** Per-source zoom bias: +1 fetches the DEM one zoom deeper than the base map. */
    public static float COMPOSITE_HILLSHADE_ZOOM_BIAS = 0f;

    // =============================================================================================
    // TILE SOURCES
    // =============================================================================================

    /** On-disk cache per tile source, MB (PersistentCacheTileDataSource.setCapacity).
     *  '--es cacheMb 100'. */
    public static int PERSISTENT_CACHE_MB = 200;
    /** Same for the DEM, which needs far more of it: one terrain view asks for a whole pyramid of
     *  elevation tiles, and at the SDK default of 50 MB it does not fit. Measured on the Crosscall
     *  at the default camera, terrain on: 64-70 DEM tiles re-downloaded on EVERY start (25-50 s of
     *  network), and two consecutive starts missed on DIFFERENT tiles - the cache was evicting
     *  exactly what the next start needed. '--es demCacheMb 200'. */
    public static int DEM_PERSISTENT_CACHE_MB = 600;

    /** Master vector tile source of the base map. */
    public static String VECTOR_URL = "https://tiles.akylas.fr/data/france/{z}/{x}/{y}.pbf";
    public static int VECTOR_MIN_ZOOM = 0;
    public static int VECTOR_MAX_ZOOM = 14;
    public static String VECTOR_CACHE_DB = "akylas_vect.db";
    /**
     * Which tile LEVEL the base map asks for, relative to the view. 0 = this SDK's own rule
     * (256 px tiles, level = floor(zoom)); -1 is what a source authored for MapBox's 512 px tiles
     * wants, and is not cosmetic - a level deeper carries a level's worth of extra POIs, so at
     * mapbox-gl's z13.67 we drew bicycle parkings its z13 tile does not even contain.
     */
    public static float VECTOR_ZOOM_BIAS = 0.0f;
    public static String HTTP_USER_AGENT = "AlpiMaps/1.4 (contact: contact@akylas.fr)";

    /** Shared elevation source: 3D terrain, hillshade, contours and the hypsometric tint all use it. */
    public static String DEM_URL = "https://tiles.mapterhorn.com/{z}/{x}/{y}.webp";
    public static int DEM_MIN_ZOOM = 1;
    /** The REAL max zoom of the source (mapterhorn stops at 16). Setting it higher only produces
     *  404s: deeper camera zooms are served by overzooming the last available level. */
    public static int DEM_MAX_ZOOM = 16;
    /** "terrarium" or "mapbox" - decides which ElevationDecoder is used. */
    public static String DEM_ENCODING = "terrarium";
    public static String DEM_CACHE_DB = "mapterhorn.db";

    /** A SECOND elevation source, of the other encoding, behind an OrderedTileDataSource: the DEM
     *  above answers first and this one fills what it does not cover. The encoding is resolved per
     *  TILE, so the two need not agree. Off by default - '--es dem2Url <url> --es dem2Encoding mapbox'.
     *  Launch-only, and changing an encoding needs 'pm clear': cached tiles carry the old one. */
    public static String DEM2_URL = "";
    public static int DEM2_MIN_ZOOM = 1;
    public static int DEM2_MAX_ZOOM = 15;
    public static String DEM2_ENCODING = "mapbox";
    public static String DEM2_CACHE_DB = "dem2.db";

    /** Pre-baked contour vector tiles (tippecanoe, layer 'contour', fields 'ele' + 'div').
     *  Zooms 11..14 only: the tileset has no data below 11 and 14 is overzoomed above. */
    public static String CONTOUR_TILES_URL = "https://tiles.akylas.fr/data/contours/{z}/{x}/{y}.pbf";
    public static int CONTOUR_TILES_MIN_ZOOM = 11;
    public static int CONTOUR_TILES_MAX_ZOOM = 14;
    public static String CONTOUR_TILES_CACHE_DB = "akylas_contours.db";

    /** Raster source used by the satellite layer / '#satellite' composite slot. */
    public static String RASTER_URL = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";
    public static int RASTER_MIN_ZOOM = 0;
    public static int RASTER_MAX_ZOOM = 19;
    public static String RASTER_CACHE_DB = "openstreetmap.db";

    // =============================================================================================
    // CAMERA (start position; every value can be overridden with --es lon/lat/zoom/tilt/rotation)
    // =============================================================================================

    public static double START_LON = 5.718957;
    public static double START_LAT = 45.187362;
    public static float START_ZOOM = 16.22f;
    public static float START_TILT = 26f;
    public static float START_ROTATION = -15.12f;

    // =============================================================================================
    // 3D TERRAIN (com.massifmaps.components.TerrainOptions)
    // =============================================================================================

    /** Tile decode threads (Options.setTileThreadPoolSize). The SDK default is 1; tangram-ng
     *  runs 2 (SceneOptions::numTileWorkers). Raise it to get tiles on screen sooner. */
    public static int TILE_THREAD_POOL_SIZE = 2;
    /** Screen size a tile may cover before the next zoom level is used, as a factor on tangram's
     *  rule (a 2x2 block of nominal tiles). 1 = their rule; larger keeps tiles coarser at a tilt
     *  (fewer tiles, fewer far labels); 0 refines everything to the camera zoom.
     *  '--es lodFactor 2'. */
    public static float TILE_LOD_FACTOR = 0.5f;
    /** Zoom levels a tile may lose to the grazing angle alone, 0 = no limit (the tangram rule).
     *  Bounds only the foreshortening half of the LOD area test, never the distance half, so far
     *  ground stays coarse while a tilted mid-field is refined. Costs ~2x the tiles per level.
     *  '--es lodGrazing 1.25'. */
    public static float TILE_LOD_GRAZING = 0f;
    /** Metres beyond which the inline style's street labels are not placed (0 = no limit). Only
     *  the inline style uses it; it is the 'text-max-distance' CartoCSS property.
     *  '--es labelMaxDistance 2000'. */
    public static float LABEL_MAX_DISTANCE = 2000f;

    public static boolean TERRAIN_ENABLED = true;
    public static float TERRAIN_EXAGGERATION = 1.0f;
    /** Auto-flatten: render flat once the terrain's on-screen parallax drops below this many
     *  pixels, and once the tilt reaches AUTO_FLATTEN_TILT. OFF here, unlike the SDK (2 / 88):
     *  flattening drops the 3D passes, and with them the buildings and their shadows - which is
     *  what this bench is usually looking at, and straight down is where a shadow reads best.
     *  '--es autoFlatten 2 --es autoFlattenTilt 88 --es autoFlattenMs 300' is the A/B. */
    public static float AUTO_FLATTEN_PARALLAX = 0.0f;
    public static float AUTO_FLATTEN_TILT = 0.0f;
    public static long AUTO_FLATTEN_MS = 300;
    /** How far flattening goes: false = RENDER (terrain passes only), true = FULL (a flat map
     *  decodes and culls as a plain 2D one, for a re-decode at each switch). '--es fullSwitch true'. */
    public static boolean TERRAIN_FULL_SWITCH = false;
    /** Terrain toggle 'expand' animation, ms (0 = pop, the old behaviour). */
    public static long TERRAIN_ANIM_MS = 700;
    /** How long the expand animation waits for terrain-decoded tiles before ramping anyway, ms. */
    public static long TERRAIN_ANIM_TILE_TIMEOUT_MS = 2500;
    /** Triangles per tile side. Slack against the draped content scales as (32/res)^2.
     *  64 is what tangram-ng uses (RasterStyle::build, hardcoded); 128 measured 8.5 fps against
     *  15.2 at 64 on the Crosscall (mesh 64, plain base, no labels/hillshade/contours). */
    public static int TERRAIN_MESH_RESOLUTION = 64;
    /** Metres the camera is held above the ground. The SDK default is 200, which stops you well
     *  short of the surface; 30 lets you get close enough to judge mesh and hillshade detail.
     *  '--es clearance N' (0 disables the clamp entirely - you can then fly through the ground). */
    public static float TERRAIN_CAMERA_CLEARANCE = 60.0f;
    /** Render fills through an offscreen drape pass instead of displacing their geometry.
     *  ON, and it is both the correct and the fast choice - this is tangram's arrangement, where the
     *  ground draw samples a texture (`base_color = sampleRaster(0)`, res/scenes/hillshade.yaml)
     *  instead of stacking vector fills over the terrain. A displaced fill chords over the ground
     *  between its vertices and z-fights it (pale slivers on slopes), and it cannot be given room
     *  without the forward pull that leaks content through ridges. Measured on the Crosscall, north
     *  pan with contours and hillshade: 12.9 fps against 10.5 with fills as geometry (bake +1.7 ms,
     *  geometry submission -3.8 ms). '--es drape false' goes back for an A/B. */
    public static boolean TERRAIN_DRAPE_FILLS = true;
    /** Lines too, which is what a city pan pays for: 13.4-15.2 fps as geometry against 26.8-27.7
     *  draped (Crosscall, packaged style, 5.724/45.188 z15 t45). Contours stay sharp - see
     *  TerrainOptions.NoDrapeLayerFilter. '--es drapeLines false' goes back for an A/B. */
    public static boolean TERRAIN_DRAPE_LINES = true;
    /** 0 = derive from the screen, which the cache's memory budget then clamps to 512. Set
     *  explicitly, so the extrusions' contact shadow baked into the drape is more than a texel
     *  wide: at 512 a drape texel is ~1.7 m on the ground and the shadow reaches under 1 m. */
    public static int TERRAIN_DRAPE_RESOLUTION = 1024;
    /** Style layers kept OUT of the drape bake and drawn live instead. They land on top of every
     *  draped layer whatever the style order, which is why an empty filter is the A/B for the
     *  contours-over-roads ordering. '--es noDrape ""' drapes everything. */
    public static String TERRAIN_NO_DRAPE_FILTER = "^contour|maneuver.*";
    /** Stitch neighbouring DEM tiles so ridges do not appear at tile borders. */
    public static boolean TERRAIN_TILE_EDGE_STITCHING = true;
    public static boolean TERRAIN_SEAMLESS_TILE_EDGES = true;
    public static boolean TERRAIN_ELEVATION_PREFETCH = true;
    /** Hide billboards behind relief; tolerance > 0 keeps summits partly behind a ridge visible. */
    public static boolean TERRAIN_BILLBOARD_OCCLUSION = true;
    public static float TERRAIN_OCCLUSION_TOLERANCE = 0.0f;
    /** Opacity a label keeps while its anchor is behind a BUILDING, for the whole layer.
     *  1 = no occlusion. TerrainOptions::TextOcclusionOpacity. */
    public static float TERRAIN_TEXT_OCCLUSION_OPACITY = 1.0f;
    /** The same, for the ROAD LABEL style layer alone (text-occlusion-opacity). < 0 = not set,
     *  so the layer-wide value above stands. This is the per-style-layer half. */
    public static float INLINE_TEXT_OCCLUSION_OPACITY = -1.0f;
    /** 0 = off; caps terrain LOD tile detail at what flat rendering would show. */
    public static boolean TERRAIN_MAX_TILE_ZOOM_OFFSET_ENABLED = false;
    public static int TERRAIN_MAX_TILE_ZOOM_OFFSET = 0;
    /** Drapes Options.getBackgroundBitmap over the terrain surface where nothing is drawn. */
    public static boolean TERRAIN_BACKGROUND_BITMAP = false;

    // Fog / view distance: they belong together, the distance ENDS the ground and the fog is what
    // makes it fade out instead of being cut off.
    /** The master switch on FogOptions - values stay configured while it is off. '--es fog false'. */
    public static boolean FOG_ENABLED = false;
    /** Where the fog values come from: DIRECT = this config on FogOptions, STYLE = the inline
     *  style's Map block (mapbox-shaped, and the only one that can be zoom-dependent). The style
     *  wins over the options wherever it declares a property, which is what STYLE demonstrates.
     *  Only the inline style carries it - '--es style inline --es fogSource style'. */
    public static final String FOG_SOURCE_DIRECT = "direct";
    public static final String FOG_SOURCE_STYLE = "style";
    public static String FOG_SOURCE = FOG_SOURCE_DIRECT;
    public static int FOG_COLOR_ARGB = 0xffb8c6d8;
    /** Fog range, in multiples of the camera-to-focus distance (mapbox 'range'). Zoom-independent,
     *  so one pair holds everywhere. '--es fogRangeStart 0.8 --es fogRangeEnd 8'. */
    public static float FOG_RANGE_START = 0.8f;
    public static float FOG_RANGE_END = 8f;
    /** Mapbox high-color / space-color: the upper atmosphere and the zenith. 0 = leave the
     *  SkyOptions gradient alone. '--es fogHigh #245bde --es fogSpace #000000'. */
    public static int FOG_HIGH_COLOR_ARGB = 0;
    public static int FOG_SPACE_COLOR_ARGB = 0;
    /** Mapbox star-intensity, 0..1. '--es fogStars 0.15'. */
    public static float FOG_STAR_INTENSITY = 0f;
    /** How far the map is drawn AND where the far plane sits, as a factor on tangram's own rule
     *  (far = 2 * cameraHeight / cos(pitch + fovy/2), capped at 127 tile widths). 1 is their rule
     *  verbatim; 0 falls back to the visible ground, which reaches the horizon.
     *  '--es viewDistance 0.5' halves it. */
    /** Fraction of a quarter turn the sky haze fades out over - mapbox horizon-blend
     *  ('--es fogBlend 0.133'). */
    public static float FOG_HORIZON_BLEND = 12f / 90f;
    /** Altitudes, in metres, the fog fades out between - mapbox vertical-range. Equal values (the
     *  default) fog every altitude alike; '--es fogVertStart 1200 --es fogVertEnd 2400' leaves the
     *  summits clear of a haze filling the valley. */
    public static float FOG_VERTICAL_START = 0f;
    public static float FOG_VERTICAL_END = 0f;

    /** Named looks, '--es fogPreset mapbox'. Each one sets every FOG_* field below, so a preset is
     *  a starting point the sliders then move - not a mode. HAZE is the plain neutral distance fog
     *  with no atmosphere; the other three are the mapbox 'fog' documentation values and two
     *  variations on them. */
    public static final String[] FOG_PRESETS = { "haze", "mapbox", "dusk", "space" };
    public static String FOG_PRESET = "";

    /** Writes a preset into the FOG_* fields. Unknown names leave them alone. */
    public static void applyFogPreset(String preset) {
        if (preset == null) {
            return;
        }
        FOG_PRESET = preset;
        // Every preset sets a high colour: it is the one property with no other way to see it, and
        // leaving it transparent (which is the API default) makes the preset look like it did
        // nothing to the sky.
        if ("haze".equals(preset)) {
            // Daylight, thin: a pale sky over a neutral ground haze, no stars.
            FOG_COLOR_ARGB = 0xffb8c6d8; FOG_RANGE_START = 0.4f; FOG_RANGE_END = 4f;
            FOG_HIGH_COLOR_ARGB = 0xff8fb4dc; FOG_SPACE_COLOR_ARGB = 0xff5b86c4;
            FOG_HORIZON_BLEND = 0.2f; FOG_STAR_INTENSITY = 0f;
        } else if ("mapbox".equals(preset)) {
            // The values from the mapbox fog documentation, verbatim.
            FOG_COLOR_ARGB = 0xffdc9f9f; FOG_RANGE_START = 0.8f; FOG_RANGE_END = 8f;
            FOG_HIGH_COLOR_ARGB = 0xff245bde; FOG_SPACE_COLOR_ARGB = 0xff000000;
            FOG_HORIZON_BLEND = 0.5f; FOG_STAR_INTENSITY = 0.15f;
        } else if ("dusk".equals(preset)) {
            FOG_COLOR_ARGB = 0xffe8a87c; FOG_RANGE_START = 0.3f; FOG_RANGE_END = 5f;
            FOG_HIGH_COLOR_ARGB = 0xff3b5998; FOG_SPACE_COLOR_ARGB = 0xff0b1026;
            FOG_HORIZON_BLEND = 0.35f; FOG_STAR_INTENSITY = 0.35f;
        } else if ("space".equals(preset)) {
            // Night: a dark ground haze, a deep blue atmosphere over it and near-black at the
            // zenith, with the stars well up. Pair it with a low sun ('--es sunHour 1') - the fog
            // colour is LIT, so under a noon sun this still comes out bright.
            FOG_COLOR_ARGB = 0xff2b3a5c; FOG_RANGE_START = 0.2f; FOG_RANGE_END = 3f;
            FOG_HIGH_COLOR_ARGB = 0xff0e1c46; FOG_SPACE_COLOR_ARGB = 0xff01030c;
            FOG_HORIZON_BLEND = 0.3f; FOG_STAR_INTENSITY = 0.9f;
        }
    }
    public static float VIEW_DISTANCE_FACTOR = 1f;
    /** Absolute view distance in METRES, whatever the camera's height or pitch. 0 = the factor
     *  rule above (tangram's, which shortens the view as the camera comes down to the ground -
     *  which is why this is pinned instead). What a long view costs is NOT the distance but the
     *  distance times the finest a far tile is allowed to be: at coarsening 3 this drew 550 tiles
     *  against 50, all of them z13, which is minutes of loading and a tile blinking as each lands.
     *  Pair it with enough COARSENING (below) and it costs the same 50 tiles. */
    public static float VIEW_DISTANCE_METERS = 170000f;
    /** Zoom levels below the camera a tile may coarsen to in terrain mode. The tile surface is the
     *  depth occluder and the DEM level follows the tile zoom, so unbounded coarsening means leaky
     *  ridges and blocky hillshade - but too little of it is what makes a long view unaffordable,
     *  because it overrides the screen-area LOD rule that would coarsen the horizon by itself.
     *  8 with the 170 km view above: 50 tiles, and the near field gets FINER (z15 against z13),
     *  because the budget goes where it is visible instead of paving the horizon.
     *  '--es coarsening 2'. */
    public static int TERRAIN_MAX_TILE_ZOOM_COARSENING = 8;

    // =============================================================================================
    // SUN / LIGHT / SHADOWS (com.massifmaps.components.LightOptions)
    // =============================================================================================

    public static boolean TERRAIN_LIGHTING = false;
    /** When >= 0 the sun is placed from the date+hour below instead of azimuth/altitude. */
    public static float SUN_HOUR_UTC = -1f;
    /** The date the sky is drawn for. TODAY by default, which is what makes the sun, the moon, the
     *  planets and the stars the ones actually up there. '--es sunYear/sunMonth/sunDay' overrides. */
    public static int SUN_YEAR = (int) DemoAstro.nowUtc()[0];
    public static int SUN_MONTH = (int) DemoAstro.nowUtc()[1];
    public static int SUN_DAY = (int) DemoAstro.nowUtc()[2];
    public static float SUN_AZIMUTH = 355f;
    public static float SUN_ALTITUDE = 9f;
    /** Whether the demo's own sun beats one the style states. Off, so a converted MapBox style
     *  lights as its source does; on, the sliders and --es sunAzimuth/sunAltitude take over.
     *  '--es appSun true'. */
    public static boolean APP_SUN = false;
    public static float SUN_INTENSITY = 1.0f;
    public static float AMBIENT_INTENSITY = 1.0f;
    /** Tint of everything in shadow. White = neutral; a cool blue reads as sky-lit at dusk. */
    public static int AMBIENT_COLOR_ARGB = 0xFFFFFFFF;
    /** Colour of the direct sun. Warm at a low sun is what makes a dusk scene read as dusk. */
    public static int SUN_COLOR_ARGB = 0xFFFFFFFF;
    public static float SHADOW_STRENGTH = 0.3f;
    public static float SHADOW_SOFTNESS = 1.0f;
    public static int SHADOW_MAP_SIZE = 1024;
    public static int SHADOW_CASCADES = 3;
    public static float SHADOW_BIAS = 1.0f;
    public static float SHADOW_NORMAL_OFFSET = 3.0f;   // shadow-map texels
    public static float SHADOW_DISTANCE = 0f;   // multiples of the camera-to-focus distance, 0 = built-in 4.5
    public static int SHADOW_CASTER_MARGIN = 3;

    // =============================================================================================
    // SKY (com.massifmaps.components.SkyOptions) + the generated day-cycle shader
    // =============================================================================================

    public static boolean SKY_ENABLED = true;
    /** What the sky pass draws: "atmosphere" (Rayleigh/Mie scattering, the default) or "gradient"
     *  (the two-colour ramp). '--es skyType gradient'. */
    public static String SKY_TYPE = "atmosphere";
    /** Scattering sample counts: "low" (5x3), "medium" (8x4) or "high" (12x5). The cost is per
     *  fragment of visible sky, so this is what a low-tilt camera pays. '--es skyQuality low'. */
    public static String SKY_QUALITY = "medium";
    /** Brightness of the sun that lights the atmosphere - mapbox sky-atmosphere-sun-intensity. */
    public static float SKY_ATMO_SUN = 10f;
    /** Tints on the two scattering terms, alpha = strength: the Rayleigh blue and the Mie halo. */
    public static int SKY_ATMO_COLOR_ARGB = 0xffffffff;
    public static int SKY_ATMO_HALO_ARGB = 0xffffffff;
    /** Exposure the scattered light is tonemapped with; lower brightens the sky. */
    public static float SKY_ATMO_LUMINANCE = 1f;
    /** Celestial objects: sun, moon and the sun's daily path, drawn by a CelestialLayer and
     *  placed by direction, so they stay in the sky while the map pans under them. The demo
     *  builds them in DemoCelestial - the SDK API knows nothing about suns or moons. */
    /** Free roam mode: "off", "look" (one finger looks, two fingers still pan/pinch/rotate the
     *  map) or "fps" (mouse look - the camera never moves - and two fingers move like the keys
     *  would, with no pinch and no rotation). '--es freeRoam fps'. */
    public static String FREE_ROAM_MODE = "off";
    /** Pan speed on a tilted view: "map" (the point under the finger follows it exactly, so the
     *  speed changes as the finger moves between near and far parts of the screen), "anchored"
     *  (the speed a gesture starts with, kept for the whole gesture) or "constant" (always the
     *  scale at the centre of the screen). '--es panSpeed map|anchored|constant'. */
    public static String PANNING_SPEED_MODE = "anchored";
    /** Degrees of turn per inch of drag. */
    public static float FREE_ROAM_LOOK_SENSITIVITY = 90f;
    /** How far an inch of two-finger drag moves, as a fraction of the camera to focus distance. */
    public static float FREE_ROAM_MOVE_SPEED = 0.5f;
    /** How far above the horizon free roam may look, in degrees. This is a NEGATIVE tilt: the
     *  camera stays put and the view pitches up (Options.setTiltRange). 0 stops at the horizon,
     *  which is what a map does by default. */
    public static float LOOK_UP_LIMIT = 90f;
    public static boolean CELESTIAL = true;
    public static boolean CELESTIAL_SUN = true;
    public static boolean CELESTIAL_MOON = true;
    public static boolean CELESTIAL_ARC = true;
    /** The moon's path across the day, the twin of the sun's. */
    public static boolean CELESTIAL_MOON_ARC = true;
    /** Draw the moon with the phase it really has (a bitmap the demo paints), rather than a disc. */
    public static boolean CELESTIAL_MOON_PHASE = true;
    /** Angular diameters in degrees. The real sun and moon are both about 0.5; larger is easier
     *  to see and to hit on a phone. */
    public static float CELESTIAL_SUN_SIZE = 2.5f;
    public static float CELESTIAL_MOON_SIZE = 2.0f;
    public static float CELESTIAL_ARC_WIDTH = 2.0f;

    // --- stars (DemoStars + DemoStarCatalogue) ----------------------------------------------------

    /** The star layer: the bright-star catalogue, the constellation figures and the planets, all
     *  placed for the date above. Off by default - it is a second CelestialLayer. */
    public static boolean STARS = false;
    public static boolean STARS_STARS = true;
    public static boolean STARS_FIGURES = true;
    public static boolean STARS_PLANETS = true;
    public static boolean STARS_EQUATOR = false;
    /** A magnitude -1.5 star is this big on screen, and each magnitude takes off that much. The
     *  sizes are in PIXELS, so they are scaled by the screen density in DemoStars. */
    public static float STARS_BRIGHTEST_SIZE = 5f;
    public static float STARS_SIZE_PER_MAGNITUDE = 0.55f;
    public static float STARS_FAINTEST_SIZE = 1.4f;
    public static float STARS_FIGURE_WIDTH = 1.5f;
    public static float STARS_FIGURE_CLICK_RADIUS = 2.5f;
    public static float STARS_PLANET_SIZE = 1.2f;
    /** Constellation NAMES drawn in the sky, at the middle of each figure. The demo paints them
     *  into a bitmap, so they are styled entirely by the app. */
    public static boolean STARS_LABELS = true;
    public static float STARS_LABEL_TEXT_SIZE = 15f;   // dp of the text inside the bitmap
    public static float STARS_LABEL_SCALE = 1f;        // 1 = the text at the size it was painted
    public static float STARS_LABEL_OPACITY = 0.85f;

    /** Star sky: no map at all - the layers are REMOVED, the terrain is off and the background is
     *  cleared to transparent, so the only thing drawn is the sky. Fades in and out. */
    public static boolean STAR_SKY = false;
    public static float STAR_SKY_FADE_MS = 600f;
    /** Follow the device's orientation in star sky mode: turning the phone turns the view, and
     *  raising it looks up. Needs LOOK_UP_LIMIT > 0 to reach the zenith. */
    public static boolean STAR_SKY_ORIENTATION = false;
    /** Show the live camera BEHIND the transparent map in star sky mode: the sky drawn over what
     *  the camera sees. Needs the CAMERA permission, and needs STAR_SKY_TRANSLUCENT. */
    public static boolean STAR_SKY_CAMERA = false;
    /** Ask for a TRANSLUCENT GL surface in star sky mode, which is what lets whatever is behind the
     *  view (a camera preview) show through the transparent clear colour. On its own it looks the
     *  same - black - and it costs a surface recreation, so it can be turned off. */
    public static boolean STAR_SKY_TRANSLUCENT = true;

    /** Day cycle: sun/moon/stars/clouds shader driven by SUN_HOUR_UTC, updated live by the panel. */
    public static boolean DAY_CYCLE = false;
    public static float DAY_CYCLE_HOUR = 12f;

    // =============================================================================================
    // HILLSHADE (stand-alone layer AND the composite '#hillshade' child layer where applicable)
    // =============================================================================================

    /** IGOR, COMBINED, BASIC ... see com.massifmaps.layers.HillshadeMethod. */
    public static String HILLSHADE_METHOD = "IGOR";
    public static float HILLSHADE_CONTRAST = 0.5f;
    public static float HILLSHADE_HEIGHT_SCALE = 0.05f;
    public static float HILLSHADE_EXAGGERATION = 1.0f;
    public static float HILLSHADE_ILLUMINATION_DEGREES = 180f;
    public static boolean HILLSHADE_ILLUMINATION_FOLLOWS_MAP = false;
    public static int HILLSHADE_SHADOW_COLOR_ARGB = 0xB0000000;
    public static int HILLSHADE_HIGHLIGHT_COLOR_ARGB = 0xFF000000;
    public static int HILLSHADE_ACCENT_COLOR_ARGB = 0xFF000000;
    /** GPU contour lines drawn inside the hillshade pass (no labels, not styled from CartoCSS). */
    public static boolean HILLSHADE_CONTOUR_LINES = false;
    public static float HILLSHADE_CONTOUR_INTERVAL = 100f;
    public static float HILLSHADE_CONTOUR_WIDTH = 0.8f;
    public static int HILLSHADE_CONTOUR_COLOR_ARGB = 0xFFC56008;
    /** Replaces the lighting shader with the "slope colouring" one (steepness bands). */
    public static boolean HILLSHADE_SLOPES_SHADER = false;

    // =============================================================================================
    // CONTOURS (ContourTileDataSource - both the stand-alone layer and the composite slot)
    // =============================================================================================

    public static float CONTOUR_BASE_INTERVAL = 10f;
    /** DEM samples per tile side before tracing: lower = far fewer vertices to trace and drape.
     *  0 = the DEM's own resolution, which is what matching 3D terrain needs - the ground is
     *  displaced by every texel of the same tile, so a line traced on a coarser grid cuts through
     *  the spurs and gullies between its samples. */
    public static int CONTOUR_RESOLUTION = 128;
    public static float CONTOUR_SIMPLIFY_TOLERANCE = 1.5f;
    /** Interval multiplier per tile zoom, as "maxZoom:multiplier" rungs ("-1" = every zoom above the
     *  others). The rungs must NEST (each a multiple of the finer ones) or lines stop at a tile
     *  border between zooms; cost tracks how fine they are. "" = leave the SDK defaults. */
    public static String CONTOUR_INTERVAL_LADDER = "";
    /** Tracing grid resolution per tile zoom, same syntax. Tracing is ~quadratic in it, so this is
     *  the cheapest knob for zoomed-out frames. "" = CONTOUR_RESOLUTION at every zoom. */
    public static String CONTOUR_RESOLUTION_LADDER = "";
    /** Tile substitution for the contour layer: "all" | "visible" | "none". A missing tile is stood
     *  in for by a cached one of another zoom - for contours that means a 48-sample z9 grid stretched
     *  over the view, which reads as long straight chords until the real tile lands. */
    public static String CONTOUR_TILE_SUBSTITUTION = "all";
    /** How many zoom levels the contour layer may walk UP for a stand-in tile while the right one
     *  loads. Every level is a different interval and grid, so a deep walk shows the same area as a
     *  ladder of coarser and coarser lines. 1 = only the immediate parent. */
    public static int CONTOUR_MAX_OVERZOOM_STANDIN = 6;
    /** Decoded-tile cache of the BASE map layer, in MB. Same story as the contour one: tiles that
     *  leave the visible set are moved into this cache, and whatever does not fit is evicted - so
     *  when it is too small a zoom step throws away the very tiles that should have stood in. */
    public static int BASE_TILE_CACHE_MB = 10;   // SDK default; raise to A/B the eviction theory
    /** Decoded-tile cache of the contour layer, in MB. The SDK default is 10, which a traced
     *  contour tile fills fast - and a tile evicted from it is a tile that DISAPPEARS when it should
     *  have stood in for the one still loading. */
    public static int CONTOUR_TILE_CACHE_MB = 10; // SDK default; raise to A/B the eviction theory
    /** Contours are generated only at or above this TILE zoom. */
    public static int CONTOUR_MIN_VISIBLE_ZOOM = 5;
    /** Fetch neighbour DEM tiles so lines meet across tile borders (up to 3 extra fetches/tile). */
    public static boolean CONTOUR_SEAMLESS_EDGES = true;
    public static int CONTOUR_MAX_OVERZOOM = 15;
    /** Emit only short label stubs (tangram's contour label generator) instead of traced contour
     *  geometry. Pair it with HILLSHADE_CONTOUR_LINES, which draws the lines in the shader for
     *  free, and with CONTOUR_LABEL_INTERVAL matching HILLSHADE_CONTOUR_INTERVAL. */
    public static boolean CONTOUR_LABEL_STUBS = false;
    /** Contour interval of the label stubs in meters; 0 follows the zoom ladder of the traced
     *  geometry. Must match the interval the shader draws or the labels sit between the lines. */
    public static float CONTOUR_LABEL_INTERVAL = 0f;
    /** Generate the label stubs from the TERRAIN's elevation grid (tangram's model) instead of
     *  loading and decoding a DEM tile of the contour source's own. Stubs only. */
    public static boolean CONTOUR_STUBS_FROM_TERRAIN = true;

    /** Font of the pre-baked contour tile labels. An inline CartoCSS string carries no font asset
     *  package, so this goes through the system-font fallback ("Arial" -> Roboto on Android). */
    public static String CONTOUR_TILES_FONT = "Arial";

    // =============================================================================================
    // INLINE STYLE KNOBS (StyleSource.INLINE / PROJECT - see DemoStyles)
    // =============================================================================================

    public static String INLINE_BACKGROUND_COLOR = "#eef2f0";
    /** Extrusion fill, so the mapbox colour match can be driven from adb (--es bldColor). */
    public static String INLINE_BUILDING_COLOR = "#d9cfc4";
    /** Extrude buildings: this is what gives the shadow pass real 3D casters. */
    public static boolean INLINE_BUILDINGS_3D = false;

    /**
     * The `buildings` style parameter of a COMPILED style (dir/zip/project), which a converted
     * Mapbox Standard uses as 0 = none, 1 = footprints, 2 = extrusions. Empty leaves the style's
     * own default. `--es bld3d` sets it too, so one key turns 3D buildings off whichever style is
     * loaded - it used to reach the inline style only.
     */
    public static String STYLE_BUILDINGS = "";
    /**
     * The `building_tilt_drop` style parameter of a converted MapBox style: how far, in PERCENT,
     * the extrusions are flattened between tilt 80 and 90 (90 = a tenth of their height left).
     * Empty leaves the style's own default. Live - a style parameter is a redraw, not a re-decode.
     */
    public static String STYLE_TILT_DROP = "";
    /** A converted style's `building_ao` parameter: 0 turns the ground contact shadows off. Live. */
    public static String STYLE_AO = "";
    /** Line widths of the inline style, as CartoCSS expressions - so they can be made
     *  zoom-dependent for testing how a line behaves as you zoom and tilt. The defaults widen
     *  with zoom the way a real style does; pass a plain number to pin a width instead. */
    public static String INLINE_ROAD_WIDTH = "linear([view::zoom], (12, 0.6), (18, 4.0))";
    public static String INLINE_MOTORWAY_WIDTH = "linear([view::zoom], (12, 1.5), (18, 9.0))";
    public static String INLINE_CONTOUR_WIDTH = "linear([view::zoom], (12, 0.4), (18, 1))";
    /** Extrusion lighting declared BY THE STYLE (needs --es styleLight true). Sun + ambient sum in
     *  linear space, mapbox's fill-extrusion model, so 0.5/0.5 is full colour in direct sun. */
    public static float INLINE_BUILDING_LIGHT = 0.5f;
    public static float INLINE_BUILDING_AMBIENT = 0.5f;
    public static float INLINE_BUILDING_GRADIENT = 0f;
    public static float INLINE_BUILDING_GRADIENT_HEIGHT = 20f;
    /** Contact shadow on the ground around a footprint, in metres. Radius 0 = off. */
    public static float INLINE_BUILDING_AO_RADIUS = 4f;
    public static float INLINE_BUILDING_AO_INTENSITY = 0.2f;
    /** Metres between subdivisions along a wall of the contact shadow. 0 = the terrain grid cell.
     *  Raise it to see the chord artifact on a slope, lower it to kill it. */
    public static float INLINE_BUILDING_AO_STEP = 0f;
    /** Exponent of the falloff: occlusion = (1 - d)^k. Higher keeps it tighter to the wall. */
    public static float INLINE_BUILDING_AO_ATTENUATION = 1.75f;
    /** Bevel at the roof edge, in metres. 0 = a hard 90 degrees, which is the default. */
    public static float INLINE_BUILDING_EDGE_RADIUS = 0f;
    /** Roofs multiplied by this. 1 = untouched; below 1 is the mapbox look. */
    public static float INLINE_BUILDING_ROOF_SHADE = 1f;
    /** 0 = the bevel is a flat facet with its own tone, tracing a rim around every roof. */
    public static float INLINE_BUILDING_ROUNDED_ROOF = 1f;
    /** Extrusion height in meters. Same vertex count at any value: the knob that separates the
     *  extrusion pass's fill cost from its vertex cost. */
    public static float INLINE_BUILDING_HEIGHT = 14f;
    /** Move sun/shadow/fog INTO the style (Map block properties) instead of setting them in code. */
    public static boolean INLINE_STYLE_LIGHTING = false;
    /** Text rules of the inline style ('--es labels false' isolates the label pipeline's cost). */
    public static boolean INLINE_LABELS = true;
    /** Strip the inline style down to the Map background plus the composite slot blocks
     *  ('--es minimal true'). Nothing of the vector data is drawn, so what is left on screen is the
     *  terrain and whatever the slots put on it - which is how the hillshade's own cost is measured
     *  without the base map's geometry dominating the frame. The base layer stays, because it is
     *  what gives the drape its tile cover. */
    public static boolean INLINE_STYLE_MINIMAL = false;
    /** Opacity of the ground-shaped fills (#landcover, #landuse). 1 = opaque, today's behaviour.
     *  Tangram draws these translucent whenever something under them has to show: their
     *  'translucent-polygons' style is alpha 0.25 (res/scenes/hillshade.yaml), selected through
     *  global.earth_style, and it is how the hillshade and the contours read through the map
     *  instead of being painted over. An un-subdivided fill also floats above the ground by its
     *  chord error, and a translucent one hides far less of what it floats over.
     *  '--es landcoverOpacity 0.25' */
    /** LAYER-level comp-op on the '#landcover' block of the inline style ("multiply", "screen",
     *  "darken", ...). Empty = none. A layer comp-op is what routes a layer through the renderer's
     *  overlay buffer, which is also where the stencil tile masks are re-stamped - so this is the
     *  knob that exercises that path. */
    public static String INLINE_COMP_OP = "";

    // ---- shield test style (StyleSource.POI) ----
    /** Sides the POI/place names may be laid out on, in preference order. Empty = the fixed layout
     *  a shield always had. '--es poiAnchors "right,left,top,bottom"' */
    public static String POI_ANCHORS = "right,left,top,bottom";
    /** Draw the icon alone when no side is free, instead of dropping the whole label. */
    public static boolean POI_TEXT_OPTIONAL = true;
    /** Gap between the icon and the name, in pixels - MIRRORED with the side the name is on. */
    public static float POI_TEXT_DX = 2f;
    /** Font icon instead of a bitmap shield: a glyph of assets/style/fonts/osm.ttf. */
    public static boolean POI_FONT_ICON = true;
    /** Bitmap shield (assets/style/shields/place.svg) instead of / besides the font icon. */
    public static boolean POI_BITMAP_ICON = false;
    /** How the lines of a wrapped name are justified: 'auto' follows the side the name is on,
     *  'left'/'right' are mirrored with it, 'middle' (or empty) centres them as before. */
    public static String POI_TEXT_ALIGN = "auto";
    /** A plate behind the name / behind the icon ('--es poiTextBg true --es poiIconBg true'). */
    public static boolean POI_TEXT_BG = false;
    public static boolean POI_ICON_BG = false;
    /** Shared plate geometry, in screen pixels. Border 0 = no border. */
    public static float POI_BG_RADIUS = 3f;
    public static float POI_BG_PADDING = 3f;
    public static float POI_BG_BORDER = 0f;
    /** Where a name wraps, in pixels - low values are how the line justification gets tested. */
    public static float POI_WRAP_WIDTH = 90f;

    public static float INLINE_LANDCOVER_OPACITY = 1.0f;
    public static int INLINE_SATELLITE_MIN_ZOOM = 11;
    public static String INLINE_HILLSHADE_SHADOW_COLOR = "#473B24";
    public static float INLINE_HILLSHADE_ILLUMINATION = 365f;
    /** Flip the 'show_relief' style parameter every N ms (StyleSource.PROJECT). 0 = do not flip. */
    public static int PARAM_TOGGLE_INTERVAL_MS = 3000;

    // =============================================================================================
    // ROUTE TEST LAYER (LAYER_ROUTE_TEST)
    // A navigation-style route over GeoJSON vector tiles, so it goes through the SAME line
    // tesselator and shaders as the base map's roads (a Line vector element does NOT - it has its
    // own tesselator in LineDrawData). Zoom out: that is where a miter turns into a needle.
    // =============================================================================================

    /** Route geometry: <data dir>/<name> if present, else the APK asset of the same name. */
    public static String ROUTE_TEST_GEOJSON_NAME = "route-test.geojson";
    /** Casing drawn under the route (Google-Maps look). 0 = no casing. */
    public static float ROUTE_TEST_CASE_WIDTH = 16f;
    public static float ROUTE_TEST_WIDTH = 10f;
    public static String ROUTE_TEST_COLOR = "#4285F4";      // Google-navigation blue
    public static String ROUTE_TEST_CASE_COLOR = "#FFFFFF"; // white casing: the outline of the route
    /** miter | bevel | round. NOTE: the vt tesselator draws 'round' as a miter today. */
    public static String ROUTE_TEST_JOIN = "round";
    /** butt | square | round */
    public static String ROUTE_TEST_CAP = "round";
    /** CartoCSS line-miterlimit: miter length / line width above which the join falls back to a bevel. */
    public static float ROUTE_TEST_MITER_LIMIT = 4f;
    /** Simplify tolerance of the route source, in tile subpixels. Vertices closer together than the
     *  line is wide make every join fold over itself - the artifact reads as darker blobs on a
     *  translucent line, and it is why a route needs LESS geometry as it zooms out, not the same. */
    public static float ROUTE_TEST_SIMPLIFY = 0f;
    /** < 1 exposes the join over-blending: every overlapping triangle blends its alpha again. */
    public static float ROUTE_TEST_OPACITY = 1f;
    /** How the opacity is applied, which picks the renderer path that removes the join doubling:
     *  geom  = line-opacity, baked into the geometry colour -> the vt single-blend stencil pass;
     *  layer = layer opacity + comp-op -> the layer is drawn opaque into the overlay FBO and
     *          composited once (no seams, but a full-screen pass, and that buffer has no depth,
     *          so in 3D terrain the route stops being occluded by ridges). */
    public static String ROUTE_TEST_OPACITY_MODE = "geom";

    // =============================================================================================
    // STYLE REGRESSION REPROS (LAYER_BUGS)
    // Four reported style regressions, on synthetic GeoJSON around the start position, each with
    // the A/B switch the report gives. See DemoStyles.bugStyle for the CartoCSS.
    //
    //   #bugpoints  two label attachments on one point (BUG_ICON_MODE / BUG_LABEL_MODE):
    //               the ::label text disappears below a size the ::icon one does not.
    //   #bugsel     a 'back/' instance under the main line (BUG_BACK_OPACITY):
    //               back/line-opacity and the main line stops being drawn.
    //   #bugline    one translucent wide line (BUG_LINE_COLOR / BUG_LINE_WIDTH): breaks at joins,
    //               plus line labels (BUG_TEXT_ALLOW_OVERLAP / BUG_TEXT_CLIP) - allow-overlap
    //               alone routes the text down the CLIP path (no culler, tile-clipped).
    // =============================================================================================

    /** Style regression repro layer (synthetic GeoJSON + DemoStyles.bugStyle). */
    // FACADE API (#146) - the layer whose whole stack is built from one JSON spec.
    public static boolean LAYER_API_SOURCE = false;
    /** A preset name from API_LAYER_SPECS, or raw JSON. Presets exist because adb eats quotes. */
    public static String API_LAYER_SPEC = "raster";

    public static final java.util.Map<String, String> API_LAYER_SPECS = new java.util.HashMap<>();
    static {
        API_LAYER_SPECS.put("raster",
            "{\"type\":\"raster\",\"opacity\":0.8,"
            + "\"source\":{\"type\":\"memory-cache\",\"capacity\":33554432,"
            + "\"source\":{\"type\":\"http\",\"minZoom\":0,\"maxZoom\":19,"
            + "\"url\":\"https://tile.openstreetmap.org/{z}/{x}/{y}.png\"}}}");
        API_LAYER_SPECS.put("composite",
            "{\"type\":\"composite-vector\",\"opacity\":0.5,"
            + "\"source\":{\"type\":\"http\",\"minZoom\":0,\"maxZoom\":14,"
            + "\"url\":\"https://example.com/{z}/{x}/{y}.mvt\"},"
            + "\"style\":{\"type\":\"mbvt\",\"cartocss\":"
            + "{\"type\":\"cartocss\",\"css\":\"#water{polygon-fill:#0000ff;}\"}}}");
        API_LAYER_SPECS.put("solid", "{\"type\":\"solid\",\"color\":-16776961}");
        // A style PROJECT, which had no spec form at all before: an asset package plus the name of
        // one style inside it.
        API_LAYER_SPECS.put("project",
            "{\"type\":\"vector\","
            + "\"source\":{\"type\":\"http\",\"minZoom\":0,\"maxZoom\":14,"
            + "\"url\":\"https://tiles.akylas.fr/data/france/{z}/{x}/{y}.pbf\"},"
            + "\"style\":{\"type\":\"mbvt\",\"project\":{\"type\":\"project\","
            + "\"assets\":{\"type\":\"dir\",\"path\":\"/sdcard/alpimaps_mbtiles/osm\"},"
            + "\"name\":\"voyager\"}}}");
    }

    /** The spec to build, resolving a preset name if that is what was given. */
    public static String apiLayerSpec() {
        String named = API_LAYER_SPECS.get(API_LAYER_SPEC);
        return named != null ? named : API_LAYER_SPEC;
    }

    public static boolean LAYER_BUGS = false;

    /** ::icon attachment: glyph = a real icon under the label, empty = text-name '' as reported,
     *  none = no ::icon attachment at all (the reported "works" control). */
    public static String BUG_ICON_MODE = "glyph";
    /** Where the small text lives: attachment = ::label, inline = the same properties on the rule
     *  (the other reported "works" control). */
    public static String BUG_LABEL_MODE = "attachment";
    public static float BUG_ICON_SIZE = 20f;
    /** The reported threshold: <= 10 the ::label text is gone, 11 and up it draws. */
    public static float BUG_LABEL_SIZE = 10f;

    /** 'back/' instance width bump over the main line, as the reported rule has it. */
    public static float BUG_SEL_WIDTH = 6f;
    /** The report has this white; on a light basemap a punched-out red line over an invisible white
     *  one looks like nothing was drawn at all, so the demo paints the back line. */
    public static String BUG_BACK_COLOR = "#1f6feb";
    /** back/line-opacity. < 0 omits the property, which is the case that draws correctly. */
    public static float BUG_BACK_OPACITY = 0.6f;

    /** Translucent on purpose: alpha < 1 is what turns the vt single-blend stencil pass on. */
    public static String BUG_LINE_COLOR = "#00000077";
    public static float BUG_LINE_WIDTH = 10f;
    /** Line labels along #bugline (the text-allow-overlap case). */
    public static boolean BUG_LINE_LABEL = true;
    public static float BUG_TEXT_SIZE = 14f;
    public static boolean BUG_TEXT_ALLOW_OVERLAP = true;
    /** unset | true | false. Unset is the reported case: mapnikvt defaults clip to allow-overlap. */
    public static String BUG_TEXT_CLIP = "unset";
    /** text-placement of the line labels: 'line' lies flat on the map, 'billboard-line' follows the
     *  same line but stays upright. Tilt the camera - at tilt 90 (top down) they look the same. */
    public static String BUG_TEXT_PLACEMENT = "line";
    public static float BUG_TEXT_DY = 12f;
    /** text-spacing: 0 (the CartoCSS default) is ONE label for the whole line on the label path,
     *  and one per SEGMENT on the clip path. Above 0 both repeat every spacing + text width. */
    public static float BUG_TEXT_SPACING = 0f;
    /** text-min-distance. Only does anything on the label path with allow-overlap off. */
    public static float BUG_TEXT_MIN_DISTANCE = 0f;

    // =============================================================================================
    // ROUTE SELECTION BENCH (LAYER_ROUTE_SELECT)
    // Many routes served as GeoJSON vector tiles, with ONE of them selected through a style
    // parameter that is compared with a feature field - the shape every real route style uses:
    //
    //     @is_selected: [param::selected_id] = [osmid] + '';
    //
    // The point of the bench is what a selection change costs. Two modes, so the two halves can be
    // told apart:
    //   value  - the parameter only feeds line-color / line-width, i.e. the APPEARANCE. Nothing
    //            about which geometry exists changes, so this is the half that could repaint.
    //   filter - adds the 'when (...)::selected' casing attachment of a real style, which decides
    //            whether the casing geometry EXISTS. That half can only be answered by decoding.
    // Tap a route to select it, or let it cycle with routeSelectCycle.
    // =============================================================================================

    /** Number of routes generated around the start position. */
    public static int ROUTE_SELECT_COUNT = 12;
    /** Extent of the fan of routes, in degrees. */
    public static float ROUTE_SELECT_SPAN = 0.05f;
    /** Vertices per route: more geometry per feature = more vertex bytes to repoint on selection. */
    public static int ROUTE_SELECT_VERTICES = 60;
    public static float ROUTE_SELECT_WIDTH = 5f;
    /** Width added to the selected route (the appearance half of a real selection). */
    public static float ROUTE_SELECT_WIDTH_BUMP = 4f;
    public static String ROUTE_SELECT_COLOR = "#3388ff";
    public static String ROUTE_SELECT_SELECTED_COLOR = "#ff3b00";
    /** value = colour/width only; filter = also the 'when (...)::selected' casing of a real style. */
    public static String ROUTE_SELECT_MODE = "value";
    /** Style parameter holding the selected id, declared by the in-memory project. */
    public static String ROUTE_SELECT_PARAMETER = "selected_id";
    /** Select the next route every N ms. 0 = only on tap / the panel button. */
    public static int ROUTE_SELECT_CYCLE_MS = 0;

    // =============================================================================================
    // MANEUVER ARROWS (LAYER_MANEUVERS)
    // The turn arrow of a navigation app: ManeuverArrowBuilder cuts it out of a route geometry
    // and serves it as ONE vector tile layer ('maneuver'), styled below like any other layer. The
    // offline routing test fills it - run it with the layer on and the arrows appear on the turns.
    // =============================================================================================

    /** Metres of the route kept before and after the maneuver point. */
    public static float MANEUVER_LENGTH_BEFORE = 30f;
    public static float MANEUVER_LENGTH_AFTER = 30f;
    public static float MANEUVER_WIDTH = 8f;
    public static float MANEUVER_CASING_WIDTH = 13f;
    public static String MANEUVER_COLOR = "#FFFFFF";
    public static String MANEUVER_CASING_COLOR = "#1A73E8";
    /** Arrow head, in multiples of the line width: 'line-end-arrow' builds it into the line itself,
     *  so the casing rule outlines the head as evenly as it outlines the shaft. Because it is a
     *  multiple of the width, the head follows the width scaling below by itself. */
    public static float MANEUVER_ARROW_WIDTH = 2.4f;
    public static float MANEUVER_ARROW_LENGTH = 1.9f;
    /** Custom arrow head outline, an SVG-style path (M/L/Z, absolute) in multiples of the line
     *  width: x along the line, y across it. Empty = the built-in triangle. The contour is a
     *  SKELETON - what is drawn is half a line width larger all round, which is what gives the
     *  casing rule an even border without any arithmetic, whatever the shape.
     *  Try a swallow-tail chevron: '--es maneuverPath "M-0.5,-1.2 L1.4,0 L-0.5,1.2 L0.1,0"'. */
    public static String MANEUVER_ARROW_PATH = "";
    /** SVG file whose first path becomes the head: a name in the data directory, or 'asset:<name>'
     *  for one bundled in the APK. Empty = the built-in triangle. The panel cycles through whatever
     *  .svg files sit in the data directory, so a head can be tried with a single adb push:
     *    adb push my-head.svg /sdcard/alpimaps_mbtiles/
     *  MANEUVER_ARROW_PATH above wins over this when both are set. */
    public static String MANEUVER_ARROW_SVG = "";
    /** The SVG head bundled in the APK, last in the panel's cycle. */
    public static String MANEUVER_SVG_ASSET = "maneuver-head-cloud.svg";
    /** Applied to the fitted contour, about its centre: a multiplier on the box, and a rotation in
     *  degrees clockwise - an icon drawn pointing up rather than along the line needs 90. */
    public static float MANEUVER_ARROW_SCALE = 1f;
    public static float MANEUVER_ARROW_ROTATION = 0f;
    /** The widths above are exact at REF zoom and shrink to MIN_SCALE of them at MIN zoom, so the
     *  whole arrow - shaft, head and border - gets smaller together as the camera pulls back and
     *  the arrow stays readable as an arrow instead of covering the junction. */
    public static float MANEUVER_ZOOM_REF = 17f;
    public static float MANEUVER_ZOOM_MIN = 12f;
    public static float MANEUVER_MIN_SCALE = 0.3f;

    // =============================================================================================
    // GEOJSON TILE-BUILD BENCH (DemoTests.runGeoJSONBench)
    // Times GeoJSONVectorTileDataSource with no renderer in the way. '--es geojsonBench many|long|
    // both|<name>' runs it at startup; the panel has a button for each dataset.
    // =============================================================================================

    /** Empty = off. many | long | both | an asset (or data-directory) file name. */
    public static String GEOJSON_BENCH = "";
    /** MANY OBJECTS: 5000 short routes, ~165k points - the per-tile feature scan. */
    public static String GEOJSON_BENCH_MANY_NAME = "bench-many-routes.geojson";
    /** LONG LINES: 8 routes of 100-250 km, ~300k points - re-clipping a long line per tile. */
    public static String GEOJSON_BENCH_LONG_NAME = "bench-long-routes.geojson";
    public static int GEOJSON_BENCH_MIN_ZOOM = 8;
    public static int GEOJSON_BENCH_MAX_ZOOM = 17;
    /** Tiles per side around the data centre, at every zoom. 4 -> 16 tiles x 10 zooms = 160 tiles. */
    public static int GEOJSON_BENCH_TILES_PER_SIDE = 4;
    /** Adds a bench dataset as a REAL layer (route style) instead of timing tile builds, so the
     *  RENDER cost can be panned through. Empty = off. many | long | both | a file name. */
    public static String GEOJSON_BENCH_LAYER = "";
    // =============================================================================================
    // MARKER POPUP FONTS (DemoTests.runPopupFonts)
    // A BalloonPopup per font list, to see what a font name resolves to on THIS device. The popups
    // draw with the platform text API, not with the tile labels' FreeType path, so this is the
    // only place the two font stacks can be compared side by side.
    // '--es popupFonts sample' at startup, or the panel button; '--es popupFonts "A|B,C"' for a
    // list of your own ('|' separates the popups, each one a CSS-like font list).
    // =============================================================================================

    /** Empty = off at startup. sample | a '|' separated list of font lists. */
    public static String POPUP_FONTS = "";
    /** What 'sample' shows: one popup per capability, in order. */
    public static String POPUP_FONTS_SAMPLE =
            "Roboto"                                                          // a named system font
            + "|serif"                                                        // a generic family
            + "|monospace"
            + "|HelveticaNeue-Light"                                          // a weight of a family
            + "|NoSuchFont, monospace"                                        // list: 1st is skipped
            + "|android:sans-serif-light, ios:HelveticaNeue-Light, windows:Segoe UI Light";

    /** Simplify tolerance of the bench source, in tile subpixels. This is the SDK default (1.0),
     *  NOT the route test's 0: at 0 nothing is dropped at any zoom, and 5000 routes then reach the
     *  terrain lattice at full resolution - measured 24M indices/frame and 4 fps on the Crosscall,
     *  against 15.7 fps at tolerance 2. Set it to 0 to reproduce that worst case on purpose. */
    public static float GEOJSON_BENCH_SIMPLIFY = 1.0f;

    // =============================================================================================
    // DEBUG / HARNESS
    // =============================================================================================

    /** false = no panel and no overlay text: clean screenshots for automated rendering checks. */
    /** Outline every tile each layer draws, on the ground: colour per zoom, brightness alternating
     *  with the tile parity, half opacity for a tile standing in with another tile's data. */
    public static boolean DEBUG_TILE_BORDERS = false;

    /** Drive the GL thread continuously instead of MapView's RENDERMODE_WHEN_DIRTY. */
    public static boolean CONTINUOUS_RENDER = false;

    public static boolean UI_ENABLED = true;
    /** PeakFinder-style relief outline post-process effect. */
    public static boolean RELIEF_OUTLINE = false;
    /** Delay before switching the effect on, in ms: attaching it before the GL surface exists
     *  leaves the offscreen colour buffer unwritten and the screen black. */
    public static float RELIEF_OUTLINE_DELAY_MS = 8000;
    /** Shaded terrain surface (TerrainOptions surface shader): the ground the outline draws on.
     *  Only visible where no tile layer paints, so pair it with base/satellite/hillshade off. */
    public static boolean RELIEF_SURFACE = false;
    /** Relief palette: false = ink on paper (light), true = paper on ink (dark / AR). */
    public static boolean RELIEF_DARK = false;
    /** Palette of the whole relief view, dark and light. The names, their plate, the shaded
     *  surface, the ink lines and the sky all read from these, so one switch changes the lot. */
    public static int RELIEF_INK_LIGHT = 0xff14141a;
    public static int RELIEF_INK_DARK = 0xffe8ecf5;
    public static int RELIEF_PAPER_LIGHT = 0xfff7f7f4;
    public static int RELIEF_PAPER_DARK = 0xff10131a;
    public static int RELIEF_SHADE_LIGHT = 0xff6c7280;
    public static int RELIEF_SHADE_DARK = 0xff5a6070;
    /** Sky behind the relief; alpha 0 makes it see-through, which is what AR mode wants. */
    public static int RELIEF_SKY_LIGHT = 0xff9fc6e8;
    public static int RELIEF_SKY_DARK = 0xff070a12;
    /** Surface shading: how far the slopes go from the paper colour towards the shade colour. */
    public static float RELIEF_SHADE_STRENGTH = 0.55f;
    /** Surface shading: light left on a slope facing away from the sun. */
    public static float RELIEF_AMBIENT = 0.35f;
    /** How much of the shading distance washes out (0..1), over RELIEF_HAZE_DISTANCE metres. */
    public static float RELIEF_HAZE = 0.7f;
    public static float RELIEF_HAZE_DISTANCE = 60000;
    /** Outline: base line width in pixels and the extra width at the far plane. */
    public static float RELIEF_OUTLINE_WIDTH = 1.2f;
    public static float RELIEF_HORIZON_BOOST = 2.5f;
    /** Outline: silhouette sensitivity and the strength of the ridge/valley lines. */
    public static float RELIEF_DEPTH_THRESHOLD = 1.0f;
    public static float RELIEF_CREASE_STRENGTH = 0.6f;

    /** The whole peak-finder view in one switch: relief surface + outline + peak names, and
     *  every map layer off (the surface only shows where no tile layer paints). */
    public static boolean PEAK_FINDER = false;
    /** Tilt the mode sets, in SDK convention - 90 is straight down, so a panorama is a low tilt. */
    public static float PEAK_FINDER_TILT = 25;
    /** Metres the viewpoint is lifted above the ground, driven by the on-screen elevation widget. */
    public static float PEAK_FINDER_ELEVATION = 0;
    public static float PEAK_FINDER_ELEVATION_STEP = 200;
    /** How far behind the terrain a label anchor may sit and still be labelled, as a fraction of
     *  its distance. The peak-finder view wants this generous: a summit right ON a ridge, or a
     *  metre behind it, is exactly what the view is for. 0.02 is the SDK default. */
    public static float PEAK_FINDER_OCCLUSION_TOLERANCE = 0.15f;
    /** View distance the mode asks for, as a multiple of tangram's rule: a panorama is the case
     *  their rule answers badly, and above 1 the far plane follows the extra ground. */
    public static float PEAK_FINDER_VIEW_DISTANCE = 3;
    /** Fly-in: the elevation the viewpoint climbs to, the zoom it lands at and how long it takes.
     *  0 seconds lets the SDK derive the duration from the length of the path. */
    public static float PEAK_FINDER_FLY_ELEVATION = 1000;
    public static float PEAK_FINDER_FLY_ZOOM = 13.6f;
    public static float PEAK_FINDER_FLY_DURATION = 3.5f;
    /** Extra height at the middle of the fly-in, in metres: the viewpoint climbs over the way
     *  there like a plane instead of rising straight to its final elevation. */
    public static float PEAK_FINDER_FLY_CLIMB = 1500;
    /** AR: the relief view over the camera preview. Dark palette, a transparent clear colour and a
     *  translucent GL surface (the map is then composited over the preview), the sky off, and the
     *  device's orientation driving the camera. */
    public static boolean AR_MODE = false;
    public static boolean AR_ORIENTATION = true;
    public static boolean AR_CAMERA = true;
    /** All labels pinned to the top of the screen instead of a band lower down. The two looks
     *  differ by which CORNER of the label the row is aligned on: a band lower down hangs the
     *  names off their bottom left corner (they read up and to the right), a pinned row hangs
     *  them off their top right corner so the text stays under the screen edge. */
    public static boolean PEAKS_PIN_TOP = true;
    /** How far below the top of the screen that row sits, as a fraction of the screen height. */
    public static float PEAKS_TOP_OFFSET = 0.03f;

    /** Summit names as callout labels (their own vector tile layer on the base source). */
    public static boolean LAYER_PEAKS = false;
    public static int PEAKS_MIN_ZOOM = 8;
    public static float PEAKS_TEXT_SIZE = 16;
    /** Rotation of the label text, degrees. The peak-finder look tilts them off the leader line. */
    public static float PEAKS_TEXT_ANGLE = 55;
    /** Where the label band sits, as a fraction of the screen height from the top. */
    public static float PEAKS_BAND = 0.25f;
    /** Shortest leader line, and the height of one stacking row, in pixels. */
    public static float PEAKS_MIN_OFFSET = 10;
    public static float PEAKS_ROW_STEP = 26;
    /** 1 = one strict row: every name on the same line, and a summit that cannot get a slot there
     *  loses its name to a better ranked one. Above 1 the losers stack instead. */
    public static int PEAKS_MAX_ROWS = 1;
    /** Pixels two names must stay apart (text-min-distance). It is what thins a crowded ridge out:
     *  a summit that cannot find a row far enough from the ones already placed loses its name to
     *  them, and which one wins is the ranking below. 0 = only overlap counts. */
    public static float PEAKS_MIN_DISTANCE = 14;
    /** Placement passes a name already on screen may fail before it is hidden (text-callout-persist).
     *  A panning map re-places its labels whenever its tile set changes, so without a grace a name
     *  that loses its row for one pass blinks out and back in. Keep it small: a name held over is
     *  a name drawn where the culler could not place it, so a generous grace shows as overlap. */
    public static int PEAKS_PERSIST = 2;
    public static float PEAKS_LINE_WIDTH = 1;
    /** Which point of the label the leader line ends at (and which is held over the summit), and
     *  which point sits on the band line: "" | center | bottom-left | top-right | ... Empty keeps
     *  the label around its own anchor. Left empty here - PEAKS_PIN_TOP picks the pair. */
    public static String PEAKS_LINE_ANCHOR = "";
    public static String PEAKS_ALIGN = "";
    /** Elevation set after the name in a smaller font (text-secondary-*): its size relative to the
     *  name, the gap before it and its baseline shift, all style properties. 0 scale = no suffix. */
    public static int PEAKS_ELE_COLOR_ARGB = 0xff6b7280; // the elevation reads as a subtitle
    public static float PEAKS_ELE_SCALE = 0.62f;
    public static float PEAKS_ELE_GAP = 3;
    public static float PEAKS_ELE_DY = 0;
    /** Metres of distance worth one rank point when the culler decides which names to keep
     *  ([ele] - [view::distance]/this). 0 = rank by elevation alone. */
    public static float PEAKS_DISTANCE_RANK = 100;
    /** Plate behind each name: colour, opacity, corner radius and padding (style properties, so
     *  they work in any CartoCSS style, not just this one). */
    public static int PEAKS_BG_COLOR_ARGB = 0xffffffff;
    public static float PEAKS_BG_OPACITY = 0.85f;
    public static float PEAKS_BG_RADIUS = 6;
    public static float PEAKS_BG_PADDING_X = 5;
    public static float PEAKS_BG_PADDING_Y = 2;
    /** Metres beyond which a summit is not labelled at all; 0 = no limit. */
    public static float PEAKS_MAX_DISTANCE = 120000;
    /** Scripted camera move so animation artifacts can be captured with adb screenrecord:
     *  "" | zoom | pan | rotate | zoomseq | approach. */
    public static String ANIM = "";
    /** 'approach': the zoom the close approach dives to before panning and pulling back out. */
    public static float ANIM_APPROACH_ZOOM = 17.5f;
    public static float ANIM_DELAY_MS = 12000;
    public static float ANIM_DURATION_S = 8;
    public static float ANIM_ZOOM_DELTA = 3;
    public static float ANIM_LON_DELTA = 0.05f;
    /** North/south component of the scripted pan. Panning north into the mountains is the case
     *  that gets slow, and it exercises quite different work from panning over the valley. */
    public static float ANIM_LAT_DELTA = 0f;
    public static float ANIM_ROTATION = 180;
    public static float ANIM_ZOOM_OUT = 10.2f;
    public static float ANIM_SETTLE_MS = 8000;

    // =============================================================================================
    // INTENT OVERRIDES - the complete key list. Keys are historical, keep them stable: scripts and
    // AI debugging sessions use them.
    // =============================================================================================

    public static void applyIntentOverrides() {
        // what is shown
        BASE_MODE = DemoCfg.cfgEnum("base", BASE_MODE, BaseMode.class);              // --es base plain|composite
        STYLE_SOURCE = DemoCfg.cfgEnum("style", STYLE_SOURCE, StyleSource.class);    // --es style dir|zip|inline|project
        // legacy 'demo' names kept working: terrain = plain base + terrain, nuti = style project
        String demo = DemoCfg.cfg("demo");
        if ("terrain".equals(demo)) {
            BASE_MODE = BaseMode.PLAIN;
            STYLE_SOURCE = StyleSource.INLINE;
            LAYER_HILLSHADE = true;
            LAYER_CONTOUR = true;
        } else if ("project".equals(demo) || "nuti".equals(demo)) {
            BASE_MODE = BaseMode.COMPOSITE;
            STYLE_SOURCE = StyleSource.PROJECT;
        } else if ("composite".equals(demo)) {
            BASE_MODE = BaseMode.COMPOSITE;
        }

        // layers
        LAYER_BASE = DemoCfg.cfgBool("map", LAYER_BASE);
        LAYER_HILLSHADE = DemoCfg.cfgBool("hillshade", LAYER_HILLSHADE);
        LAYER_CONTOUR = DemoCfg.cfgBool("contourLayer", LAYER_CONTOUR);
        LAYER_CONTOUR_TILES = DemoCfg.cfgBool("contourTiles", LAYER_CONTOUR_TILES);
        LAYER_SATELLITE = DemoCfg.cfgBool("satLayer", LAYER_SATELLITE);
        LAYER_HYPSO = DemoCfg.cfgBool("hypso", LAYER_HYPSO);
        LAYER_ELEMENTS = DemoCfg.cfgBool("elements", LAYER_ELEMENTS);
        LAYER_ROUTES = DemoCfg.cfgBool("routes", LAYER_ROUTES);
        LAYER_ROUTE_TEST = DemoCfg.cfgBool("routeTest", LAYER_ROUTE_TEST);
        LAYER_ROUTE_SELECT = DemoCfg.cfgBool("routeSelect", LAYER_ROUTE_SELECT);
        ROUTE_SELECT_COUNT = DemoCfg.cfgInt("routeSelectCount", ROUTE_SELECT_COUNT);
        ROUTE_SELECT_MODE = DemoCfg.cfgStr("routeSelectMode", ROUTE_SELECT_MODE);
        ROUTE_SELECT_CYCLE_MS = DemoCfg.cfgInt("routeSelectCycle", ROUTE_SELECT_CYCLE_MS);
        ROUTE_SELECT_WIDTH = DemoCfg.cfgFloat("routeSelectWidth", ROUTE_SELECT_WIDTH);
        LAYER_MANEUVERS = DemoCfg.cfgBool("maneuvers", LAYER_MANEUVERS);
        LAYER_API_SOURCE = DemoCfg.cfgBool("apiSource", LAYER_API_SOURCE);
        API_LAYER_SPEC = DemoCfg.cfgStr("apiLayerSpec", API_LAYER_SPEC);
        LAYER_BUGS = DemoCfg.cfgBool("bugs", LAYER_BUGS);

        // style regression repros
        BUG_ICON_MODE = DemoCfg.cfgStr("bugIcon", BUG_ICON_MODE);
        BUG_LABEL_MODE = DemoCfg.cfgStr("bugLabel", BUG_LABEL_MODE);
        BUG_ICON_SIZE = DemoCfg.cfgFloat("bugIconSize", BUG_ICON_SIZE);
        BUG_LABEL_SIZE = DemoCfg.cfgFloat("bugLabelSize", BUG_LABEL_SIZE);
        BUG_SEL_WIDTH = DemoCfg.cfgFloat("bugSelWidth", BUG_SEL_WIDTH);
        BUG_BACK_COLOR = DemoCfg.cfgColor("bugBackColor", BUG_BACK_COLOR);
        BUG_BACK_OPACITY = DemoCfg.cfgFloat("bugBackOpacity", BUG_BACK_OPACITY);
        BUG_LINE_COLOR = DemoCfg.cfgColor("bugLineColor", BUG_LINE_COLOR);
        BUG_LINE_WIDTH = DemoCfg.cfgFloat("bugLineWidth", BUG_LINE_WIDTH);
        BUG_LINE_LABEL = DemoCfg.cfgBool("bugLineLabel", BUG_LINE_LABEL);
        BUG_TEXT_SIZE = DemoCfg.cfgFloat("bugTextSize", BUG_TEXT_SIZE);
        BUG_TEXT_ALLOW_OVERLAP = DemoCfg.cfgBool("bugAllowOverlap", BUG_TEXT_ALLOW_OVERLAP);
        BUG_TEXT_CLIP = DemoCfg.cfgStr("bugTextClip", BUG_TEXT_CLIP);
        BUG_TEXT_PLACEMENT = DemoCfg.cfgStr("bugTextPlacement", BUG_TEXT_PLACEMENT);
        BUG_TEXT_DY = DemoCfg.cfgFloat("bugTextDy", BUG_TEXT_DY);
        BUG_TEXT_SPACING = DemoCfg.cfgFloat("bugTextSpacing", BUG_TEXT_SPACING);
        BUG_TEXT_MIN_DISTANCE = DemoCfg.cfgFloat("bugTextMinDistance", BUG_TEXT_MIN_DISTANCE);

        // composite slots ('hs', 'sat', 'contour' are the historical keys)
        COMPOSITE_HILLSHADE = DemoCfg.cfgBool("hs", COMPOSITE_HILLSHADE);
        COMPOSITE_SATELLITE = DemoCfg.cfgBool("sat", COMPOSITE_SATELLITE);
        COMPOSITE_CONTOUR = DemoCfg.cfgBool("contour", COMPOSITE_CONTOUR);
        LANDFORM_URL = DemoCfg.cfgStr("landformUrl", LANDFORM_URL);
        LANDFORM_SLOT = DemoCfg.cfgStr("landformSlot", LANDFORM_SLOT);
        LANDFORM_MAX_ZOOM = DemoCfg.cfgInt("landformMaxZoom", LANDFORM_MAX_ZOOM);
        COMPOSITE_SINGLE_PASS = DemoCfg.cfgBool("singlePass", COMPOSITE_SINGLE_PASS);
        COMPOSITE_HILLSHADE_ZOOM_BIAS = DemoCfg.cfgFloat("hsBias", COMPOSITE_HILLSHADE_ZOOM_BIAS);

        // sources
        VECTOR_URL = DemoCfg.cfgStr("vectorUrl", VECTOR_URL);
        VECTOR_MAX_ZOOM = DemoCfg.cfgInt("vectorMaxZoom", VECTOR_MAX_ZOOM);
        VECTOR_ZOOM_BIAS = DemoCfg.cfgFloat("vectorZoomBias", VECTOR_ZOOM_BIAS);
        DEM_URL = DemoCfg.cfgStr("demUrl", DEM_URL);
        DEM_MAX_ZOOM = DemoCfg.cfgInt("demMaxZoom", DEM_MAX_ZOOM);
        DEM_ENCODING = DemoCfg.cfgStr("demEncoding", DEM_ENCODING);
        DEM2_URL = DemoCfg.cfgStr("dem2Url", DEM2_URL);
        DEM2_MAX_ZOOM = DemoCfg.cfgInt("dem2MaxZoom", DEM2_MAX_ZOOM);
        DEM2_ENCODING = DemoCfg.cfgStr("dem2Encoding", DEM2_ENCODING);
        RASTER_URL = DemoCfg.cfgStr("rasterUrl", RASTER_URL);
        CONTOUR_TILES_URL = DemoCfg.cfgStr("contourTilesUrl", CONTOUR_TILES_URL);
        CONTOUR_TILES_MAX_ZOOM = DemoCfg.cfgInt("contourTilesMaxZoom", CONTOUR_TILES_MAX_ZOOM);
        STYLE_DIR_NAME = DemoCfg.cfgStr("styleDir", STYLE_DIR_NAME);
        STYLE_ZIP_NAME = DemoCfg.cfgStr("styleZip", STYLE_ZIP_NAME);
        STYLE_ASSETS_PATH = DemoCfg.cfgStr("styleAssets", STYLE_ASSETS_PATH);
        STYLE_ASSETS_NAME = DemoCfg.cfgStr("styleName", STYLE_ASSETS_NAME);
        LIGHT_PRESET = DemoCfg.cfgStr("lightPreset", LIGHT_PRESET);
        PERSISTENT_CACHE_MB = DemoCfg.cfgInt("cacheMb", PERSISTENT_CACHE_MB);
        DEM_PERSISTENT_CACHE_MB = DemoCfg.cfgInt("demCacheMb", DEM_PERSISTENT_CACHE_MB);

        // camera
        START_LON = DemoCfg.cfgFloat("lon", (float) START_LON);
        START_LAT = DemoCfg.cfgFloat("lat", (float) START_LAT);
        START_ZOOM = DemoCfg.cfgFloat("zoom", START_ZOOM);
        START_TILT = DemoCfg.cfgFloat("tilt", START_TILT);
        START_ROTATION = DemoCfg.cfgFloat("rotation", START_ROTATION);

        // terrain
        TILE_THREAD_POOL_SIZE = DemoCfg.cfgInt("tilePool", TILE_THREAD_POOL_SIZE);
        TILE_LOD_FACTOR = DemoCfg.cfgFloat("lodFactor", TILE_LOD_FACTOR);
        TILE_LOD_GRAZING = DemoCfg.cfgFloat("lodGrazing", TILE_LOD_GRAZING);
        LABEL_MAX_DISTANCE = DemoCfg.cfgFloat("labelMaxDistance", LABEL_MAX_DISTANCE);
        TERRAIN_ENABLED = DemoCfg.cfgBool("terrain", TERRAIN_ENABLED);
        TERRAIN_CAMERA_CLEARANCE = DemoCfg.cfgFloat("clearance", TERRAIN_CAMERA_CLEARANCE);
        TERRAIN_EXAGGERATION = DemoCfg.cfgFloat("exaggeration", TERRAIN_EXAGGERATION);
        AUTO_FLATTEN_PARALLAX = DemoCfg.cfgFloat("autoFlatten", AUTO_FLATTEN_PARALLAX);
        AUTO_FLATTEN_TILT = DemoCfg.cfgFloat("autoFlattenTilt", AUTO_FLATTEN_TILT);
        AUTO_FLATTEN_MS = (long) DemoCfg.cfgFloat("autoFlattenMs", AUTO_FLATTEN_MS);
        TERRAIN_FULL_SWITCH = DemoCfg.cfgBool("fullSwitch", TERRAIN_FULL_SWITCH);
        TERRAIN_ANIM_MS = (long) DemoCfg.cfgFloat("terrainAnimMs", TERRAIN_ANIM_MS);
        TERRAIN_MESH_RESOLUTION = DemoCfg.cfgInt("meshResolution", TERRAIN_MESH_RESOLUTION);
        TERRAIN_DRAPE_FILLS = DemoCfg.cfgBool("drape", TERRAIN_DRAPE_FILLS);
        TERRAIN_DRAPE_LINES = DemoCfg.cfgBool("drapeLines", TERRAIN_DRAPE_LINES);
        TERRAIN_DRAPE_RESOLUTION = DemoCfg.cfgInt("drapeResolution", TERRAIN_DRAPE_RESOLUTION);
        TERRAIN_NO_DRAPE_FILTER = DemoCfg.cfgStr("noDrape", TERRAIN_NO_DRAPE_FILTER);
        TERRAIN_TILE_EDGE_STITCHING = DemoCfg.cfgBool("stitch", TERRAIN_TILE_EDGE_STITCHING);
        TERRAIN_SEAMLESS_TILE_EDGES = DemoCfg.cfgBool("seamlessEdges", TERRAIN_SEAMLESS_TILE_EDGES);
        TERRAIN_ELEVATION_PREFETCH = DemoCfg.cfgBool("prefetch", TERRAIN_ELEVATION_PREFETCH);
        TERRAIN_BILLBOARD_OCCLUSION = DemoCfg.cfgBool("occlusion", TERRAIN_BILLBOARD_OCCLUSION);
        TERRAIN_OCCLUSION_TOLERANCE = DemoCfg.cfgFloat("occlusionTolerance", TERRAIN_OCCLUSION_TOLERANCE);
        TERRAIN_TEXT_OCCLUSION_OPACITY = DemoCfg.cfgFloat("textOcclusion", TERRAIN_TEXT_OCCLUSION_OPACITY);
        INLINE_TEXT_OCCLUSION_OPACITY = DemoCfg.cfgFloat("roadLabelOcclusion", INLINE_TEXT_OCCLUSION_OPACITY);
        TERRAIN_BACKGROUND_BITMAP = DemoCfg.cfgBool("backgroundBitmap", TERRAIN_BACKGROUND_BITMAP);
        if (DemoCfg.cfg("maxTileZoomOffset") != null) {
            TERRAIN_MAX_TILE_ZOOM_OFFSET_ENABLED = true;
            TERRAIN_MAX_TILE_ZOOM_OFFSET = DemoCfg.cfgInt("maxTileZoomOffset", TERRAIN_MAX_TILE_ZOOM_OFFSET);
        }

        // fog / distance
        // The preset first: the individual extras below then override whatever it set.
        if (DemoCfg.cfg("fogPreset") != null) {
            applyFogPreset(DemoCfg.cfg("fogPreset"));
            FOG_ENABLED = true;
        }
        if (DemoCfg.cfg("fog") != null) {
            FOG_ENABLED = !"false".equals(DemoCfg.cfg("fog"));
            FOG_COLOR_ARGB = DemoCfg.cfgColorInt("fog", FOG_COLOR_ARGB);
        }
        FOG_SOURCE = DemoCfg.cfgStr("fogSource", FOG_SOURCE);
        FOG_RANGE_START = DemoCfg.cfgFloat("fogRangeStart", FOG_RANGE_START);
        FOG_RANGE_END = DemoCfg.cfgFloat("fogRangeEnd", FOG_RANGE_END);
        FOG_HIGH_COLOR_ARGB = DemoCfg.cfgColorInt("fogHigh", FOG_HIGH_COLOR_ARGB);
        FOG_SPACE_COLOR_ARGB = DemoCfg.cfgColorInt("fogSpace", FOG_SPACE_COLOR_ARGB);
        FOG_STAR_INTENSITY = DemoCfg.cfgFloat("fogStars", FOG_STAR_INTENSITY);
        FOG_HORIZON_BLEND = DemoCfg.cfgFloat("fogBlend", FOG_HORIZON_BLEND);
        FOG_VERTICAL_START = DemoCfg.cfgFloat("fogVertStart", FOG_VERTICAL_START);
        FOG_VERTICAL_END = DemoCfg.cfgFloat("fogVertEnd", FOG_VERTICAL_END);
        VIEW_DISTANCE_FACTOR = DemoCfg.cfgFloat("viewDistance", VIEW_DISTANCE_FACTOR);
        VIEW_DISTANCE_METERS = DemoCfg.cfgFloat("viewDistanceMeters", VIEW_DISTANCE_METERS);
        TERRAIN_MAX_TILE_ZOOM_COARSENING = DemoCfg.cfgInt("coarsening", TERRAIN_MAX_TILE_ZOOM_COARSENING);

        // sun / shadows
        TERRAIN_LIGHTING = DemoCfg.cfgBool("terrainLight", TERRAIN_LIGHTING);
        SUN_HOUR_UTC = DemoCfg.cfgFloat("sunHour", SUN_HOUR_UTC);
        FREE_ROAM_MODE = DemoCfg.cfgStr("freeRoam", FREE_ROAM_MODE);
        if ("true".equals(FREE_ROAM_MODE)) {
            FREE_ROAM_MODE = "look";
        } else if ("false".equals(FREE_ROAM_MODE)) {
            FREE_ROAM_MODE = "off";
        }
        PANNING_SPEED_MODE = DemoCfg.cfgStr("panSpeed", PANNING_SPEED_MODE);
        FREE_ROAM_LOOK_SENSITIVITY = DemoCfg.cfgFloat("lookSensitivity", FREE_ROAM_LOOK_SENSITIVITY);
        FREE_ROAM_MOVE_SPEED = DemoCfg.cfgFloat("moveSpeed", FREE_ROAM_MOVE_SPEED);
        LOOK_UP_LIMIT = DemoCfg.cfgFloat("lookUp", LOOK_UP_LIMIT);
        CELESTIAL = DemoCfg.cfgBool("celestial", CELESTIAL);
        CELESTIAL_SUN = DemoCfg.cfgBool("celestialSun", CELESTIAL_SUN);
        CELESTIAL_MOON = DemoCfg.cfgBool("celestialMoon", CELESTIAL_MOON);
        CELESTIAL_ARC = DemoCfg.cfgBool("celestialArc", CELESTIAL_ARC);
        CELESTIAL_MOON_ARC = DemoCfg.cfgBool("celestialMoonArc", CELESTIAL_MOON_ARC);
        CELESTIAL_MOON_PHASE = DemoCfg.cfgBool("celestialMoonPhase", CELESTIAL_MOON_PHASE);
        CELESTIAL_SUN_SIZE = DemoCfg.cfgFloat("celestialSunSize", CELESTIAL_SUN_SIZE);
        CELESTIAL_MOON_SIZE = DemoCfg.cfgFloat("celestialMoonSize", CELESTIAL_MOON_SIZE);
        CELESTIAL_ARC_WIDTH = DemoCfg.cfgFloat("celestialArcWidth", CELESTIAL_ARC_WIDTH);
        STARS = DemoCfg.cfgBool("stars", STARS);
        STARS_STARS = DemoCfg.cfgBool("starsStars", STARS_STARS);
        STARS_FIGURES = DemoCfg.cfgBool("starsFigures", STARS_FIGURES);
        STARS_PLANETS = DemoCfg.cfgBool("starsPlanets", STARS_PLANETS);
        STARS_EQUATOR = DemoCfg.cfgBool("starsEquator", STARS_EQUATOR);
        STARS_BRIGHTEST_SIZE = DemoCfg.cfgFloat("starsSize", STARS_BRIGHTEST_SIZE);
        STARS_FIGURE_WIDTH = DemoCfg.cfgFloat("starsFigureWidth", STARS_FIGURE_WIDTH);
        STARS_PLANET_SIZE = DemoCfg.cfgFloat("starsPlanetSize", STARS_PLANET_SIZE);
        STARS_LABELS = DemoCfg.cfgBool("starsLabels", STARS_LABELS);
        STARS_LABEL_SCALE = DemoCfg.cfgFloat("starsLabelScale", STARS_LABEL_SCALE);
        STAR_SKY = DemoCfg.cfgBool("starSky", STAR_SKY);
        SKY_TYPE = DemoCfg.cfgStr("skyType", SKY_TYPE);
        SKY_QUALITY = DemoCfg.cfgStr("skyQuality", SKY_QUALITY);
        SKY_ATMO_SUN = DemoCfg.cfgFloat("skyAtmoSun", SKY_ATMO_SUN);
        SKY_ATMO_COLOR_ARGB = DemoCfg.cfgColorInt("skyAtmoColor", SKY_ATMO_COLOR_ARGB);
        SKY_ATMO_HALO_ARGB = DemoCfg.cfgColorInt("skyAtmoHalo", SKY_ATMO_HALO_ARGB);
        SKY_ATMO_LUMINANCE = DemoCfg.cfgFloat("skyAtmoLum", SKY_ATMO_LUMINANCE);
        STAR_SKY_FADE_MS = DemoCfg.cfgFloat("starSkyFade", STAR_SKY_FADE_MS);
        STAR_SKY_ORIENTATION = DemoCfg.cfgBool("starSkyOrientation", STAR_SKY_ORIENTATION);
        STAR_SKY_TRANSLUCENT = DemoCfg.cfgBool("starSkyTranslucent", STAR_SKY_TRANSLUCENT);
        STAR_SKY_CAMERA = DemoCfg.cfgBool("starSkyCamera", STAR_SKY_CAMERA);
        SUN_YEAR = DemoCfg.cfgInt("sunYear", SUN_YEAR);
        SUN_MONTH = DemoCfg.cfgInt("sunMonth", SUN_MONTH);
        SUN_DAY = DemoCfg.cfgInt("sunDay", SUN_DAY);
        SUN_AZIMUTH = DemoCfg.cfgFloat("sunAzimuth", SUN_AZIMUTH);
        SUN_ALTITUDE = DemoCfg.cfgFloat("sunAltitude", SUN_ALTITUDE);
        APP_SUN = DemoCfg.cfgBool("appSun", APP_SUN);
        SUN_INTENSITY = DemoCfg.cfgFloat("sunIntensity", SUN_INTENSITY);
        AMBIENT_INTENSITY = DemoCfg.cfgFloat("ambient", AMBIENT_INTENSITY);
        AMBIENT_COLOR_ARGB = DemoCfg.cfgColorInt("ambientColor", AMBIENT_COLOR_ARGB);
        SUN_COLOR_ARGB = DemoCfg.cfgColorInt("sunColor", SUN_COLOR_ARGB);
        SHADOW_STRENGTH = DemoCfg.cfgFloat("shadow", SHADOW_STRENGTH);
        SHADOW_SOFTNESS = DemoCfg.cfgFloat("shadowSoftness", SHADOW_SOFTNESS);
        SHADOW_MAP_SIZE = DemoCfg.cfgInt("shadowMapSize", SHADOW_MAP_SIZE);
        SHADOW_CASCADES = DemoCfg.cfgInt("shadowCascades", SHADOW_CASCADES);
        SHADOW_BIAS = DemoCfg.cfgFloat("shadowBias", SHADOW_BIAS);
        SHADOW_NORMAL_OFFSET = DemoCfg.cfgFloat("shadowNormalOffset", SHADOW_NORMAL_OFFSET);
        SHADOW_DISTANCE = DemoCfg.cfgFloat("shadowDistance", SHADOW_DISTANCE);
        SHADOW_CASTER_MARGIN = DemoCfg.cfgInt("shadowMargin", SHADOW_CASTER_MARGIN);

        // sky
        SKY_ENABLED = DemoCfg.cfgBool("sky", SKY_ENABLED);
        DAY_CYCLE = DemoCfg.cfgBool("daycycle", DAY_CYCLE);
        DAY_CYCLE_HOUR = DemoCfg.cfgFloat("dayCycleHour", DAY_CYCLE_HOUR);

        // hillshade
        HILLSHADE_METHOD = DemoCfg.cfgStr("hsMethod", HILLSHADE_METHOD);
        HILLSHADE_CONTRAST = DemoCfg.cfgFloat("hsContrast", HILLSHADE_CONTRAST);
        HILLSHADE_HEIGHT_SCALE = DemoCfg.cfgFloat("hsHeightScale", HILLSHADE_HEIGHT_SCALE);
        HILLSHADE_EXAGGERATION = DemoCfg.cfgFloat("hsExaggeration", HILLSHADE_EXAGGERATION);
        HILLSHADE_ILLUMINATION_DEGREES = DemoCfg.cfgFloat("hsIllumination", HILLSHADE_ILLUMINATION_DEGREES);
        HILLSHADE_SHADOW_COLOR_ARGB = DemoCfg.cfgColorInt("hsShadowColor", HILLSHADE_SHADOW_COLOR_ARGB);
        HILLSHADE_CONTOUR_LINES = DemoCfg.cfgBool("hsContours", HILLSHADE_CONTOUR_LINES);
        HILLSHADE_CONTOUR_INTERVAL = DemoCfg.cfgFloat("hsContourInterval", HILLSHADE_CONTOUR_INTERVAL);
        HILLSHADE_SLOPES_SHADER = DemoCfg.cfgBool("slopes", HILLSHADE_SLOPES_SHADER);

        // contours
        CONTOUR_BASE_INTERVAL = DemoCfg.cfgFloat("contourInterval", CONTOUR_BASE_INTERVAL);
        CONTOUR_RESOLUTION = DemoCfg.cfgInt("contourResolution", CONTOUR_RESOLUTION);
        CONTOUR_SIMPLIFY_TOLERANCE = DemoCfg.cfgFloat("contourSimplify", CONTOUR_SIMPLIFY_TOLERANCE);
        CONTOUR_MIN_VISIBLE_ZOOM = DemoCfg.cfgInt("contourMinZoom", CONTOUR_MIN_VISIBLE_ZOOM);
        CONTOUR_INTERVAL_LADDER = DemoCfg.cfgStr("contourLadder", CONTOUR_INTERVAL_LADDER);
        CONTOUR_TILE_SUBSTITUTION = DemoCfg.cfgStr("contourSubst", CONTOUR_TILE_SUBSTITUTION);
        CONTOUR_MAX_OVERZOOM_STANDIN = DemoCfg.cfgInt("contourStandIn", CONTOUR_MAX_OVERZOOM_STANDIN);
        CONTOUR_TILE_CACHE_MB = DemoCfg.cfgInt("contourCacheMB", CONTOUR_TILE_CACHE_MB);
        BASE_TILE_CACHE_MB = DemoCfg.cfgInt("baseCacheMB", BASE_TILE_CACHE_MB);
        CONTOUR_RESOLUTION_LADDER = DemoCfg.cfgStr("contourResLadder", CONTOUR_RESOLUTION_LADDER);
        CONTOUR_SEAMLESS_EDGES = DemoCfg.cfgBool("contourSeamless", CONTOUR_SEAMLESS_EDGES);
        CONTOUR_LABEL_STUBS = DemoCfg.cfgBool("contourStubs", CONTOUR_LABEL_STUBS);
        CONTOUR_LABEL_INTERVAL = DemoCfg.cfgFloat("contourStubInterval", CONTOUR_LABEL_INTERVAL);
        CONTOUR_STUBS_FROM_TERRAIN = DemoCfg.cfgBool("stubsFromTerrain", CONTOUR_STUBS_FROM_TERRAIN);

        // route test layer
        ROUTE_TEST_GEOJSON_NAME = DemoCfg.cfgStr("routeGeojson", ROUTE_TEST_GEOJSON_NAME);
        ROUTE_TEST_WIDTH = DemoCfg.cfgFloat("routeWidth", ROUTE_TEST_WIDTH);
        ROUTE_TEST_CASE_WIDTH = DemoCfg.cfgFloat("routeCaseWidth", ROUTE_TEST_CASE_WIDTH);
        ROUTE_TEST_COLOR = DemoCfg.cfgColor("routeColor", ROUTE_TEST_COLOR);
        ROUTE_TEST_CASE_COLOR = DemoCfg.cfgColor("routeCaseColor", ROUTE_TEST_CASE_COLOR);
        ROUTE_TEST_JOIN = DemoCfg.cfgStr("routeJoin", ROUTE_TEST_JOIN);
        ROUTE_TEST_CAP = DemoCfg.cfgStr("routeCap", ROUTE_TEST_CAP);
        ROUTE_TEST_MITER_LIMIT = DemoCfg.cfgFloat("routeMiterLimit", ROUTE_TEST_MITER_LIMIT);
        ROUTE_TEST_OPACITY = DemoCfg.cfgFloat("routeOpacity", ROUTE_TEST_OPACITY);
        ROUTE_TEST_SIMPLIFY = DemoCfg.cfgFloat("routeSimplify", ROUTE_TEST_SIMPLIFY);
        ROUTE_TEST_OPACITY_MODE = DemoCfg.cfgStr("routeOpacityMode", ROUTE_TEST_OPACITY_MODE);

        // maneuver arrows
        MANEUVER_LENGTH_BEFORE = DemoCfg.cfgFloat("maneuverBefore", MANEUVER_LENGTH_BEFORE);
        MANEUVER_LENGTH_AFTER = DemoCfg.cfgFloat("maneuverAfter", MANEUVER_LENGTH_AFTER);
        MANEUVER_WIDTH = DemoCfg.cfgFloat("maneuverWidth", MANEUVER_WIDTH);
        MANEUVER_CASING_WIDTH = DemoCfg.cfgFloat("maneuverCaseWidth", MANEUVER_CASING_WIDTH);
        MANEUVER_COLOR = DemoCfg.cfgColor("maneuverColor", MANEUVER_COLOR);
        MANEUVER_CASING_COLOR = DemoCfg.cfgColor("maneuverCaseColor", MANEUVER_CASING_COLOR);
        MANEUVER_ARROW_WIDTH = DemoCfg.cfgFloat("maneuverArrowWidth", MANEUVER_ARROW_WIDTH);
        MANEUVER_ARROW_LENGTH = DemoCfg.cfgFloat("maneuverArrowLength", MANEUVER_ARROW_LENGTH);
        MANEUVER_ZOOM_REF = DemoCfg.cfgFloat("maneuverZoomRef", MANEUVER_ZOOM_REF);
        MANEUVER_ZOOM_MIN = DemoCfg.cfgFloat("maneuverZoomMin", MANEUVER_ZOOM_MIN);
        MANEUVER_MIN_SCALE = DemoCfg.cfgFloat("maneuverMinScale", MANEUVER_MIN_SCALE);
        MANEUVER_ARROW_PATH = DemoCfg.cfgStr("maneuverPath", MANEUVER_ARROW_PATH);
        MANEUVER_ARROW_SVG = DemoCfg.cfgStr("maneuverSvg", MANEUVER_ARROW_SVG);
        MANEUVER_ARROW_SCALE = DemoCfg.cfgFloat("maneuverPathScale", MANEUVER_ARROW_SCALE);
        MANEUVER_ARROW_ROTATION = DemoCfg.cfgFloat("maneuverPathRotation", MANEUVER_ARROW_ROTATION);
        GEOJSON_BENCH = DemoCfg.cfgStr("geojsonBench", GEOJSON_BENCH);
        GEOJSON_BENCH_MIN_ZOOM = DemoCfg.cfgInt("geojsonBenchMinZoom", GEOJSON_BENCH_MIN_ZOOM);
        GEOJSON_BENCH_MAX_ZOOM = DemoCfg.cfgInt("geojsonBenchMaxZoom", GEOJSON_BENCH_MAX_ZOOM);
        GEOJSON_BENCH_TILES_PER_SIDE = DemoCfg.cfgInt("geojsonBenchTiles", GEOJSON_BENCH_TILES_PER_SIDE);
        GEOJSON_BENCH_LAYER = DemoCfg.cfgStr("geojsonLayer", GEOJSON_BENCH_LAYER);
        GEOJSON_BENCH_SIMPLIFY = DemoCfg.cfgFloat("geojsonBenchSimplify", GEOJSON_BENCH_SIMPLIFY);
        POPUP_FONTS = DemoCfg.cfgStr("popupFonts", POPUP_FONTS);

        // inline style
        INLINE_BACKGROUND_COLOR = DemoCfg.cfgColor("bg", INLINE_BACKGROUND_COLOR);
        INLINE_BUILDING_COLOR = DemoCfg.cfgColor("bldColor", INLINE_BUILDING_COLOR);
        INLINE_BUILDINGS_3D = DemoCfg.cfgBool("bld3d", INLINE_BUILDINGS_3D);
        STYLE_BUILDINGS = DemoCfg.cfgStr("buildings", STYLE_BUILDINGS);
        STYLE_TILT_DROP = DemoCfg.cfgStr("bldTiltDrop", STYLE_TILT_DROP);
        STYLE_AO = DemoCfg.cfgStr("bldAo", STYLE_AO);
        if (STYLE_BUILDINGS.isEmpty() && DemoCfg.cfg("bld3d") != null) {
            STYLE_BUILDINGS = INLINE_BUILDINGS_3D ? "2" : "1";
        }
        INLINE_BUILDING_HEIGHT = DemoCfg.cfgFloat("bldHeight", INLINE_BUILDING_HEIGHT);
        INLINE_ROAD_WIDTH = DemoCfg.cfgStr("roadWidth", INLINE_ROAD_WIDTH);
        INLINE_MOTORWAY_WIDTH = DemoCfg.cfgStr("motorwayWidth", INLINE_MOTORWAY_WIDTH);
        INLINE_CONTOUR_WIDTH = DemoCfg.cfgStr("contourWidth", INLINE_CONTOUR_WIDTH);
        INLINE_BUILDING_LIGHT = DemoCfg.cfgFloat("bldLight", INLINE_BUILDING_LIGHT);
        INLINE_BUILDING_AMBIENT = DemoCfg.cfgFloat("bldAmbient", INLINE_BUILDING_AMBIENT);
        INLINE_BUILDING_GRADIENT = DemoCfg.cfgFloat("bldGradient", INLINE_BUILDING_GRADIENT);
        INLINE_BUILDING_GRADIENT_HEIGHT = DemoCfg.cfgFloat("bldGradientHeight", INLINE_BUILDING_GRADIENT_HEIGHT);
        INLINE_BUILDING_AO_RADIUS = DemoCfg.cfgFloat("bldAoRadius", INLINE_BUILDING_AO_RADIUS);
        INLINE_BUILDING_AO_INTENSITY = DemoCfg.cfgFloat("bldAoIntensity", INLINE_BUILDING_AO_INTENSITY);
        INLINE_BUILDING_AO_STEP = DemoCfg.cfgFloat("bldAoStep", INLINE_BUILDING_AO_STEP);
        INLINE_BUILDING_AO_ATTENUATION = DemoCfg.cfgFloat("bldAoAttenuation", INLINE_BUILDING_AO_ATTENUATION);
        INLINE_BUILDING_EDGE_RADIUS = DemoCfg.cfgFloat("bldEdgeRadius", INLINE_BUILDING_EDGE_RADIUS);
        INLINE_BUILDING_ROOF_SHADE = DemoCfg.cfgFloat("bldRoofShade", INLINE_BUILDING_ROOF_SHADE);
        INLINE_BUILDING_ROUNDED_ROOF = DemoCfg.cfgFloat("bldRoundedRoof", INLINE_BUILDING_ROUNDED_ROOF);
        INLINE_STYLE_LIGHTING = DemoCfg.cfgBool("styleLight", INLINE_STYLE_LIGHTING);
        INLINE_LABELS = DemoCfg.cfgBool("labels", INLINE_LABELS);
        INLINE_STYLE_MINIMAL = DemoCfg.cfgBool("minimal", INLINE_STYLE_MINIMAL);
        // 'none' rather than an empty value: 'adb shell am start --es key ""' drops the empty
        // argument and every extra AFTER it is then read one token off.
        POI_ANCHORS = DemoCfg.cfgStr("poiAnchors", POI_ANCHORS);
        if ("none".equalsIgnoreCase(POI_ANCHORS) || "off".equalsIgnoreCase(POI_ANCHORS)) {
            POI_ANCHORS = "";
        }
        POI_TEXT_OPTIONAL = DemoCfg.cfgBool("poiTextOptional", POI_TEXT_OPTIONAL);
        POI_TEXT_DX = DemoCfg.cfgFloat("poiTextDx", POI_TEXT_DX);
        POI_FONT_ICON = DemoCfg.cfgBool("poiFontIcon", POI_FONT_ICON);
        POI_BITMAP_ICON = DemoCfg.cfgBool("poiBitmapIcon", POI_BITMAP_ICON);
        POI_TEXT_ALIGN = DemoCfg.cfgStr("poiTextAlign", POI_TEXT_ALIGN);
        if ("none".equalsIgnoreCase(POI_TEXT_ALIGN)) {
            POI_TEXT_ALIGN = "";
        }
        POI_TEXT_BG = DemoCfg.cfgBool("poiTextBg", POI_TEXT_BG);
        POI_ICON_BG = DemoCfg.cfgBool("poiIconBg", POI_ICON_BG);
        POI_BG_RADIUS = DemoCfg.cfgFloat("poiBgRadius", POI_BG_RADIUS);
        POI_BG_PADDING = DemoCfg.cfgFloat("poiBgPadding", POI_BG_PADDING);
        POI_BG_BORDER = DemoCfg.cfgFloat("poiBgBorder", POI_BG_BORDER);
        POI_WRAP_WIDTH = DemoCfg.cfgFloat("poiWrapWidth", POI_WRAP_WIDTH);
        INLINE_LANDCOVER_OPACITY = DemoCfg.cfgFloat("landcoverOpacity", INLINE_LANDCOVER_OPACITY);
        INLINE_COMP_OP = DemoCfg.cfgStr("compOp", INLINE_COMP_OP);
        INLINE_SATELLITE_MIN_ZOOM = DemoCfg.cfgInt("satZoom", INLINE_SATELLITE_MIN_ZOOM);
        PARAM_TOGGLE_INTERVAL_MS = DemoCfg.cfgInt("paramInterval", PARAM_TOGGLE_INTERVAL_MS);

        // harness
        DEBUG_TILE_BORDERS = DemoCfg.cfgBool("tileBorders", DEBUG_TILE_BORDERS);
        CONTINUOUS_RENDER = DemoCfg.cfgBool("continuousRender", CONTINUOUS_RENDER);
        UI_ENABLED = DemoCfg.cfgBool("ui", UI_ENABLED);
        RELIEF_OUTLINE = DemoCfg.cfgBool("peakfinder", RELIEF_OUTLINE);
        RELIEF_OUTLINE_DELAY_MS = DemoCfg.cfgFloat("peakfinderDelay", RELIEF_OUTLINE_DELAY_MS);
        RELIEF_SURFACE = DemoCfg.cfgBool("reliefSurface", RELIEF_SURFACE);
        RELIEF_DARK = DemoCfg.cfgBool("reliefDark", RELIEF_DARK);
        RELIEF_SHADE_STRENGTH = DemoCfg.cfgFloat("reliefShade", RELIEF_SHADE_STRENGTH);
        RELIEF_AMBIENT = DemoCfg.cfgFloat("reliefAmbient", RELIEF_AMBIENT);
        RELIEF_HAZE = DemoCfg.cfgFloat("reliefHaze", RELIEF_HAZE);
        RELIEF_HAZE_DISTANCE = DemoCfg.cfgFloat("reliefHazeDistance", RELIEF_HAZE_DISTANCE);
        RELIEF_OUTLINE_WIDTH = DemoCfg.cfgFloat("reliefWidth", RELIEF_OUTLINE_WIDTH);
        RELIEF_HORIZON_BOOST = DemoCfg.cfgFloat("reliefHorizonBoost", RELIEF_HORIZON_BOOST);
        RELIEF_DEPTH_THRESHOLD = DemoCfg.cfgFloat("reliefThreshold", RELIEF_DEPTH_THRESHOLD);
        RELIEF_CREASE_STRENGTH = DemoCfg.cfgFloat("reliefCrease", RELIEF_CREASE_STRENGTH);
        PEAK_FINDER = DemoCfg.cfgBool("peakFinder", PEAK_FINDER);
        PEAK_FINDER_TILT = DemoCfg.cfgFloat("peakFinderTilt", PEAK_FINDER_TILT);
        PEAK_FINDER_ELEVATION = DemoCfg.cfgFloat("peakFinderElevation", PEAK_FINDER_ELEVATION);
        PEAK_FINDER_OCCLUSION_TOLERANCE = DemoCfg.cfgFloat("peakFinderOcclusion", PEAK_FINDER_OCCLUSION_TOLERANCE);
        PEAK_FINDER_VIEW_DISTANCE = DemoCfg.cfgFloat("peakFinderViewDistance", PEAK_FINDER_VIEW_DISTANCE);
        PEAK_FINDER_FLY_ELEVATION = DemoCfg.cfgFloat("peakFinderFlyElevation", PEAK_FINDER_FLY_ELEVATION);
        PEAK_FINDER_FLY_ZOOM = DemoCfg.cfgFloat("peakFinderFlyZoom", PEAK_FINDER_FLY_ZOOM);
        PEAK_FINDER_FLY_DURATION = DemoCfg.cfgFloat("peakFinderFlyDuration", PEAK_FINDER_FLY_DURATION);
        PEAK_FINDER_FLY_CLIMB = DemoCfg.cfgFloat("peakFinderFlyClimb", PEAK_FINDER_FLY_CLIMB);
        AR_MODE = DemoCfg.cfgBool("ar", AR_MODE);
        AR_ORIENTATION = DemoCfg.cfgBool("arOrientation", AR_ORIENTATION);
        AR_CAMERA = DemoCfg.cfgBool("arCamera", AR_CAMERA);
        PEAKS_PIN_TOP = DemoCfg.cfgBool("peaksPinTop", PEAKS_PIN_TOP);
        PEAKS_TOP_OFFSET = DemoCfg.cfgFloat("peaksTopOffset", PEAKS_TOP_OFFSET);
        LAYER_PEAKS = DemoCfg.cfgBool("peaks", LAYER_PEAKS);
        PEAKS_MIN_ZOOM = DemoCfg.cfgInt("peaksMinZoom", PEAKS_MIN_ZOOM);
        PEAKS_TEXT_SIZE = DemoCfg.cfgFloat("peaksTextSize", PEAKS_TEXT_SIZE);
        PEAKS_TEXT_ANGLE = DemoCfg.cfgFloat("peaksAngle", PEAKS_TEXT_ANGLE);
        PEAKS_BAND = DemoCfg.cfgFloat("peaksBand", PEAKS_BAND);
        PEAKS_MIN_OFFSET = DemoCfg.cfgFloat("peaksOffset", PEAKS_MIN_OFFSET);
        PEAKS_ROW_STEP = DemoCfg.cfgFloat("peaksStep", PEAKS_ROW_STEP);
        PEAKS_MAX_ROWS = DemoCfg.cfgInt("peaksRows", PEAKS_MAX_ROWS);
        PEAKS_MIN_DISTANCE = DemoCfg.cfgFloat("peaksMinDistance", PEAKS_MIN_DISTANCE);
        PEAKS_PERSIST = DemoCfg.cfgInt("peaksPersist", PEAKS_PERSIST);
        PEAKS_LINE_WIDTH = DemoCfg.cfgFloat("peaksLineWidth", PEAKS_LINE_WIDTH);
        PEAKS_LINE_ANCHOR = DemoCfg.cfgStr("peaksLineAnchor", PEAKS_LINE_ANCHOR);
        PEAKS_ALIGN = DemoCfg.cfgStr("peaksAlign", PEAKS_ALIGN);
        PEAKS_ELE_SCALE = DemoCfg.cfgFloat("peaksEleScale", PEAKS_ELE_SCALE);
        PEAKS_ELE_COLOR_ARGB = DemoCfg.cfgColorInt("peaksEleColor", PEAKS_ELE_COLOR_ARGB);
        PEAKS_ELE_GAP = DemoCfg.cfgFloat("peaksEleGap", PEAKS_ELE_GAP);
        PEAKS_ELE_DY = DemoCfg.cfgFloat("peaksEleDy", PEAKS_ELE_DY);
        PEAKS_DISTANCE_RANK = DemoCfg.cfgFloat("peaksDistanceRank", PEAKS_DISTANCE_RANK);
        PEAKS_BG_COLOR_ARGB = DemoCfg.cfgColorInt("peaksBgColor", PEAKS_BG_COLOR_ARGB);
        PEAKS_BG_OPACITY = DemoCfg.cfgFloat("peaksBgOpacity", PEAKS_BG_OPACITY);
        PEAKS_BG_RADIUS = DemoCfg.cfgFloat("peaksBgRadius", PEAKS_BG_RADIUS);
        PEAKS_MAX_DISTANCE = DemoCfg.cfgFloat("peaksMaxDistance", PEAKS_MAX_DISTANCE);
        ANIM = DemoCfg.cfgStr("anim", ANIM);
        ANIM_DELAY_MS = DemoCfg.cfgFloat("animDelay", ANIM_DELAY_MS);
        ANIM_DURATION_S = DemoCfg.cfgFloat("animDuration", ANIM_DURATION_S);
        ANIM_ZOOM_DELTA = DemoCfg.cfgFloat("animZoomDelta", ANIM_ZOOM_DELTA);
        ANIM_LON_DELTA = DemoCfg.cfgFloat("animLonDelta", ANIM_LON_DELTA);
        ANIM_LAT_DELTA = DemoCfg.cfgFloat("animLatDelta", ANIM_LAT_DELTA);
        ANIM_ROTATION = DemoCfg.cfgFloat("animRotation", ANIM_ROTATION);
        ANIM_ZOOM_OUT = DemoCfg.cfgFloat("animZoomOut", ANIM_ZOOM_OUT);
        ANIM_SETTLE_MS = DemoCfg.cfgFloat("animSettle", ANIM_SETTLE_MS);
        ANIM_APPROACH_ZOOM = DemoCfg.cfgFloat("animApproachZoom", ANIM_APPROACH_ZOOM);
    }

    /**
     * The UTC hour the sky is drawn for: the explicit sun hour if one is set (the panel's hour
     * slider writes it), then the day-cycle hour, and otherwise the real clock - so out of the box
     * the sun, the moon, the planets and the stars are where they are right now.
     */
    public static double currentHourUtc() {
        if (SUN_HOUR_UTC >= 0) {
            return SUN_HOUR_UTC;
        }
        if (DAY_CYCLE) {
            return DAY_CYCLE_HOUR;
        }
        return DemoAstro.nowUtc()[3];
    }

    private DemoConfig() {
    }
}
