package com.massifmaps.MassifDemo.examples.sources;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.Position;
import com.massifmaps.api.Spec;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.TimeZone;

/**
 * A weather radar loop: one tile per time step, two steps mixed in a single draw.
 */
@ExampleInfo(
    id = "animated-radar",
    title = "Animate a weather radar",
    description = "Each time step is a tile of its own. The layer draws a step and the next in one pass and "
                + "mixes the values, so the fade has no pop at a step boundary. Play, scrub, change the speed, "
                + "or switch to 3D and watch it drape over the Alps.",
    section = Sections.SOURCES,
    order = 20)
public class AnimatedRadarExample extends MapExample {

    /** LibreWXR serves RainViewer's API: the steps of the loop, then a tile URL per step. */
    private static final String STEPS = "https://api.librewxr.net/public/weather-maps.json";
    private static final String TILES = "https://api.librewxr.net/v2/radar/{time}/256/{z}/{x}/{y}/2/1_1.png";

    /** A tile server wants to know who is asking: a real app identifies itself. */
    private static final String UA = "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    /** Where each state looks: all of Europe flat, the Alps from the north in 3D. */
    private static final Position EUROPE = new Position(4.5, 47.0);
    private static final Position ALPS = new Position(7.2, 46.2);

    private ExampleHost host;
    private MassifMap map;
    private long[] times = new long[0];
    private boolean playing = true;
    private float stepsPerSecond = 2f;
    private float position;
    private long lastNanos;

    @Override
    public void onStart(ExampleHost host) {
        this.host = host;
        this.map = host.map();

        // The ground, set up BEFORE any layer decodes and left flat: the 3D switch below is the `flattened`
        // flag, so a map that never goes 3D builds not one tile for it. Mapterhorn's open DEM.
        map.terrain(Spec.of("terrain").set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("mapterhorn-dem.db"))
                .set("capacity", 200 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.mapterhorn.com/{z}/{x}/{y}.webp")
                    .set("minZoom", 1)
                    .set("maxZoom", 16)
                    // What picks the elevation decoder, per tile; on the HTTP source, not the cache in front of it.
                    .set("metaData", Spec.object().set("dem_encoding", "terrarium")))))
           .apply(Spec.object()
               .set("flattened", true)
               .set("exaggeration", 1.5)
               .set("flattenMode", "TERRAIN_FLATTEN_MODE_FULL")
               .set("viewDistanceFactor", 1.6)
               .set("cameraClearance", 40));
        map.sky(Spec.of("sky"));
        map.fog(Spec.of("fog").set("rangeStart", 2.2).set("rangeEnd", 8));
        map.light(Spec.of("light").set("terrainLightingEnabled", false)
                                  .set("sunAzimuth", 315).set("sunAltitude", 42)
                                  .set("shadowStrength", 0.0));

        // The Massif streets style (styles/massif/carto, bundled by the build as assets/styles/massif.zip)
        // over OpenFreeMap's vector tiles, cached on disk: a free service a demo would re-fetch every run.
        map.addLayer("basemap", Spec.of("vector")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("openfreemap.db"))
                .set("capacity", 100 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                    .set("maxZoom", 14)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA))))
            .set("style", Spec.of("mbvt").set("project", Spec.of("project")
                .set("assets", Spec.of("zip").set("data", Spec.of("url").set("url", "assets://styles/massif.zip")))
                .set("name", "streets"))));
        map.camera().moveTo(new Position(4.5, 47.0), 5.5f);
        host.caption("Loading the radar steps...");

        List<Long> steps;
        try {
            steps = fetchSteps();
        } catch (Exception e) {
            host.caption("The radar is not reachable: " + e.getMessage());
            return;
        }
        times = new long[steps.size()];
        List<String> timestamps = new ArrayList<>();
        for (int i = 0; i < times.length; i++) {
            times[i] = steps.get(i);
            timestamps.add(String.valueOf(times[i]));
        }

        // The memory cache keeps a step fetched once: it is the second raster of one tile and the first
        // of the next, and its default 6 MB holds a third of a loop. prefetchFrames = every step, so
        // the loop does not stall on its first pass.
        map.addLayer("radar", Spec.of("animated-raster")
            .set("source", Spec.of("memory-cache")
                .set("capacity", 64 * 1024 * 1024)
                .set("source", Spec.of("persistent-cache")
                    .set("databasePath", host.cachePath("librewxr-radar.db"))
                    .set("capacity", 100 * 1024 * 1024)
                    // The server renders a tile on demand and takes seconds over it, so a tile fetched
                    // once is kept - for the 5 minutes its Cache-Control gives: a forecast step is
                    // revised, and must not be shown later as an observation.
                    .set("source", Spec.of("http").set("url", TILES).set("maxZoom", 12)
                        .set("maxAgeHeaderCheck", true))))
            // Radar is coarse data: one level fewer is a quarter of the tiles to wait for.
            .set("zoomLevelBias", -1)
            .set("frameTimestamps", timestamps.toArray(new String[0]))
            .set("prefetchFrames", timestamps.size())
            .set("opacity", 0.85f));

        host.toggle("Play", true, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                playing = on;
            }
        });
        host.toggle("3D", false, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                map.terrain().set("flattened", !on);
                map.light().set("terrainLightingEnabled", on);
                // tilt 90 is straight down in this SDK, so the 3D view is a LOW tilt.
                map.camera().animate(1.5f).moveTo(on ? ALPS : EUROPE, on ? 9f : 5.5f, on ? 180f : 0f, on ? 35f : 90f);
            }
        });
        host.slider("Time", 0f, 1f, 0f, new ExampleHost.OnValue() {
            @Override
            public void onValue(float value) {
                position = value * (times.length - 1);
                show();
            }
        });
        host.slider("Steps a second", 0.5f, 6f, stepsPerSecond, new ExampleHost.OnValue() {
            @Override
            public void onValue(float value) {
                stepsPerSecond = value;
            }
        });

        lastNanos = System.nanoTime();
        host.postDelayed(new Runnable() {
            @Override
            public void run() {
                tick();
                AnimatedRadarExample.this.host.postDelayed(this, 16);
            }
        }, 16);
    }

    /** The clock: advances the position by the time since the last frame, at the chosen rate. */
    private void tick() {
        long now = System.nanoTime();
        float seconds = (now - lastNanos) / 1e9f;
        lastNanos = now;
        if (playing && times.length > 0) {
            position = (position + seconds * stepsPerSecond) % times.length;
            host.setSlider("Time", times.length > 1 ? position / (times.length - 1) : 0f);
            show();
        }
    }

    private void show() {
        map.layer("radar").set("animationPosition", position);
        int step = Math.min(times.length - 1, (int) position);
        SimpleDateFormat format = new SimpleDateFormat("HH:mm", Locale.US);
        format.setTimeZone(TimeZone.getTimeZone("UTC"));
        boolean forecast = times[step] * 1000L > System.currentTimeMillis();
        host.caption(format.format(new Date(times[step] * 1000L)) + " UTC" + (forecast ? "  forecast" : ""));
    }

    private static List<Long> fetchSteps() throws Exception {
        HttpURLConnection connection = (HttpURLConnection) new URL(STEPS).openConnection();
        connection.setRequestProperty("User-Agent", UA);
        try (InputStream in = connection.getInputStream()) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[4096];
            for (int n; (n = in.read(buffer)) > 0; ) {
                out.write(buffer, 0, n);
            }
            JSONObject radar = new JSONObject(out.toString("UTF-8")).getJSONObject("radar");
            List<Long> steps = new ArrayList<>();
            for (String key : new String[] { "past", "nowcast" }) {
                JSONArray array = radar.optJSONArray(key);
                for (int i = 0; array != null && i < array.length(); i++) {
                    steps.add(array.getJSONObject(i).getLong("time"));
                }
            }
            return steps;
        } finally {
            connection.disconnect();
        }
    }
}
