package com.massifmaps.MassifDemo.demo;

import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.Toast;

import com.massifmaps.api.Massif;
import com.massifmaps.api.MassifException;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifObject;
import com.massifmaps.api.MassifSource;
import com.massifmaps.api.Position;
import com.massifmaps.api.Spec;
import com.massifmaps.valhalla.LatLon;
import com.massifmaps.valhalla.RoutingRequest;
import com.massifmaps.valhalla.ValhallaOnlineRoutingService;
import com.massifmaps.valhalla.ValhallaRoutingService;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.HashMap;

/**
 * The one-shot test cases the demo can RUN (as opposed to the layers it can SHOW): routing,
 * vector-tile search, GeoJSON geometry. Each is a panel button; none of them is part of the map
 * configuration, which is why they live outside {@link DemoMap}.
 *
 * All of them draw their result into one shared vector layer, created on first use and always
 * added on top.
 */
public final class DemoTests {

    private static final String TAG = "DemoTests";

    private static MassifObject resultSource;
    private static int resultCounter;

    /**
     * Drives a TWO-FINGER drag over the map, as a real gesture would.
     *
     * The only way to exercise it: adb can synthesize one pointer, not two, and the emulator's
     * touch devices are not writable from the shell. The MotionEvent goes through
     * MapView.onTouchEvent, which is the same entry point a finger uses, so what it tests is the
     * gesture path and not a shortcut around it.
     *
     * @param dx horizontal travel in pixels, @param dy vertical travel (negative = upwards)
     */
    public static void runTwoFingerDrag(final DemoMap demo, final float dx, final float dy) {
        final com.massifmaps.ui.MapView mapView = demo.mapView;
        final Handler handler = new Handler(Looper.getMainLooper());
        final Position before = demo.map.camera().position();
        final float x1 = 400, x2 = 700, y = 1600;
        final long downTime = android.os.SystemClock.uptimeMillis();
        final int steps = 12;
        // Real delays between the events, not a tight loop: the gesture is only recognised once
        // the click handler THREAD has seen the movement, so a burst of events with fake
        // timestamps is dropped as a two-finger tap.
        send(mapView, downTime, downTime, android.view.MotionEvent.ACTION_DOWN, 1, x1, y, x2, y);
        handler.postDelayed(new Runnable() {
            public void run() {
                send(mapView, downTime, android.os.SystemClock.uptimeMillis(),
                        android.view.MotionEvent.ACTION_POINTER_DOWN | (1 << android.view.MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                        2, x1, y, x2, y);
            }
        }, 30);
        for (int step = 1; step <= steps; step++) {
            final float fx = dx * step / steps, fy = dy * step / steps;
            handler.postDelayed(new Runnable() {
                public void run() {
                    send(mapView, downTime, android.os.SystemClock.uptimeMillis(),
                            android.view.MotionEvent.ACTION_MOVE, 2, x1 + fx, y + fy, x2 + fx, y + fy);
                }
            }, 60 + step * 25L);
        }
        handler.postDelayed(new Runnable() {
            public void run() {
                send(mapView, downTime, android.os.SystemClock.uptimeMillis(),
                        android.view.MotionEvent.ACTION_POINTER_UP | (1 << android.view.MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                        2, x1 + dx, y + dy, x2 + dx, y + dy);
                send(mapView, downTime, android.os.SystemClock.uptimeMillis(),
                        android.view.MotionEvent.ACTION_UP, 1, x1 + dx, y + dy, x2 + dx, y + dy);
                Log.i(TAG, "two-finger drag (" + dx + "," + dy + "): focus " + before + " -> " + demo.map.camera().position());
            }
        }, 60 + (steps + 2) * 25L);
    }

    private static void send(com.massifmaps.ui.MapView mapView, long downTime, long eventTime, int action,
                             int count, float x1, float y1, float x2, float y2) {
        android.view.MotionEvent.PointerProperties[] properties = new android.view.MotionEvent.PointerProperties[count];
        android.view.MotionEvent.PointerCoords[] coords = new android.view.MotionEvent.PointerCoords[count];
        for (int i = 0; i < count; i++) {
            properties[i] = new android.view.MotionEvent.PointerProperties();
            properties[i].id = i;
            properties[i].toolType = android.view.MotionEvent.TOOL_TYPE_FINGER;
            coords[i] = new android.view.MotionEvent.PointerCoords();
            coords[i].x = (i == 0 ? x1 : x2);
            coords[i].y = (i == 0 ? y1 : y2);
            coords[i].pressure = 1;
            coords[i].size = 1;
        }
        android.view.MotionEvent event = android.view.MotionEvent.obtain(downTime, eventTime, action, count,
                properties, coords, 0, 0, 1, 1, 0, 0, android.view.InputDevice.SOURCE_TOUCHSCREEN, 0);
        mapView.onTouchEvent(event);
        event.recycle();
    }


    /** Lazily creates (and adds) the layer every test draws its result into. */
    private static MassifObject results(DemoMap demo) {
        if (resultSource == null) {
            Massif.destroy("source", "demo-results");
            resultSource = Massif.source("demo-results", Spec.of("local").set("projection", Spec.of("EPSG:4326")));
            Massif.destroy("layer", "demo-results");
            demo.map.add(Massif.layer("demo-results", Spec.of("elements").set("source", "demo-results")));
        }
        return resultSource;
    }

    /** An element built from a spec, put on the shared results layer. */
    private static void addResult(DemoMap demo, Spec element) {
        String id = "demo-result-" + (++resultCounter);
        MassifObject object = Massif.object("element", id, element);
        results(demo).call("add", object.handle()).close();
    }

    // =============================================================================================
    // ROUTING
    // =============================================================================================

    /** Offline Valhalla routing from the .vtiles package on the device. */
    public static void runOfflineRouting(final DemoMap demo) {
        final String tiles = demo.dataPath + "/" + DemoConfig.ROUTING_VTILES_NAME;
        new Thread(new Runnable() {
            public void run() {
                try {
                    ValhallaRoutingService service = new ValhallaRoutingService();
                    service.setProfile("pedestrian");
                    service.addMBTilesPath(tiles);

                    java.util.List<LatLon> points = new java.util.ArrayList<LatLon>();
                    points.add(new LatLon(45.1845, 5.7168));
                    points.add(new LatLon(45.24433, 5.74027));
                    RoutingRequest request = new RoutingRequest(points);
                    request.setParameter("costing_options",
                            "{\"bicycle\":{\"non_network_penalty\":0,\"use_ferry\":0,\"shortest\":true,\"use_roads\":0.0,\"use_tracks\":0.5,\"bicycle_type\":\"Hybrid\"}}");
                    request.setParameter("language", "\"fr-FR\"");

                    drawRoute(demo, service.calculateRoute(request), service);
                } catch (Exception e) {
                    report(demo, "offline routing failed: " + e.getMessage());
                    Log.e(TAG, "offline routing failed", e);
                }
            }
        }).start();
    }

    /** Online Valhalla routing (no data files needed) through a plain java.net POST. */
    public static void runOnlineRouting(final DemoMap demo) {
        new Thread(new Runnable() {
            public void run() {
                try {
                    ValhallaOnlineRoutingService service = new ValhallaOnlineRoutingService(
                            "https://valhalla1.openstreetmap.de",
                            (url, body, headers) -> {
                                try {
                                    java.net.URL netUrl = new java.net.URL(url);
                                    java.net.HttpURLConnection conn = (java.net.HttpURLConnection) netUrl.openConnection();
                                    conn.setRequestMethod("POST");
                                    conn.setDoOutput(true);
                                    conn.setRequestProperty("Content-Type", "application/json");
                                    conn.getOutputStream().write(body.getBytes("UTF-8"));
                                    int code = conn.getResponseCode();
                                    java.io.InputStream is = (code < 400) ? conn.getInputStream() : conn.getErrorStream();
                                    java.util.Scanner s = new java.util.Scanner(is).useDelimiter("\\A");
                                    return s.hasNext() ? s.next() : "";
                                } catch (Exception e) {
                                    throw new RuntimeException(e);
                                }
                            });
                    service.setProfile("pedestrian");
                    HashMap headers = new HashMap();
                    headers.put("X-Client-Id", "MassifMapsDemo");
                    service.setHeaders(headers);

                    java.util.List<LatLon> points = new java.util.ArrayList<LatLon>();
                    points.add(new LatLon(45.1877, 5.7249));   // Place Grenette
                    points.add(new LatLon(45.1916, 5.7148));   // Gare de Grenoble
                    // The online service returns raw Valhalla JSON and has no shape decoder, so
                    // this test only reports the response (the offline one draws the geometry).
                    String rawJson = service.calculateRoute(new RoutingRequest(points));
                    report(demo, "online routing OK (" + rawJson.length() + " bytes)");
                    largeLog(TAG, rawJson);
                } catch (Exception e) {
                    report(demo, "online routing failed: " + e.getMessage());
                    Log.e(TAG, "online routing failed", e);
                }
            }
        }).start();
    }

    /** Decodes the Valhalla shape of every leg, draws it, and puts an arrow on every turn. */
    private static void drawRoute(DemoMap demo, String rawJson, ValhallaRoutingService service) throws Exception {
        JSONArray legs = new JSONObject(rawJson).getJSONObject("trip").getJSONArray("legs");
        Massif.destroy("elementstyle", "demo-route-line");
        Massif.object("elementstyle", "demo-route-line", Spec.of("line").set("width", 4).set("color", 0xFFFF0000));
        demo.clearManeuverArrows();
        int points = 0;
        int arrowCount = 0;
        for (int i = 0; i < legs.length(); i++) {
            JSONObject leg = legs.getJSONObject(i);
            JSONArray coordinates = new JSONArray(service.parseShape(leg.getString("shape")));
            double[][] poses = new double[coordinates.length()][];
            for (int k = 0; k < poses.length; k++) {
                JSONArray point = coordinates.getJSONArray(k);
                poses[k] = new double[] { point.getDouble(0), point.getDouble(1) };
            }
            points += poses.length;
            addResult(demo, Spec.of("line").set("poses", poses).set("style", "demo-route-line"));
            arrowCount += addManeuverArrows(demo, leg, poses, arrowCount);
        }
        report(demo, "route drawn (" + points + " points, " + arrowCount + " maneuver arrows)");
    }

    /**
     * One arrow per real turn of a leg. Valhalla maneuver types 0-6 are the start and destination
     * ones, which have nothing to point at. The shape indices are per leg, and so is the geometry
     * handed over here, so they line up.
     *
     * The shape is WGS84 (that is what the GeoJSON reader returns), hence the null projection.
     */
    private static int addManeuverArrows(DemoMap demo, JSONObject leg, double[][] poses, int firstArrowId) {
        JSONArray maneuvers = leg.optJSONArray("maneuvers");
        if (maneuvers == null) {
            return 0;
        }
        int count = 0;
        for (int i = 0; i < maneuvers.length(); i++) {
            JSONObject maneuver = maneuvers.optJSONObject(i);
            if (maneuver == null || maneuver.optInt("type", 0) <= 6) {
                continue;
            }
            int index = maneuver.optInt("begin_shape_index", -1);
            if (index < 0) {
                continue;
            }
            demo.setManeuverArrow(firstArrowId + count, buildArrow(demo, poses, index));
            count++;
        }
        return count;
    }

    /** The arrow GeoJSON of one maneuver, cut by the geometry builder. */
    private static String buildArrow(DemoMap demo, double[][] poses, int index) {
        MassifObject arrow = demo.maneuverBuilder().call("buildArrowAtIndex", poses, index);
        try {
            return arrow.json();
        } finally {
            arrow.close();
        }
    }

    /**
     * The maneuver gallery: one arrow per shape real navigation actually produces, laid out on a
     * grid around the start position, so the whole set can be judged in one screenshot instead of
     * waiting for a route to happen to contain a hairpin.
     *
     * Each entry is a synthetic route in METRES around its own cell, fed through the real builder
     * with a maneuver index - so the slicing, not just the drawing, is what is on screen. The
     * roundabout gets longer lengths, because what a driver needs to see there is the whole arc.
     */
    public static int seedManeuverArrows(DemoMap demo) {
        double lon = DemoConfig.START_LON, lat = DemoConfig.START_LAT;
        // Longer than the 30 m default: these are drawn side by side to be READ, and a shaft only
        // as long as the head is, is a triangle with a stub, not a maneuver.
        float before = GALLERY_LEG, after = GALLERY_LEG;
        int count = 0;
        try {
            // col, row, name, points (east/north metres from the cell centre), maneuver index
            count += addGalleryArrow(demo, count, lon, lat, 0, 1, new double[][] {
                    { -90, 0 }, { 0, 0 }, { 0, -90 } }, 1, before, after);              // right 90
            count += addGalleryArrow(demo, count, lon, lat, 1, 1, new double[][] {
                    { -90, 0 }, { 0, 0 }, { 0, 90 } }, 1, before, after);               // left 90
            count += addGalleryArrow(demo, count, lon, lat, 2, 1, new double[][] {
                    { -90, 0 }, { 0, 0 }, { 65, -65 } }, 1, before, after);             // slight right 45
            count += addGalleryArrow(demo, count, lon, lat, 0, 0, new double[][] {
                    { -90, 0 }, { 0, 0 }, { -65, -65 } }, 1, before, after);            // sharp right 135
            count += addGalleryArrow(demo, count, lon, lat, 1, 0, new double[][] {
                    { -90, 22 }, { 0, 22 }, { 20, 11 }, { 20, -11 }, { 0, -22 }, { -90, -22 } }, 1, before, after); // U-turn
            count += addGalleryArrow(demo, count, lon, lat, 2, 0, roundabout(45, 3, 10), 2, 40, 260);     // roundabout
        } catch (Exception e) {
            Log.w(TAG, "maneuver gallery failed: " + e.getMessage());
        }
        demo.maneuverBuilder().set("lengthBefore", DemoConfig.MANEUVER_LENGTH_BEFORE).set("lengthAfter", DemoConfig.MANEUVER_LENGTH_AFTER);
        Log.i(TAG, "seeded " + count + " gallery maneuver arrows around " + lat + ", " + lon);
        return count;
    }

    /** Cell spacing of the gallery grid, and the length of each leg, in metres. */
    private static final double GALLERY_SPACING = 300;
    private static final float GALLERY_LEG = 60;

    private static int addGalleryArrow(DemoMap demo, int arrowId, double lon, double lat,
                                       int col, int row, double[][] points, int maneuverIndex,
                                       float lengthBefore, float lengthAfter) {
        demo.maneuverBuilder().set("lengthBefore", lengthBefore).set("lengthAfter", lengthAfter);
        double east = (col - 1) * GALLERY_SPACING;
        double north = (row - 0.5) * GALLERY_SPACING;
        double[][] poses = new double[points.length][];
        for (int i = 0; i < points.length; i++) {
            poses[i] = offsetMeters(lon, lat, east + points[i][0], north + points[i][1]);
        }
        demo.setManeuverArrow(arrowId, buildArrow(demo, poses, maneuverIndex));
        return 1;
    }

    /**
     * A roundabout: straight approach from the west, then the ring, then the exit north. The
     * maneuver point is the entry, which is what a routing engine reports.
     */
    private static double[][] roundabout(double radius, int exitQuarters, int stepDegrees) {
        java.util.List<double[]> points = new java.util.ArrayList<double[]>();
        points.add(new double[] { -radius - 90, -radius });
        points.add(new double[] { -radius, -radius });                 // entry: index 1, the maneuver
        for (int angle = 180; angle >= 180 - 90 * exitQuarters; angle -= stepDegrees) {
            double a = Math.toRadians(angle);
            points.add(new double[] { Math.cos(a) * radius, Math.sin(a) * radius - radius });
        }
        double a = Math.toRadians(180 - 90 * exitQuarters);
        points.add(new double[] { Math.cos(a) * radius, Math.sin(a) * radius - radius - 90 });
        return points.toArray(new double[0][]);
    }

    /** Offsets a WGS84 position by metres east / north - exact enough over the few hundred here. */
    private static double[] offsetMeters(double lon, double lat, double east, double north) {
        double metresPerDegLat = 111320.0;
        double metresPerDegLon = metresPerDegLat * Math.cos(Math.toRadians(lat));
        return new double[] { lon + east / metresPerDegLon, lat + north / metresPerDegLat };
    }


    // =============================================================================================
    // SEARCH
    // =============================================================================================

    /**
     * Searches the base layer's own tiles around the map centre. Needs a VECTOR base layer:
     * the search service reads the same source and decoder the layer renders from.
     */
    public static void runVectorTileSearch(final DemoMap demo) {
        if (demo.baseLayer == null || demo.baseDecoder == null) {
            report(demo, "search needs the vector base layer (LAYERS > base)");
            return;
        }
        final Position centre = demo.map.camera().position();
        new Thread(new Runnable() {
            public void run() {
                try {
                    long start = System.nanoTime();
                    Massif.destroy("search", "demo-tests-search");
                    MassifObject service = Massif.object("search", "demo-tests-search", Spec.of("vectortile")
                            .set("source", demo.vectorSource().id()).set("style", demo.baseDecoder.id())
                            .set("minZoom", 14).set("maxZoom", 14)
                            .set("sortByDistance", true).set("preventDuplicates", true));
                    Massif.destroy("geometry", "demo-tests-centre");
                    MassifObject point = Massif.object("geometry", "demo-tests-centre", Spec.of("point").set("pos", new double[] { centre.lng, centre.lat }));
                    Massif.destroy("projection", "demo-tests-wgs84");
                    MassifObject wgs84 = Massif.object("projection", "demo-tests-wgs84", Spec.of("EPSG:4326"));
                    Massif.destroy("search", "demo-tests-request");
                    MassifObject request = Massif.object("search", "demo-tests-request", Spec.of("request")
                            .set("searchRadius", 2000)
                            .set("filterExpression", "regexp_ilike(class,'.*peak.*') OR regexp_ilike(class,'.*motorway.*')"));
                    request.set("geometry", point).set("projection", wgs84);

                    MassifObject found = service.call("findFeatures", request.handle());
                    long count = found.getLong("featureCount", 0);
                    found.close();
                    report(demo, "search: " + count + " features in " + ((System.nanoTime() - start) / 1000000) + " ms");
                } catch (Exception e) {
                    report(demo, "search failed: " + e.getMessage());
                    Log.e(TAG, "search failed", e);
                }
            }
        }).start();
    }

    // =============================================================================================
    // GEOMETRY
    // =============================================================================================

    /**
     * GeoJSONVectorTileDataSource: features added at runtime and styled with CartoCSS (as opposed
     * to the vector elements of the ELEMENTS layer, which carry their own style objects).
     */
    public static void addGeoJSONLine(DemoMap demo) {
        try {
            Massif.destroy("style", "demo-test-line-style");
            Massif.style("demo-test-line-style", Spec.of("mbvt").set("cartocss", Spec.of("cartocss").set("css",
                    "#items { line-color: #374C70; line-cap: round; line-join: round; line-width: 12; }")));
            Massif.destroy("source", "demo-test-line-source");
            MassifSource source = Massif.source("demo-test-line-source", Spec.of("geojson").set("minZoom", 0).set("maxZoom", 24));
            source.setLayerGeoJSON(source.createLayer("items"), "{\"type\":\"Feature\",\"id\":1,\"properties\":{\"name\":\"test\"},"
                    + "\"geometry\":{\"type\":\"LineString\",\"coordinates\":"
                    + "[[5.7249,45.1982],[5.7225,45.1975],[5.7220,45.1949],[5.7201,45.1935],[5.7255,45.1915]]}}");
            Massif.destroy("layer", "demo-test-line");
            demo.map.add(Massif.layer("demo-test-line", Spec.of("vector").set("source", "demo-test-line-source").set("style", "demo-test-line-style")));
            report(demo, "geojson line added");
        } catch (Exception e) {
            report(demo, "geojson test failed: " + e.getMessage());
            Log.e(TAG, "geojson test failed", e);
        }
    }

    // =============================================================================================
    // GEOJSON TILE-BUILD BENCH
    // =============================================================================================

    /**
     * Times {@link GeoJSONVectorTileDataSource} with no renderer in the way: import the asset, then
     * build a fixed set of tiles and add up the wall time. The tile set is derived from the data
     * extent, so the same asset always walks the same tiles and two builds are comparable.
     *
     * Two shapes, because they stress different things:
     *   bench-many-routes.geojson  5000 short routes / 165k points - MANY OBJECTS (per-tile scan)
     *   bench-long-routes.geojson  8 routes of 100-250 km / 300k points - LONG LINES (re-clipping)
     *
     *   adb shell am start -n com.massifmaps.MassifDemo/.MainActivity --es ui false --es geojsonBench many
     *   adb shell am start -n com.massifmaps.MassifDemo/.MainActivity --es ui false --es geojsonBench long
     *
     * A file of the same name under the data directory wins over the asset, so another dataset can
     * be tried with a push and no rebuild.
     */
    public static void runGeoJSONBench(final DemoMap demo, final String which) {
        new Thread(new Runnable() {
            public void run() {
                if ("both".equalsIgnoreCase(which)) {
                    benchOne(demo, DemoConfig.GEOJSON_BENCH_MANY_NAME);
                    benchOne(demo, DemoConfig.GEOJSON_BENCH_LONG_NAME);
                } else if ("long".equalsIgnoreCase(which)) {
                    benchOne(demo, DemoConfig.GEOJSON_BENCH_LONG_NAME);
                } else if ("many".equalsIgnoreCase(which) || "true".equalsIgnoreCase(which)) {
                    benchOne(demo, DemoConfig.GEOJSON_BENCH_MANY_NAME);
                } else {
                    benchOne(demo, which); // an explicit asset / data-directory file name
                }
            }
        }).start();
    }

    /**
     * One BalloonPopup per '|' separated font list, stacked around the start camera. What it shows
     * is which font a name resolves to on THIS device - the popups draw with the platform text API,
     * not with the tile labels' FreeType path.
     *
     *   --es popupFonts sample                     the built-in set, one popup per capability
     *   --es popupFonts "Roboto|serif|monospace"   a set of your own
     */
    public static void runPopupFonts(final DemoMap demo, final String which) {
        // The live camera, for the panel button. At startup the focus latitude is not applied yet,
        // which is why DemoMap passes the configured start position instead.
        runPopupFonts(demo, which, demo.map.camera().position());
    }

    public static void runPopupFonts(final DemoMap demo, final String which, final Position wgs) {
        String fontLists = (which == null || which.isEmpty()
                || "sample".equalsIgnoreCase(which) || "true".equalsIgnoreCase(which))
                ? DemoConfig.POPUP_FONTS_SAMPLE : which;
        String[] fonts = fontLists.split("\\|");
        for (int i = 0; i < fonts.length; i++) {
            String styleId = "demo-popup-style-" + i;
            Massif.destroy("elementstyle", styleId);
            Massif.object("elementstyle", styleId, Spec.of("balloon")
                    .set("titleFontName", fonts[i]).set("titleFontSize", 18)
                    .set("descriptionFontName", fonts[i]).set("descriptionFontSize", 14));
            addResult(demo, Spec.of("balloon")
                    .set("position", new double[] { wgs.lng, wgs.lat + 0.0016 * (i - (fonts.length - 1) / 2.0) })
                    .set("style", styleId).set("title", fonts[i]).set("description", "Handgloves 0123"));
        }
        report(demo, "popup fonts: " + fonts.length);
    }

    /**
     * The same two datasets, but as a REAL layer (route style, same tesselator and shaders as the
     * route test) so the RENDER cost can be compared instead of the tile-build cost. Pan across an
     * empty area and across the data at the same zoom to separate "cost of having the layer" from
     * "cost of drawing the features".
     *
     *   --es geojsonLayer many|long|both     (add at startup, then pan)
     *   data around lon 5.55..6.05 / lat 44.95..45.31; lon 6.6 is empty at the same zoom.
     */
    public static void addGeoJSONBenchLayer(final DemoMap demo, final String which) {
        new Thread(new Runnable() {
            public void run() {
                if ("both".equalsIgnoreCase(which)) {
                    addBenchLayerOne(demo, DemoConfig.GEOJSON_BENCH_MANY_NAME);
                    addBenchLayerOne(demo, DemoConfig.GEOJSON_BENCH_LONG_NAME);
                } else if ("long".equalsIgnoreCase(which)) {
                    addBenchLayerOne(demo, DemoConfig.GEOJSON_BENCH_LONG_NAME);
                } else if ("many".equalsIgnoreCase(which) || "true".equalsIgnoreCase(which)) {
                    addBenchLayerOne(demo, DemoConfig.GEOJSON_BENCH_MANY_NAME);
                } else {
                    addBenchLayerOne(demo, which);
                }
            }
        }).start();
    }

    private static void addBenchLayerOne(final DemoMap demo, String name) {
        String geoJSON = demo.readDataOrAsset(name);
        if (geoJSON == null) {
            report(demo, "bench layer: " + name + " not found");
            return;
        }
        try {
            String id = "demo-bench-" + name.replaceAll("[^A-Za-z0-9]", "-");
            Massif.destroy("source", id);
            MassifSource source = Massif.source(id, Spec.of("geojson").set("minZoom", 0).set("maxZoom", 24)
                    .set("simplifyTolerance", DemoConfig.GEOJSON_BENCH_SIMPLIFY));
            int layerIndex = source.createLayer("route");
            long start = System.nanoTime();
            source.setLayerGeoJSON(layerIndex, geoJSON);
            long importMs = (System.nanoTime() - start) / 1000000L;

            Massif.destroy("style", id + "-style");
            Massif.style(id + "-style", Spec.of("mbvt").set("cartocss", Spec.of("cartocss").set("css", DemoStyles.routeTestStyle())));
            Massif.destroy("layer", id);
            final com.massifmaps.api.MassifLayer layer = Massif.layer(id, Spec.of("vector").set("source", id).set("style", id + "-style"));
            new Handler(Looper.getMainLooper()).post(new Runnable() {
                public void run() {
                    demo.map.add(layer);
                }
            });
            report(demo, "bench layer " + name + " added (import " + importMs + " ms)");
        } catch (Exception e) {
            report(demo, "bench layer failed: " + e.getMessage());
            Log.e(TAG, "bench layer failed", e);
        }
    }

    private static void benchOne(DemoMap demo, String name) {
        String geoJSON = demo.readDataOrAsset(name);
        if (geoJSON == null) {
            report(demo, "bench: " + name + " not found");
            return;
        }

        try {
            Massif.destroy("source", "demo-bench");
            MassifSource source = Massif.source("demo-bench", Spec.of("geojson").set("minZoom", 0).set("maxZoom", 24)
                    .set("simplifyTolerance", DemoConfig.GEOJSON_BENCH_SIMPLIFY));
            int layerIndex = source.createLayer("route");

            long importStart = System.nanoTime();
            source.setLayerGeoJSON(layerIndex, geoJSON);
            long importMs = (System.nanoTime() - importStart) / 1000000L;

            // The data extent, as the two corners [[minX, minY], [maxX, maxY]] in WGS84.
            org.json.JSONArray extent = new org.json.JSONArray(source.getString("dataExtent", "[[0,0],[0,0]]"));
            double lon = (extent.getJSONArray(0).getDouble(0) + extent.getJSONArray(1).getDouble(0)) * 0.5;
            double lat = (extent.getJSONArray(0).getDouble(1) + extent.getJSONArray(1).getDouble(1)) * 0.5;
            Log.i(TAG, "bench " + name + " centre " + lon + "," + lat);

            // Deepest zooms first: that is the order a user zooming in produces, and the order that
            // makes a per-tile feature scan hurt most (many tiles, few features in each).
            StringBuilder perZoom = new StringBuilder();
            long totalNs = 0;
            long totalBytes = 0;
            int tiles = 0;
            for (int zoom = DemoConfig.GEOJSON_BENCH_MAX_ZOOM; zoom >= DemoConfig.GEOJSON_BENCH_MIN_ZOOM; zoom--) {
                int span = 1 << zoom;
                int centerX = (int) Math.floor((lon + 180.0) / 360.0 * span);
                double latRad = Math.toRadians(lat);
                int centerY = (int) Math.floor((1.0 - Math.log(Math.tan(latRad) + 1.0 / Math.cos(latRad)) / Math.PI) / 2.0 * span);

                long zoomNs = 0;
                int side = DemoConfig.GEOJSON_BENCH_TILES_PER_SIDE;
                for (int dy = 0; dy < side; dy++) {
                    for (int dx = 0; dx < side; dx++) {
                        int x = centerX + dx - side / 2;
                        int y = centerY + dy - side / 2;
                        if (x < 0 || y < 0 || x >= span || y >= span) {
                            continue;
                        }
                        // The builder measures tile y from the NORTH edge, so this is plain XYZ.
                        long start = System.nanoTime();
                        MassifObject data = source.call("loadTile", new int[] { x, y, zoom });
                        zoomNs += System.nanoTime() - start;
                        tiles++;
                        if (data != null) {
                            totalBytes += data.data("data").length;
                            data.close();
                        }
                    }
                }
                totalNs += zoomNs;
                perZoom.append(" z").append(zoom).append("=").append(zoomNs / 1000000L);
            }

            String summary = "bench " + name + ": import " + importMs + " ms, " + tiles + " tiles in "
                    + (totalNs / 1000000L) + " ms (" + (totalNs / 1000L / Math.max(1, tiles)) + " us/tile), "
                    + totalBytes + " bytes";
            Log.i(TAG, summary);
            Log.i(TAG, "bench " + name + " per zoom (ms):" + perZoom);
            report(demo, summary);
        } catch (Exception e) {
            report(demo, "bench failed: " + e.getMessage());
            Log.e(TAG, "bench failed", e);
        }
    }

    /** The next frame as PNG bytes, through the facade: no object API involved. */
    public static void runCapture(final DemoMap demo) {
        demo.map.capture(false, new MassifMap.CaptureCallback() {
            public void onCapture(byte[] png) {
                boolean valid = png != null && png.length > 8 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G';
                report(demo, "capture: " + (png == null ? "failed" : png.length + " bytes, png=" + valid));
            }
        });
    }

    // =============================================================================================
    // HELPERS
    // =============================================================================================

    /** Toast + log, from any thread. */
    static void report(final DemoMap demo, final String message) {
        Log.i(TAG, message);
        new Handler(Looper.getMainLooper()).post(new Runnable() {
            public void run() {
                Toast.makeText(demo.mapView.getContext(), message, Toast.LENGTH_SHORT).show();
            }
        });
        demo.mapView.requestRender();
    }

    /** Logcat truncates at ~4k, so long JSON has to be split. */
    public static void largeLog(String tag, String content) {
        if (content.length() > 4000) {
            Log.d(tag, content.substring(0, 4000));
            largeLog(tag, content.substring(4000));
        } else {
            Log.d(tag, content);
        }
    }

    private DemoTests() {
    }
}
