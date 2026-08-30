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

    private static final String[][] FORMULAS = {
        // An EMPTY list is the built-in curve. Not "", which reads as "no value" through some
        // bindings - the property has to arrive as a list for the SDK to clear the old one.
        { "Mapbox", "[]" },
        { "Psychedelic", PSYCHEDELIC },
    };

    /** The hour is swept, not picked: the curve is continuous and that is the point of it. */
    private static final float START_HOUR = 17.4f;

    private ExampleHost host;
    private int style;
    private int formula;
    private float hour = START_HOUR;

    @Override
    public void onStart(ExampleHost host) {
        this.host = host;
        MassifMap map = host.map();

        buildLayer(map);

        // The curve is only read while this is on; off, the style's and the app's own sun colours
        // stand, which is what every map did before the curve existed.
        map.light(Spec.of("light")
            .set("dayCycleLightsEnabled", true)
            .set("sunOverridingStyle", true)
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
                DayCycleLightExample.this.host.caption(FORMULAS[formula][0] + " formula on " + STYLES[style][0]);
            }
        });
        host.button("Formula", new Runnable() {
            @Override
            public void run() {
                formula = (formula + 1) % FORMULAS.length;
                applyFormula();
                DayCycleLightExample.this.host.caption(FORMULAS[formula][0] + " formula on " + STYLES[style][0]);
            }
        });
        // A slider, because the curve is continuous: sweeping it is what shows the sun passing
        // THROUGH dusk rather than jumping between four presets.
        host.slider("Hour", 0f, 24f, START_HOUR, new ExampleHost.OnValue() {
            @Override
            public void onValue(float value) {
                hour = value;
                applyHour();
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
            // MapBox's tiles are 512 px and this SDK's are 256, so at the same view it would ask
            // for a level DEEPER than mapbox-gl does - and a level deeper carries a level's worth
            // of extra labels. The converted style already reads ([view::zoom] - 1); this is the
            // fetch side of the same offset.
            .set("zoomLevelBias", mapbox ? -1.0 : 0.0)
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

    /** An hour is a sun POSITION; the curve turns that into a light. */
    private void applyHour() {
        double altitude = 62.0 * Math.sin(Math.PI * (hour - 6.0) / 12.0);
        double azimuth = 90.0 + (hour - 6.0) * 15.0;
        host.map().light().apply(Spec.object()
            .set("sunAzimuth", azimuth)
            .set("sunAltitude", altitude));
    }
}
