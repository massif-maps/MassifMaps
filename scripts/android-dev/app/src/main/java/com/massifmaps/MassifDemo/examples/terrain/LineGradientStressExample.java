package com.massifmaps.MassifDemo.examples.terrain;

import android.graphics.Color;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifSource;
import com.massifmaps.api.Position;
import com.massifmaps.api.Spec;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Locale;

/**
 * A gradient test scene, Android only: two routes of about 40000 points each over roughly 100 km,
 * from the bench's own data, drawn with a smooth and a stepped ramp, a dashed variant, a casing,
 * on 3D terrain, tilted. It is for finding what breaks, not for the gallery.
 */
@ExampleInfo(
    id = "line-gradient-stress",
    title = "Line gradient stress test",
    description = "Two long routes (about 40000 points each) with a smooth and a stepped colour "
                + "ramp, a dashed variant and a casing, over 3D terrain at a tilt.",
    section = Sections.TERRAIN,
    order = 90)
public class LineGradientStressExample extends MapExample {

    private static final String UA =
        "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    /** 8 routes of 30000-49000 points around Grenoble; the first two are used. */
    private static final String ROUTES_ASSET = "bench-long-routes.geojson";

    private static final String WIDTH = "line-width: linear([view::zoom], (8, 3), (15, 10));";

    /** Gradient with a casing: the border pass draws with the ramp off. */
    private static final String SOLID = String.join("\n",
        "{",
        "  line-gradient: [gradient];",
        "  " + WIDTH,
        "  line-join: round;",
        "  line-cap: round;",
        "  line-border-width: 1.5;",
        "  line-border-color: #0B1220;",
        "}");

    /** Gradient with a dash pattern: the distance along the line is the dash's, not the ramp's. */
    private static final String DASHED = String.join("\n",
        "{",
        "  line-gradient: [gradient];",
        "  " + WIDTH,
        "  line-join: round;",
        "  line-cap: round;",
        "  line-dasharray: 14,10;",
        "}");

    /** Tilt 90 is straight down in this SDK, so the oblique views are the lower numbers. */
    private static final float[] TILTS = { 55f, 35f, 90f };

    private boolean dashed;
    private boolean steps;
    private float rotation;
    private int tiltIndex;

    private static Spec dem(ExampleHost host) {
        return Spec.of("persistent-cache")
            .set("databasePath", host.cachePath("mapterhorn-dem.db"))
            .set("capacity", 200 * 1024 * 1024)
            .set("source", Spec.of("http")
                .set("url", "https://tiles.mapterhorn.com/{z}/{x}/{y}.webp")
                .set("minZoom", 1)
                .set("maxZoom", 16)
                .set("metaData", Spec.object().set("dem_encoding", "terrarium")));
    }

    /**
     * Stops that wander along the route, hue hue0 to hue1. A stepped ramp alternates bright and dark so
     * every edge shows at any zoom.
     */
    private static String ramp(int stops, boolean steps, float hue0, float hue1, float phase) {
        StringBuilder sb = new StringBuilder(steps ? "step;" : "");
        for (int i = 0; i < stops; i++) {
            float position = i / (float) (steps ? stops : stops - 1);
            float wave = Math.max(0f, Math.min(1f, position + 0.12f * (float) Math.sin(position * 40f + phase)));
            float value = steps && i % 2 == 1 ? 0.6f : 0.95f;
            int color = Color.HSVToColor(new float[] { hue0 + (hue1 - hue0) * wave, 0.85f, value });
            if (i > 0) {
                sb.append(',');
            }
            sb.append(String.format(Locale.US, "%.4f:#%06X", position, color & 0xFFFFFF));
        }
        return sb.toString();
    }

    private static JSONObject variant(JSONObject route, String variant, String gradient) throws JSONException {
        JSONObject copy = new JSONObject(route.toString());
        copy.put("properties", new JSONObject().put("variant", variant).put("gradient", gradient));
        return copy;
    }

    private static String readAsset(ExampleHost host, String name) throws IOException {
        try (InputStream in = host.context().getAssets().open(name)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream(8 * 1024 * 1024);
            byte[] buffer = new byte[64 * 1024];
            for (int read; (read = in.read(buffer)) > 0; ) {
                out.write(buffer, 0, read);
            }
            return out.toString("UTF-8");
        }
    }

    private static String layerName(boolean dashed, boolean steps) {
        return (dashed ? "route-dash" : "route-solid") + (steps ? "-steps" : "-smooth");
    }

    /**
     * Shows one of the four layers. Route A is in the document twice, smooth and stepped, and each
     * layer keeps one of them: swapping layers is what changes the picture, since a layer that is
     * already drawing does not pick up a changed document.
     */
    private void showVariant(MassifMap map) {
        for (int i = 0; i < 4; i++) {
            boolean d = i / 2 == 1;
            boolean s = i % 2 == 1;
            map.layer(layerName(d, s)).visible(d == dashed && s == steps);
        }
    }

    @Override
    public void onStart(final ExampleHost host) {
        final MassifMap map = host.map();

        // Imagery and labels, as the terrain example has them: a dark ground the colours stand out on.
        map.addLayer("satellite", Spec.of("raster")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("world-imagery.db"))
                .set("capacity", 200 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://server.arcgisonline.com/ArcGIS/rest/services/"
                              + "World_Imagery/MapServer/tile/{z}/{y}/{x}")
                    .set("maxZoom", 18)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA)))));
        map.style("hybrid", Spec.of("mbvt")
            .set("project", Spec.of("project")
                .set("assets", Spec.of("zip")
                    .set("data", Spec.of("url").set("url", "assets://styles/massif.zip")))
                .set("name", "hybrid")));
        map.addLayer("labels", Spec.of("vector")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("openfreemap.db"))
                .set("capacity", 100 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                    .set("maxZoom", 14)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA))))
            .set("style", "hybrid"));

        map.terrain(Spec.of("terrain").set("source", dem(host)))
           .apply(Spec.object().set("exaggeration", 1).set("viewDistanceFactor", 1.6).set("cameraClearance", 40));
        map.sky(Spec.of("sky"));
        map.fog(Spec.of("fog").set("rangeStart", 2.2).set("rangeEnd", 8));
        map.light(Spec.of("light")
            .set("terrainLightingEnabled", true)
            .set("sunAzimuth", 315)
            .set("sunAltitude", 42)
            .set("shadowStrength", 1.0)
            .set("shadowSoftness", 1.5));

        Position view;
        try {
            JSONArray features = new JSONObject(readAsset(host, ROUTES_ASSET)).getJSONArray("features");
            JSONObject routeA = features.getJSONObject(0);
            JSONArray line = routeA.getJSONObject("geometry").getJSONArray("coordinates");
            JSONArray at = line.getJSONArray(line.length() * 3 / 10);
            view = new Position(at.getDouble(0), at.getDouble(1));

            String document = new JSONObject()
                .put("type", "FeatureCollection")
                .put("features", new JSONArray()
                    .put(variant(routeA, "a-smooth", ramp(60, false, 130f, 0f, 0f)))
                    .put(variant(routeA, "a-steps", ramp(30, true, 130f, 0f, 0f)))
                    .put(variant(features.getJSONObject(1), "b", ramp(24, true, 200f, 290f, 2f))))
                .toString();

            // lineMetrics is what lets the style know where along a route each tile piece sits.
            MassifSource routes = map.source("route-data",
                Spec.of("geojson").set("maxZoom", 14).set("lineMetrics", true));
            routes.setLayerGeoJSON(routes.createLayer("routes"), document);
        } catch (IOException | JSONException e) {
            host.toast("Could not read " + ROUTES_ASSET + ": " + e.getMessage());
            return;
        }

        for (int i = 0; i < 4; i++) {
            boolean d = i / 2 == 1;
            boolean s = i % 2 == 1;
            String css = "#routes[variant!='" + (s ? "a-smooth" : "a-steps") + "'] " + (d ? DASHED : SOLID);
            map.addLayer(layerName(d, s), Spec.of("vector").set("source", "route-data")
                .set("style", Spec.of("mbvt").set("cartocss", Spec.of("cartocss").set("css", css))));
        }
        dashed = host.option("dash", 0f) > 0f;
        steps = host.option("steps", 0f) > 0f;
        showVariant(map);

        rotation = host.option("rotation", 0f);
        map.camera().moveTo(view, host.option("zoom", 11f), rotation, host.option("tilt", TILTS[0]));

        host.toggle("Terrain", true, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                map.set("terrain.enabled", on);
            }
        });
        host.toggle("Dash", dashed, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                dashed = on;
                showVariant(map);
            }
        });
        host.toggle("Steps on A", steps, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                steps = on;
                showVariant(map);
            }
        });
        host.button("Tilt", new Runnable() {
            @Override
            public void run() {
                tiltIndex = (tiltIndex + 1) % TILTS.length;
                map.camera().animate(1.0f).moveTo(map.camera().position(), map.camera().zoom(),
                    rotation, TILTS[tiltIndex]);
            }
        });
        host.caption("A smooth ramp (A) and a stepped one (B), each 40000 points, on terrain.");
    }
}
