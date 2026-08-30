package com.massifmaps.MassifDemo.examples.styles;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.Spec;
import com.massifmaps.api.Position;

/**
 * The hour drives the whole palette, and the curve that decides how is the app's to replace.
 */
@ExampleInfo(
    id = "day-cycle-light",
    title = "Light the map by the hour",
    description = "One palette, no night theme: the scene light is read off a curve of "
                + "`LightStop`s, and every colour on the map - 2D fills, labels, 3D walls - is "
                + "derived from it. Swap the curve and the whole map changes character.",
    section = Sections.STYLES,
    order = 20)
public class DayCycleLightExample extends MapExample {

    /**
     * Mapbox Standard reads MAPBOX's own vector tiles - its layers name mapbox-streets-v8 source
     * layers, so no other tileset can feed it. Put your own token here; a demo cannot ship one.
     */
    private static final String MAPBOX_TOKEN = "<your-mapbox-access-token>";

    /**
     * The two styles, both converted by `massif-style mapbox2css --live-light`: the colours stay as
     * the style authored them and the `*-emissive-strength` values ride along, for the SDK to light
     * at draw time. That is what leaves ONE palette covering every hour.
     */
    private static final String[][] STYLES = {
        { "Mapbox Standard", "mapbox-standard" },
        { "MapTiler Streets", "maptiler-streets" },
    };

    /**
     * A curve is a list of lights anchored on SUN HEIGHTS, and the SDK interpolates between them.
     * Empty means the built-in one, which is MapBox Standard's own four light setups - so a
     * converted Standard renders as its `day` preset at noon and its `dusk` preset at 19h with
     * nothing set here at all.
     *
     * The second is the same machinery pointed somewhere else entirely. Nothing about it is a
     * special case: the SDK derives the 2D grade, the 3D sun and ambient, and the brightness a
     * style ramps its labels over from whatever this returns.
     */
    private static final String PSYCHEDELIC =
        "["
        + "{\"sunAltitude\":-15,\"ambientColor\":\"#2d0a4e\",\"ambientIntensity\":0.7,"
        + "\"sunColor\":\"#00e5ff\",\"sunIntensity\":0.4},"
        + "{\"sunAltitude\":2,\"ambientColor\":\"#ff2d95\",\"ambientIntensity\":0.85,"
        + "\"sunColor\":\"#ff8a00\",\"sunIntensity\":0.6},"
        + "{\"sunAltitude\":25,\"ambientColor\":\"#7cff4f\",\"ambientIntensity\":0.9,"
        + "\"sunColor\":\"#ff00d4\",\"sunIntensity\":0.5},"
        + "{\"sunAltitude\":60,\"ambientColor\":\"#00fff0\",\"ambientIntensity\":1.0,"
        + "\"sunColor\":\"#fff700\",\"sunIntensity\":0.45}"
        + "]";

    /**
     * The BUILT-IN curve, written out. Passing an empty list selects exactly this, but the point of
     * an example is to show the shape - these are MapBox Standard's own four light setups at the
     * sun heights it states them for, and every one is an ordinary value an app can change. The
     * doubled twilight stop holds the preset flat from 3 to 12 degrees, so the sun passes THROUGH
     * dusk instead of crossing it.
     */
    private static final String MAPBOX =
        "["
        + "{\"sunAltitude\":-9,\"ambientColor\":\"#464d69\",\"ambientIntensity\":0.5,"
        + "\"sunColor\":\"#3f4455\",\"sunIntensity\":0.5},"
        + "{\"sunAltitude\":3,\"ambientColor\":\"#363e5e\",\"ambientIntensity\":0.8,"
        + "\"sunColor\":\"#fec286\",\"sunIntensity\":0.2},"
        + "{\"sunAltitude\":12,\"ambientColor\":\"#363e5e\",\"ambientIntensity\":0.8,"
        + "\"sunColor\":\"#fec286\",\"sunIntensity\":0.2},"
        + "{\"sunAltitude\":38,\"ambientColor\":\"#ffffff\",\"ambientIntensity\":0.8,"
        + "\"sunColor\":\"#ffffff\",\"sunIntensity\":0.2}"
        + "]";

    private static final String[][] FORMULAS = {
        { "Mapbox", MAPBOX },
        { "Psychedelic", PSYCHEDELIC },
    };

    /**
     * The SUN HEIGHT is what the curve is anchored on, so it is what the slider sweeps.
     *
     * An hour is one step further away: it has to be turned into a height first, and a day's worth
     * of hours crosses the twilight band (3 to 12 degrees up) in about 33 minutes - 2.3% of a 0-24
     * slider - so dawn and dusk are unreachable by dragging one. Sweeping the height instead lands
     * on every preset exactly, which is also how a render is compared against MapBox's own.
     */
    private static final float START_ALTITUDE = 10f;

    /** MapBox's four presets, as the sun heights the curve anchors them at. */
    private static final float[] PRESET_ALTITUDES = { 40f, 70f, 10f, -30f };
    private static final boolean[] PRESET_RISING = { true, false, false, false };
    private static final String[] PRESET_NAMES = { "dawn", "day", "dusk", "night" };
    private int preset = 2;

    private ExampleHost host;
    private int style;
    private int formula;
    private float sunAltitude = START_ALTITUDE;
    private boolean rising;

    @Override
    public void onStart(ExampleHost host) {
        this.host = host;
        MassifMap map = host.map();

        // How far a TILTED far field may coarsen. The LOD area test drops a tile a level for
        // distance and again for the grazing angle it is seen at; unbounded, the second term makes
        // the horizon band jump between levels as the camera turns, so one side of the screen keeps
        // its buildings and the other loses them. This caps the grazing half alone - distance still
        // coarsens freely, which is what keeps the far field cheap.
        map.options().set("tileLODForeshorteningLimit", 1.0);

        buildLayer(map);

        // A TERRAIN, for the shadows. Cast shadows are drawn from the drape pass and land on the
        // terrain surface - with no terrain there is no surface to receive them and nothing casts
        // at all, however high shadowStrength goes. Paris is flat, so this is here for the light
        // rather than for the relief.
        map.terrain(Spec.of("terrain")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("mapterhorn-dem.db"))
                .set("capacity", 200 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.mapterhorn.com/{z}/{x}/{y}.webp")
                    .set("minZoom", 1)
                    .set("maxZoom", 16)
                    // Picks the elevation decoder per tile; without it the SDK assumes mapbox
                    // encoding and terrarium heights come out in the hundreds of kilometres.
                    .set("metaData", Spec.object().set("dem_encoding", "terrarium")))))
           .apply(Spec.object().set("exaggeration", 1).set("cameraClearance", 40));

        // The curve is only read while this is on; off, the style's and the app's own sun colours
        // stand, which is what every map did before the curve existed.
        map.light(Spec.of("light")
            .set("dayCycleLightsEnabled", true)
            .set("sunOverridingStyle", true)
            // Without this the ground is never lit, and the shadow multiply lives in the same
            // block - so the buildings cast nothing. The style says `colors-prelit`, so lighting
            // the ground here does NOT light its colours twice: it only lets the shadows land.
            .set("terrainLightingEnabled", true)
            // Buildings cast: a low sun is what the curve is most worth looking at, and it is also
            // when the shadows are longest. They follow the same sun the curve reads, so they
            // stretch and swing round as the hour is swept.
            .set("shadowStrength", 0.35)
            .set("shadowSoftness", 1.2));

        applyFormula();
        applyHour();
        map.camera().moveTo(new Position(2.3376, 48.8600), 17.2f, 20f, 45f);

        host.button("Style", new Runnable() {
            @Override
            public void run() {
                style = (style + 1) % STYLES.length;
                buildLayer(DayCycleLightExample.this.host.map());
                DayCycleLightExample.this.host.caption(describe());
            }
        });
        host.button("Formula", new Runnable() {
            @Override
            public void run() {
                formula = (formula + 1) % FORMULAS.length;
                applyFormula();
                DayCycleLightExample.this.host.caption(describe());
            }
        });
        // A slider, because the curve is continuous: sweeping it is what shows the sun passing
        // THROUGH dusk rather than jumping between four presets.
        host.slider("Sun", -30f, 70f, START_ALTITUDE, new ExampleHost.OnValue() {
            @Override
            public void onValue(float value) {
                sunAltitude = value;
                applyHour();
                DayCycleLightExample.this.host.caption(describe());
            }
        });
        // Straight to MapBox's own four, so the render can be held against theirs. Each sets the
        // AZIMUTH too: nothing but the direction of travel separates dawn from dusk at the same
        // height, and the SDK reads that from the sun's easting.
        host.button("Preset", new Runnable() {
            @Override
            public void run() {
                preset = (preset + 1) % PRESET_ALTITUDES.length;
                sunAltitude = PRESET_ALTITUDES[preset];
                rising = PRESET_RISING[preset];
                applyHour();
                DayCycleLightExample.this.host.caption(PRESET_NAMES[preset] + " - " + describe());
            }
        });
        host.caption("Two styles, two formulas: the hour picks the light, the curve picks the look.");
    }

    /**
     * The basemap. Each style needs the tiles it was written against - Standard names mapbox's own
     * source layers, MapTiler Streets names OpenMapTiles ones, and neither reads the other's.
     */
    private void buildLayer(MassifMap map) {
        // Off the stack AND out of the registry: an id is unique, so rebuilding "basemap" without
        // this fails with RESULT_DUPLICATE_ID.
        map.removeLayer("basemap");
        boolean mapbox = style == 0;
        Spec source = mapbox
            ? Spec.of("http")
                .set("url", "https://api.mapbox.com/v4/mapbox.mapbox-streets-v8,mapbox.mapbox-terrain-v2"
                          + "/{z}/{x}/{y}.vector.pbf?access_token=" + MAPBOX_TOKEN)
                .set("maxZoom", 16)
            : Spec.of("http")
                .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                .set("maxZoom", 14)
                .set("HTTPHeaders", Spec.object().set("User-Agent", "MassifMapsExamples/1.0"));

        map.addLayer("basemap", Spec.of("vector")
            // Cached on disk in front of the server: both are other people's tiles, and a demo that
            // gets panned around re-fetches the same ones on every run.
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath(mapbox ? "mapbox-vector.db" : "openfreemap.db"))
                .set("capacity", 100 * 1024 * 1024)
                .set("source", source))
            .set("style", Spec.of("mbvt")
                .set("project", Spec.of("project")
                    .set("assets", Spec.of("zip")
                        .set("data", Spec.of("url")
                            .set("url", "assets://styles/" + STYLES[style][1] + ".zip"))))));
    }

    /**
     * The whole formula, in one property. An empty list is the built-in curve; a list of stops
     * replaces it, and everything the SDK derives from the light follows without a re-decode -
     * the tiles are untouched, so this is a redraw.
     */
    private void applyFormula() {
        host.map().light().apply(Spec.object()
            .set("dayCycleLightStops", FORMULAS[formula][1]));
    }

    private String describe() {
        return String.format(java.util.Locale.US, "sun %.0f\u00b0 %s - %s on %s",
            sunAltitude, rising ? "rising" : "setting", FORMULAS[formula][0], STYLES[style][0]);
    }

    /** The curve reads a sun POSITION. East of north is morning, which is what picks dawn. */
    private void applyHour() {
        host.map().light().apply(Spec.object()
            .set("sunAzimuth", rising ? 90.0 : 270.0)
            .set("sunAltitude", sunAltitude));
    }
}
