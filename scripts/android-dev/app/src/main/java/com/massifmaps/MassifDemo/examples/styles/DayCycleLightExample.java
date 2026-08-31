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
     * The BUILT-IN curves, written out - MapBox Standard's four `lights` blocks, byte for byte, at
     * the sun heights the SDK anchors them at. Passing empty lists selects exactly these; the point
     * of an example is to show the shape.
     *
     * TWO curves, because Standard's `dawn` and `dusk` presets are different lights at the same sun
     * height, and nothing but the direction of travel tells them apart: the SDK reads the setting
     * one while the sun is west (`sunDir.x < 0`) and the rising one while it is east. Setting only
     * `dayCycleLightStops` is what made a morning render as dusk.
     *
     * The doubled twilight stop holds the preset FLAT from 3 to 12 degrees, so the sun passes
     * through dawn or dusk instead of crossing it - that band, plus below -9 and above 38, is where
     * the curve returns a preset exactly rather than a blend of two.
     */
    private static final String MAPBOX_SETTING =
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

    /** The same curve with Standard's `dawn` light in the twilight band instead of its `dusk` one. */
    private static final String MAPBOX_RISING =
        "["
        + "{\"sunAltitude\":-9,\"ambientColor\":\"#464d69\",\"ambientIntensity\":0.5,"
        + "\"sunColor\":\"#3f4455\",\"sunIntensity\":0.5},"
        + "{\"sunAltitude\":3,\"ambientColor\":\"#ffecdc\",\"ambientIntensity\":0.75,"
        + "\"sunColor\":\"#feca8b\",\"sunIntensity\":0.5},"
        + "{\"sunAltitude\":12,\"ambientColor\":\"#ffecdc\",\"ambientIntensity\":0.75,"
        + "\"sunColor\":\"#feca8b\",\"sunIntensity\":0.5},"
        + "{\"sunAltitude\":38,\"ambientColor\":\"#ffffff\",\"ambientIntensity\":0.8,"
        + "\"sunColor\":\"#ffffff\",\"sunIntensity\":0.2}"
        + "]";

    /** { name, setting curve, rising curve } - one curve for both when a formula has no dawn. */
    private static final String[][] FORMULAS = {
        { "Mapbox", MAPBOX_SETTING, MAPBOX_RISING },
        { "Psychedelic", PSYCHEDELIC, PSYCHEDELIC },
    };

    /** Paris, and the camera the example opens on. */
    private static final double LON = 2.3376;
    private static final double LAT = 48.8600;

    /**
     * The EQUINOX, as its Julian day at noon UTC (2026-03-20). On it the sun rises at 6 and sets at
     * 18 local solar time at every latitude, so the slider's hours mean the same thing anywhere.
     */
    private static final double JULIAN_NOON = 2461120.0;

    /** Local solar time: 12 is the sun at its highest, whatever the longitude. */
    private static final float START_HOUR = 17.4f;

    /**
     * The hours that land on MapBox's four presets EXACTLY, at this camera on this date: dawn 6:48
     * (sun 6.5° and east), day 12:00 (41.1°, past the 38° stop), dusk 17:24 (7.1° and west), night
     * 22:00 (-33.9°, below the -9° stop). Anywhere else on the slider the curve blends two.
     */
    private static final float[] PRESET_HOURS = { 6.8f, 12f, 17.4f, 22f };
    private int preset = 2;

    private ExampleHost host;
    private int style;
    private int formula;
    private float hour = START_HOUR;
    private float sunAltitude;
    private float sunAzimuth;

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

        // A sky, because the hour is the whole example: the atmosphere is integrated against the
        // SAME sun, so it reddens and darkens with the slider without a value of its own. Options
        // starts with no SkyOptions, so nothing is drawn behind the map until this line.
        map.sky(Spec.of("sky"));

        applyFormula();
        applyHour();
        map.camera().moveTo(new Position(LON, LAT), 17.2f, 20f, 45f);

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
        // The HOUR, because that is what a day is: the sun walks its real arc, so dawn and dusk
        // come with the azimuth swinging round rather than being picked by hand. Forcing a height
        // instead is the gear panel's `sun altitude`, which overrides this until it is dragged again.
        host.slider("Hour", 0f, 24f, START_HOUR, new ExampleHost.OnValue() {
            @Override
            public void onValue(float value) {
                hour = value;
                applyHour();
                DayCycleLightExample.this.host.caption(describe());
            }
        });
        // Straight to MapBox's own four, so the render can be held against theirs.
        host.button("Preset", new Runnable() {
            @Override
            public void run() {
                preset = (preset + 1) % PRESET_HOURS.length;
                hour = PRESET_HOURS[preset];
                applyHour();
                DayCycleLightExample.this.host.caption(describe());
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
     * The whole formula, in two properties. An empty list is the built-in curve; a list of stops
     * replaces it, and everything the SDK derives from the light follows without a re-decode -
     * the tiles are untouched, so this is a redraw. Both are written every time, or a formula
     * without a dawn of its own would keep the previous one's.
     */
    private void applyFormula() {
        host.map().light().apply(Spec.object()
            .set("dayCycleLightStops", FORMULAS[formula][1])
            .set("dayCycleRisingLightStops", FORMULAS[formula][2]));
    }

    private String describe() {
        return String.format(java.util.Locale.US, "%02d:%02d - sun %.0f\u00b0 - %s - %s on %s",
            (int) hour, (int) ((hour - (int) hour) * 60), sunAltitude, light(),
            FORMULAS[formula][0], STYLES[style][0]);
    }

    /**
     * Which MapBox preset this hour actually renders. The curve only returns one EXACTLY where it
     * is flat - below -9, between 3 and 12, above 38 - and everything else is a blend of two, which
     * is why an arbitrary hour never matches a `lightPreset` screenshot.
     */
    private String light() {
        if (formula != 0) {
            return "custom curve";
        }
        String twilight = sunAzimuth <= 180f ? "dawn" : "dusk";
        if (sunAltitude <= -9f) {
            return "night";
        }
        if (sunAltitude >= 38f) {
            return "day";
        }
        if (sunAltitude >= 3f && sunAltitude <= 12f) {
            return twilight;
        }
        return sunAltitude < 3f ? "night to " + twilight : twilight + " to day";
    }

    /** The curve reads a sun POSITION, and the hour is where the sun actually is at that hour. */
    private void applyHour() {
        setSunFromHour();
        host.map().light().apply(Spec.object()
            .set("sunAzimuth", (double) sunAzimuth)
            .set("sunAltitude", (double) sunAltitude));
    }

    /**
     * Local solar time to a sun position - the NOAA low-accuracy form, good to ~0.1 degree, which
     * is what LightOptions.setSunPositionFromTime computes in C++; the facade cannot reach that
     * method, so the example spells it out.
     */
    private void setSunFromHour() {
        double n = JULIAN_NOON + (hour - LON / 15.0 - 12.0) / 24.0 - 2451545.0;
        double meanLong = 280.460 + 0.9856474 * n;
        double meanAnom = Math.toRadians(357.528 + 0.9856003 * n);
        double eclipticLong = Math.toRadians(
            meanLong + 1.915 * Math.sin(meanAnom) + 0.020 * Math.sin(2 * meanAnom));
        double obliquity = Math.toRadians(23.439 - 0.0000004 * n);
        double rightAsc = Math.atan2(Math.cos(obliquity) * Math.sin(eclipticLong),
                                     Math.cos(eclipticLong));
        double decl = Math.asin(Math.sin(obliquity) * Math.sin(eclipticLong));

        // Greenwich mean sidereal time, then the local hour angle.
        double gmst = (18.697374558 + 24.06570982441908 * n) % 24.0;
        double hourAngle = Math.toRadians((gmst < 0 ? gmst + 24.0 : gmst) * 15.0 + LON) - rightAsc;

        double lat = Math.toRadians(LAT);
        sunAltitude = (float) Math.toDegrees(Math.asin(
            Math.sin(lat) * Math.sin(decl) + Math.cos(lat) * Math.cos(decl) * Math.cos(hourAngle)));
        // atan2 here is measured from south; the SDK wants clockwise from north.
        sunAzimuth = (float) (Math.toDegrees(Math.atan2(Math.sin(hourAngle),
            Math.cos(hourAngle) * Math.sin(lat) - Math.tan(decl) * Math.cos(lat))) + 180.0);
    }
}
