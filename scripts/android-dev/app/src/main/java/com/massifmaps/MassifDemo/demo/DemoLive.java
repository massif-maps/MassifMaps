package com.massifmaps.MassifDemo.demo;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;

import com.massifmaps.api.EventListener;
import com.massifmaps.api.MassifApi;

/**
 * Applies intent extras to a RUNNING demo, so any knob can be changed from adb without a relaunch:
 *
 *   adb shell am broadcast -a com.massifmaps.MassifDemo.CONFIG --es fog false
 *   adb shell am broadcast -a com.massifmaps.MassifDemo.CONFIG --es fogPreset dusk --es fogSource style
 *
 * WHY THIS EXISTS (do not remove - scripted runs and AI agents depend on it): a relaunch rebuilds
 * every cache, which is exactly what hides a stale-redraw bug. Toggling live is the only way to see
 * whether a change reaches the drape bake, the shadow mask and the sky in the same frame.
 *
 * The extras are the same keys {@link DemoConfig#applyIntentOverrides()} reads at launch - they are
 * merged over the launch ones and the whole config is re-read, then only the option groups whose
 * keys arrived are re-applied. The camera is left alone unless a camera key is in the broadcast,
 * so toggling fog does not throw the view back to the start position.
 */
public final class DemoLive extends BroadcastReceiver {

    public static final String ACTION = "com.massifmaps.MassifDemo.CONFIG";

    private static final String TAG = "DemoLive";

    private static final String[] FOG_KEYS = {
        "fog", "fogPreset", "fogSource", "fogRangeStart", "fogRangeEnd",
        "fogHigh", "fogSpace", "fogStars", "fogBlend", "fogHorizon"
    };
    private static final String[] TERRAIN_KEYS = {
        "terrain", "drape", "drapeLines", "drapeResolution", "meshResolution", "exaggeration",
        "viewDistance", "viewDistanceMeters", "coarsening", "stitch", "textOcclusion", "lodFactor", "lodGrazing"
    };
    private static final String[] LIGHT_KEYS = {
        "daycycle", "sunHour", "sunAzimuth", "sunAltitude", "shadow", "shadowSoftness",
        "shadowDistance", "shadowCascades", "shadowNormalOffset", "terrainLight", "ambient",
        "ambientColor", "sunColor", "sunIntensity"
    };
    // Anything written INTO the style: the tiles carry it, so it only changes on a re-decode.
    private static final String[] STYLE_KEYS = {
        "style", "styleLight", "bg", "bldColor", "bld3d", "bldLight", "bldAmbient", "bldGradient", "bldGradientHeight",
        "bldAoRadius", "bldAoIntensity", "bldAoAttenuation", "bldAoStep", "bldEdgeRadius", "bldRoofShade", "bldRoundedRoof",
        "roadLabelOcclusion"
    };
    private static final String[] SKY_KEYS = { "sky", "skyColor", "horizonColor", "sunDisc" };
    private static final String[] CAMERA_KEYS = { "lon", "lat", "zoom", "tilt", "rotation" };

    private final DemoMap demo;

    public DemoLive(DemoMap demo) {
        this.demo = demo;
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        Bundle extras = intent != null ? intent.getExtras() : null;
        if (demo == null || extras == null || extras.isEmpty()) {
            return;
        }
        Log.i(TAG, "config " + extras);
        DemoCfg.merge(extras);
        // Re-read everything: each field defaults to its CURRENT value, so keys that were not sent
        // keep whatever the panel or a previous broadcast left them at.
        DemoConfig.applyIntentOverrides();

        if (has(extras, FOG_KEYS)) {
            demo.applyFogOptions();
            // The style carries the fog in STYLE mode, so it has to be decoded again.
            if (extras.containsKey("fogSource") || extras.containsKey("fogPreset")) {
                if (DemoConfig.FOG_SOURCE_STYLE.equals(DemoConfig.FOG_SOURCE)) {
                    demo.rebuildBaseLayer();
                }
            }
        }
        if (has(extras, TERRAIN_KEYS)) {
            demo.applyTerrainOptions();
        }
        if (has(extras, LIGHT_KEYS)) {
            demo.applyLightOptions();
        }
        if (has(extras, SKY_KEYS)) {
            demo.applySkyOptions();
        }
        if (has(extras, STYLE_KEYS)) {
            demo.rebuildBaseLayer();
        }
        if (has(extras, CAMERA_KEYS)) {
            demo.applyCamera();
        }
        if (extras.containsKey("apiCreate")) {
            // The map's layer list, so a layer built from a spec can be put on the map.
            if (MassifApi.findObject("layers", "demo") == 0) {
                MassifApi.adopt("layers", "demo", demo.mapView.getLayers());
            }
            applyApiCreate(extras.getString("apiCreate"));
        }
        if (extras.containsKey("apiSet")) {
            applyApiSet(extras.getString("apiSet"));
        }
        if (extras.containsKey("apiEvents")) {
            applyApiEvents("true".equals(extras.getString("apiEvents")));
        }
        if (extras.containsKey("apiCall")) {
            applyApiCall(extras.getString("apiCall"), "true".equals(extras.getString("apiAsync")));
        }
        if (extras.containsKey("apiSearch")) {
            applyApiSearch(extras.getString("apiSearch"));
        }
        if (extras.containsKey("apiMarkers")) {
            applyApiMarkers(Integer.parseInt(extras.getString("apiMarkers")));
        }
        if (extras.containsKey("apiMarkerDrag")) {
            applyApiMarkerDrag(Integer.parseInt(extras.getString("apiMarkerDrag")));
        }
        if (extras.containsKey("apiMoveRate")) {
            applyApiMoveRate(Integer.parseInt(extras.getString("apiMoveRate")));
        }
        if (extras.containsKey("apiRoute")) {
            applyApiRoute(extras.getString("apiRoute"), extras.getString("apiRouteProfile"));
        }
        if (extras.containsKey("apiSugar")) {
            applyApiSugar("true".equals(extras.getString("apiSugar")));
        }
        if (extras.containsKey("apiCancel")) {
            Log.i(TAG, "apiCancel " + apiCall + " -> " + MassifApi.cancelCall(apiCall));
        }
        demo.mapView.requestRender();
    }

    /**
     * Builds an object from a spec, so the create verb can be exercised live:
     *
     *   --es apiCreate 'projection:wgs84:{"type":"EPSG:4326"}'
     *   --es apiCreate 'source:extra:{"type":"http","url":"https://..."}'
     */
    private void applyApiCreate(String request) {
        String[] parts = request != null ? request.split(":", 3) : new String[0];
        if (parts.length < 3) {
            Log.w(TAG, "apiCreate wants kind:id:json, got: " + request);
            return;
        }
        try {
            Log.i(TAG, "apiCreate " + parts[0] + ":" + parts[1] + " -> handle="
                    + MassifApi.create(parts[0], parts[1], parts[2]));
        } catch (Exception e) {
            Log.w(TAG, "apiCreate failed: " + e.getMessage());
        }
    }

    /**
     * Drives a property through the facade API (#146) instead of the typed setters, so the
     * generated property table and its dotted path walking can be exercised on a device.
     *
     *   --es apiSet fogOptions.rangeStart=2.5            (the map's options, the default target)
     *   --es apiSet layer:demoApiLayer:opacity=0.3       (anything else in the registry)
     *   --es apiSet 'options:demo:baseProjection=@projection:wgs84'   (point at another object)
     */
    private void applyApiSet(String assignment) {
        int equals = assignment != null ? assignment.indexOf('=') : -1;
        if (equals <= 0) {
            Log.w(TAG, "apiSet wants [kind:id:]path=value, got: " + assignment);
            return;
        }
        String target = assignment.substring(0, equals);
        String value = assignment.substring(equals + 1);

        String kind = "options";
        String id = "demo";
        String path = target;
        int second = target.indexOf(':', target.indexOf(':') + 1);
        if (second > 0) {
            kind = target.substring(0, target.indexOf(':'));
            id = target.substring(target.indexOf(':') + 1, second);
            path = target.substring(second + 1);
        }

        int handle = MassifApi.findObject(kind, id);
        if (handle == 0 && "options".equals(kind) && "demo".equals(id)) {
            handle = MassifApi.adopt(kind, id, demo.mapView.getOptions());
        }
        if (handle == 0) {
            Log.w(TAG, "apiSet: nothing registered as " + kind + ":" + id);
            return;
        }

        int result;
        double before;
        if (value.startsWith("@")) {
            // @kind:id points an OBJECT property at another registered object, which is the one
            // shape a string cannot express.
            String[] ref = value.substring(1).split(":", 2);
            int other = ref.length == 2 ? MassifApi.findObject(ref[0], ref[1]) : 0;
            before = Double.NaN;
            result = MassifApi.setObject(handle, path, other);
            Log.i(TAG, "apiSet " + path + " <- " + value + " (handle=" + other
                    + ", result=" + result + ")");
            return;
        }
        try {
            before = MassifApi.getFloat(handle, path, Double.NaN);
            result = MassifApi.setFloat(handle, path, Double.parseDouble(value));
        } catch (NumberFormatException e) {
            before = Double.NaN;
            result = MassifApi.setString(handle, path, value);
        }
        // Both: a struct property carries JSON in the string, a scalar carries a number.
        Log.i(TAG, "apiSet " + kind + ":" + id + ":" + path + " " + before + " -> "
                + MassifApi.getFloat(handle, path, Double.NaN)
                + " json=" + MassifApi.getString(handle, path, "-")
                + " (handle=" + handle + ", result=" + result + ")");
    }

    /**
     * Runs a method through the facade API (#146), synchronously or on its worker thread:
     *
     *   --es apiCall 'source:demoApiSource:loadTile:[[8467,5852,14]]'
     *   --es apiCall 'source:demoApiSource:loadTile:[[8467,5852,14]]' --es apiAsync true
     *   --es apiCancel true                                    (cancels the last async call)
     *
     * The synchronous form blocks the caller, which for an HTTP source is the point of the
     * comparison: the same call with apiAsync true returns immediately and logs from the event.
     */
    private void applyApiCall(String request, boolean async) {
        String[] parts = request != null ? request.split(":", 4) : new String[0];
        if (parts.length < 3) {
            Log.w(TAG, "apiCall wants kind:id:method[:argsJson], got: " + request);
            return;
        }
        String args = parts.length > 3 ? parts[3] : "";
        int handle = MassifApi.findObject(parts[0], parts[1]);
        if (handle == 0) {
            Log.w(TAG, "apiCall: no " + parts[0] + " named " + parts[1]);
            return;
        }

        try {
            if (async) {
                final String event = parts[2] + ".done";
                apiCallListener = new EventListener() {
                    @Override
                    public boolean onEvent(int target, String name, int payload) {
                        logCallResult("async " + name, payload);
                        return false;
                    }
                };
                MassifApi.offEvent(handle, event);
                MassifApi.on(handle, event, apiCallListener, false, 1, false);
                apiCall = MassifApi.callAsync(handle, parts[2], args, event);
                Log.i(TAG, "apiCall " + parts[2] + " queued as " + apiCall + ", waiting for " + event
                        + " (--es apiCancel true to stop it)");
            } else {
                long start = System.currentTimeMillis();
                int result = MassifApi.call(handle, parts[2], args);
                logCallResult(parts[2] + " in " + (System.currentTimeMillis() - start) + " ms", result);
                // A sync result is owned by the caller; an async one is freed with the event.
                MassifApi.destroy(result);
            }
        } catch (Exception e) {
            Log.w(TAG, "apiCall failed: " + e.getMessage());
        }
    }

    /** Kept alive while subscribed, and holds the results read out of the event. */
    private EventListener apiSearchListener;

    /**
     * A search over the base layer's tiles, end to end through the facade (#146):
     *
     *   --es apiSearch 'Grenoble'
     *   --es apiSearch 'Grenoble:place,mountain_peak'
     *
     * Nothing about a search filter was taught to the facade: the request is created from a spec,
     * every filter on it is an ordinary property, and the result is read through the collection
     * channel (featureCount + getFeature). The positions come back in lon/lat because the service
     * reports its data source's projection and the result inherits it.
     *
     * ASYNC, not call: findFeatures fetches and decodes every tile in range, so running it on the
     * caller's thread ANRs the app - measured, at z14 over the start camera.
     */
    private void applyApiSearch(String request) {
        String[] parts = request != null ? request.split(":", 2) : new String[0];
        if (parts.length < 1 || parts[0].isEmpty()) {
            Log.w(TAG, "apiSearch wants a regex[:layer,layer], got: " + request);
            return;
        }

        // The demo builds its layers with the object API, so the search needs one under an id.
        com.massifmaps.layers.VectorTileLayer vector = null;
        for (int i = 0; i < demo.mapView.getLayers().count(); i++) {
            if (demo.mapView.getLayers().get(i) instanceof com.massifmaps.layers.VectorTileLayer) {
                vector = (com.massifmaps.layers.VectorTileLayer) demo.mapView.getLayers().get(i);
                break;
            }
        }
        if (vector == null) {
            Log.w(TAG, "apiSearch: no vector layer to search");
            return;
        }
        // Re-registered every time: rebuildBaseLayer replaces the SDK layer and the old id goes stale.
        MassifApi.unregisterObject("layer", "searchBase");
        MassifApi.adopt("layer", "searchBase", vector);
        MassifApi.unregisterObject("search", "demoSearch");
        final int service = MassifApi.create("search", "demoSearch",
                                             "{\"type\":\"vectortile\",\"layer\":\"searchBase\"}");
        if (service == 0) {
            Log.w(TAG, "apiSearch: could not build the service");
            return;
        }
        int zoom = (int) demo.mapView.getZoom();
        MassifApi.setFloat(service, "minZoom", zoom);
        MassifApi.setFloat(service, "maxZoom", zoom);
        MassifApi.setFloat(service, "maxResults", 10);
        if (parts.length > 1) {
            // A list of names is a struct property, so it is written as JSON like any other.
            Log.i(TAG, "apiSearch layers=" + parts[1] + " -> " + MassifApi.setString(
                    service, "layers", "[\"" + parts[1].replace(",", "\",\"") + "\"]"));
        }

        MassifApi.unregisterObject("search", "demoRequest");
        int query = MassifApi.create("search", "demoRequest", "{\"type\":\"request\"}");
        MassifApi.setString(query, "regexFilter", parts[0]);

        // BOUND IT. A request with no geometry searches every tile in the world at that zoom -
        // measured: the whole y=16383 row went past before the call had to be killed.
        // From the MAP's base projection, not the data source's: a MapView coordinate is in the
        // former, and reading it as the latter put the search box near null island.
        com.massifmaps.core.MapPos focus = demo.mapView.getOptions().getBaseProjection()
                .toWgs84(demo.mapView.getFocusPos());
        double span = 0.1;   // degrees, about 8 km at this latitude - a screenful at z14
        String box = corner(focus, -span, -span) + "," + corner(focus, span, -span) + ","
                   + corner(focus, span, span) + "," + corner(focus, -span, span) + ","
                   + corner(focus, -span, -span);
        MassifApi.unregisterObject("geometry", "demoBox");
        int area = MassifApi.create("geometry", "demoBox",
                "{\"geojson\":{\"type\":\"Polygon\",\"coordinates\":[[" + box + "]]}}");
        MassifApi.setObject(query, "geometry", area);
        int wgs84 = MassifApi.findObject("projection", "wgs84");
        if (wgs84 == 0) {
            wgs84 = MassifApi.create("projection", "wgs84", "{\"type\":\"EPSG:4326\"}");
        }
        MassifApi.setObject(query, "projection", wgs84);
        Log.i(TAG, "apiSearch around " + focus + " +/-" + span + " geometry=" + area);

        final String label = parts[0];
        final long start = System.currentTimeMillis();
        apiSearchListener = new EventListener() {
            @Override
            public boolean onEvent(int target, String name, int found) {
                long elapsed = System.currentTimeMillis() - start;
                if (found == 0) {
                    Log.i(TAG, "apiSearch '" + label + "' failed after " + elapsed + " ms");
                    return false;
                }
                int count = (int) MassifApi.getInt(found, "featureCount", 0);
                Log.i(TAG, "apiSearch '" + label + "' -> " + count + " in " + elapsed + " ms");
                for (int i = 0; i < count; i++) {
                    int feature = MassifApi.call(found, "getFeature", "[" + i + "]");
                    Log.i(TAG, "  " + i
                            + " id=" + MassifApi.getInt(feature, "id", -1)
                            + " layer=" + MassifApi.getString(feature, "layerName", "-")
                            + " tile=" + MassifApi.getString(feature, "mapTile", "-")
                            + " name=" + MassifApi.getString(feature, "properties.name", "-")
                            + " at=" + MassifApi.getPos(feature, "geometry.centerPos", "EPSG:4326"));
                    MassifApi.destroy(feature);
                }
                return false;
            }
        };
        MassifApi.offEvent(service, "search.done");
        MassifApi.on(service, "search.done", apiSearchListener, false, 1, false);
        apiCall = MassifApi.callAsync(service, "findFeatures", "[" + query + "]", "search.done");
        Log.i(TAG, "apiSearch '" + label + "' at z" + zoom + " queued as " + apiCall);
    }

    private com.massifmaps.datasources.GeoJSONVectorTileDataSource markerSource;
    private int markerLayerIndex = -1;
    private com.massifmaps.core.MapPos markerCentre;

    /**
     * MARKERS AS TILE FEATURES, to measure what replacing the vector-element path would cost.
     *
     *   --es apiMarkers 500        build N markers as GeoJSON features in one vector tile layer
     *   --es apiMarkerDrag 120     move ONE of them N times, to find the drag ceiling
     *
     * The question this answers: a Marker today is a VectorElement with its own renderer. As a tile
     * feature it costs nothing to draw beyond what tiles already cost, collides with tile labels and
     * drapes on terrain - but moving one means re-tiling, and that is the number that decides it.
     */
    private void applyApiMarkers(int count) {
        markerCentre = demo.mapView.getOptions().getBaseProjection()
                .toWgs84(demo.mapView.getFocusPos());

        long start = System.nanoTime();
        long imported;
        try {
            markerSource = new com.massifmaps.datasources.GeoJSONVectorTileDataSource(0, 24);
            markerLayerIndex = markerSource.createLayer("markers");
            markerSource.setLayerGeoJSONString(markerLayerIndex, markerFeatures(count));
            imported = (System.nanoTime() - start) / 1000000;
        } catch (Exception e) {
            Log.w(TAG, "apiMarkers failed: " + e);
            return;
        }

        com.massifmaps.styles.CartoCSSStyleSet style = new com.massifmaps.styles.CartoCSSStyleSet(
                "#markers{marker-fill:#e02020;marker-width:10;marker-allow-overlap:false;"
                + "text-name:[name];text-face-name:'Roboto Regular';text-size:11;text-dy:-12;}");
        com.massifmaps.layers.VectorTileLayer layer = new com.massifmaps.layers.VectorTileLayer(
                markerSource, new com.massifmaps.vectortiles.MBVectorTileDecoder(style));
        demo.mapView.getLayers().add(layer);
        Log.i(TAG, "apiMarkers " + count + " features imported in " + imported + " ms");
    }

    private String markerFeatures(int count) {
        StringBuilder json = new StringBuilder("{\"type\":\"FeatureCollection\",\"features\":[");
        for (int i = 0; i < count; i++) {
            // A deterministic spiral, so the same count always lays out the same way.
            double angle = i * 2.399963;
            double radius = 0.0006 * Math.sqrt(i);
            json.append(i > 0 ? "," : "").append(feature(i,
                    markerCentre.getX() + radius * Math.cos(angle),
                    markerCentre.getY() + radius * Math.sin(angle)));
        }
        return json.append("]}").toString();
    }

    private static String feature(int id, double lon, double lat) {
        return "{\"type\":\"Feature\",\"id\":" + id
             + ",\"properties\":{\"name\":\"m" + id + "\"}"
             + ",\"geometry\":{\"type\":\"Point\",\"coordinates\":[" + lon + "," + lat + "]}}";
    }

    /**
     * Moves one feature N times and reports what it really costs.
     *
     * TWO numbers, because the first one alone is misleading: updateGeoJSONFeature only writes the
     * in-memory feature store and calls notifyTilesChanged, so it returns long before anything is
     * redrawn. The re-encode happens on the tile thread in loadTile, and THAT is the drag ceiling.
     */
    private void applyApiMarkerDrag(int updates) {
        if (markerSource == null) {
            Log.w(TAG, "apiMarkerDrag: run --es apiMarkers N first");
            return;
        }
        int zoom = (int) demo.mapView.getZoom();
        com.massifmaps.core.MapTile tile = tileOf(markerCentre.getX(), markerCentre.getY(), zoom);

        long notifyNanos = 0, encodeNanos = 0;
        try {
            for (int i = 0; i < updates; i++) {
                String moved = feature(0,
                        markerCentre.getX() + 0.0004 * Math.cos(i * 0.2),
                        markerCentre.getY() + 0.0004 * Math.sin(i * 0.2));
                long a = System.nanoTime();
                markerSource.updateGeoJSONStringFeature(markerLayerIndex, moved);
                long b = System.nanoTime();
                // What the tile thread would do next, on this thread so it can be timed.
                markerSource.loadTile(tile);
                encodeNanos += System.nanoTime() - b;
                notifyNanos += b - a;
            }
        } catch (Exception e) {
            Log.w(TAG, "apiMarkerDrag failed: " + e);
            return;
        }
        double notify = notifyNanos / 1000000.0 / updates;
        double encode = encodeNanos / 1000000.0 / updates;
        Log.i(TAG, "apiMarkerDrag z" + zoom + " " + updates + " updates: notify "
                + String.format("%.2f", notify) + " ms + re-encode "
                + String.format("%.2f", encode) + " ms = "
                + String.format("%.2f", notify + encode) + " ms/tile -> "
                + Math.round(1000.0 / Math.max(notify + encode, 0.001)) + " tile-updates/s");
    }

    /** The web mercator tile a position falls in, so the bench re-encodes the one that changed. */
    private static com.massifmaps.core.MapTile tileOf(double lon, double lat, int zoom) {
        int n = 1 << zoom;
        int x = (int) Math.floor((lon + 180.0) / 360.0 * n);
        double rad = Math.toRadians(lat);
        int y = (int) Math.floor((1.0 - Math.log(Math.tan(rad) + 1.0 / Math.cos(rad)) / Math.PI)
                                 / 2.0 * n);
        return new com.massifmaps.core.MapTile(x, y, zoom, 0);
    }

    private EventListener apiMoveListener;
    private EventListener apiMoveCoalescedListener;

    /**
     * How often map.moved fires, RAW and COALESCED, over the same drag.
     *
     * This is what decides whether a native annotation view can follow the map. The answer turned
     * out to be the opposite of the worry - it fires far ABOVE frame rate, so the useful number is
     * what a coalescing subscription collapses it to.
     *
     *   --es apiMoveRate 3      then drag during the window
     */
    private void applyApiMoveRate(final int seconds) {
        // The sugar installs the UI dispatcher, and coalescing only applies to the queued path.
        if (sugarMap == null) {
            sugarMap = com.massifmaps.api.MassifMap.attach(demo.mapView, "demo");
        }
        final java.util.concurrent.atomic.AtomicInteger raw =
                new java.util.concurrent.atomic.AtomicInteger();
        final java.util.concurrent.atomic.AtomicInteger coalesced =
                new java.util.concurrent.atomic.AtomicInteger();
        int handle = MassifApi.findObject("options", "demo");
        if (handle == 0) {
            handle = MassifApi.adopt("options", "demo", demo.mapView.getOptions());
        }
        demo.mapView.setMapEventListener(
                MassifApi.createEventBridge(handle, demo.mapView.getMapEventListener()));

        apiMoveListener = new EventListener() {
            @Override
            public boolean onEvent(int target, String event, int payload) {
                raw.incrementAndGet();
                return false;
            }
        };
        apiMoveCoalescedListener = new EventListener() {
            @Override
            public boolean onEvent(int target, String event, int payload) {
                coalesced.incrementAndGet();
                return false;
            }
        };
        final int rawSub = MassifApi.on(handle, "map.moved", apiMoveListener, false, 0, false);
        final int coalescedSub = MassifApi.on(handle, "map.moved", apiMoveCoalescedListener, false, 1, true);
        final long start = System.currentTimeMillis();
        Log.i(TAG, "apiMoveRate counting map.moved for " + seconds + " s - drag now");
        new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(new Runnable() {
            public void run() {
                long elapsed = System.currentTimeMillis() - start;
                MassifApi.off(rawSub);
                MassifApi.off(coalescedSub);
                Log.i(TAG, "apiMoveRate " + elapsed + " ms: raw " + raw.get() + " ("
                        + Math.round(raw.get() * 1000.0 / elapsed) + "/s), coalesced "
                        + coalesced.get() + " ("
                        + Math.round(coalesced.get() * 1000.0 / elapsed) + "/s)");
            }
        }, seconds * 1000L);
    }

    /** Kept alive while subscribed, and does the reading when the route arrives. */
    private EventListener apiRouteListener;

    /**
     * A route from the SDK's OWN Valhalla service, end to end through the facade (#146):
     *
     *   --es apiRoute '45.1877,5.7249;45.1916,5.7148'
     *   --es apiRoute '...' --es apiRouteProfile bicycle
     *
     * The demo's other routing test (DemoTests.runOnlineRouting) drives routing-lib, a separate
     * library with its own JNI - this one drives massif::ValhallaOnlineRoutingService, which is what
     * the facade's "routing" kind builds. Public OSM endpoint, so no key and no data files.
     *
     * The result is read the way a search result is: counts are properties, elements are calls, and
     * the path itself comes back flat rather than as JSON.
     */
    private void applyApiRoute(String waypoints, String profile) {
        String[] stops = waypoints != null ? waypoints.split(";") : new String[0];
        if (stops.length < 2) {
            Log.w(TAG, "apiRoute wants lat,lon;lat,lon[;...], got: " + waypoints);
            return;
        }
        StringBuilder points = new StringBuilder("[");
        for (int i = 0; i < stops.length; i++) {
            String[] pair = stops[i].split(",");
            if (pair.length != 2) {
                Log.w(TAG, "apiRoute: '" + stops[i] + "' is not lat,lon");
                return;
            }
            // The request is built in EPSG:4326, so a point is [lon, lat].
            points.append(i > 0 ? "," : "").append("[").append(pair[1].trim())
                  .append(",").append(pair[0].trim()).append("]");
        }
        points.append("]");

        MassifApi.unregisterObject("routing", "demoRouter");
        int service = MassifApi.create("routing", "demoRouter", "{\"type\":\"valhalla-online\"}");
        MassifApi.setString(service, "customServiceURL",
                            "https://valhalla1.openstreetmap.de/{service}");
        MassifApi.setString(service, "profile", profile != null ? profile : "pedestrian");

        MassifApi.unregisterObject("routing", "demoRoutingRequest");
        final int query = MassifApi.create("routing", "demoRoutingRequest",
                "{\"type\":\"request\",\"projection\":\"EPSG:4326\",\"points\":" + points + "}");
        // Free-form JSON, so it is a call rather than a property.
        MassifApi.call(query, "setCustomParameter", "[\"language\",\"fr-FR\"]");

        final long start = System.currentTimeMillis();
        apiRouteListener = new EventListener() {
            @Override
            public boolean onEvent(int target, String name, int route) {
                long elapsed = System.currentTimeMillis() - start;
                if (route == 0) {
                    Log.i(TAG, "apiRoute failed after " + elapsed + " ms");
                    return false;
                }
                int count = (int) MassifApi.getInt(route, "instructionCount", 0);
                Log.i(TAG, "apiRoute " + MassifApi.getFloat(route, "totalDistance", 0) + " m, "
                        + MassifApi.getFloat(route, "totalTime", 0) + " s, "
                        + MassifApi.getInt(route, "pointCount", 0) + " points, "
                        + count + " instructions, in " + elapsed + " ms"
                        + " (" + MassifApi.getString(route, "projection.name", "-") + ")");
                for (int i = 0; i < count; i++) {
                    int step = MassifApi.call(route, "getInstruction", "[" + i + "]");
                    Log.i(TAG, "  " + i
                            + " action=" + MassifApi.getInt(step, "action", -1)
                            + " at=" + MassifApi.getInt(step, "pointIndex", -1)
                            + " " + MassifApi.getFloat(step, "distance", 0) + "m"
                            + " street=" + MassifApi.getString(step, "streetName", "-")
                            + " : " + MassifApi.getString(step, "instruction", "-"));
                    MassifApi.destroy(step);
                }
                // The path is flat, not JSON: one crossing for the whole polyline.
                int path = MassifApi.call(route, "getPoints", "");
                double[] flat = MassifApi.getDoubles(path);
                Log.i(TAG, "  path " + (flat.length / 2) + " positions, first=["
                        + (flat.length > 1 ? flat[0] + "," + flat[1] : "") + "] last=["
                        + (flat.length > 1 ? flat[flat.length - 2] + "," + flat[flat.length - 1] : "")
                        + "]");
                MassifApi.destroy(path);
                return false;
            }
        };
        MassifApi.offEvent(service, "route.done");
        MassifApi.on(service, "route.done", apiRouteListener, false, 1, false);
        // Async: the online service does an HTTP round trip on the calling thread.
        apiCall = MassifApi.callAsync(service, "calculateRoute", "[" + query + "]", "route.done");
        Log.i(TAG, "apiRoute " + points + " queued as " + apiCall);
    }

    private static String corner(com.massifmaps.core.MapPos centre, double dx, double dy) {
        return "[" + (centre.getX() + dx) + "," + (centre.getY() + dy) + "]";
    }

    private void logCallResult(String what, int result) {
        if (result == 0) {
            Log.i(TAG, "apiCall " + what + " -> failed");
            return;
        }
        // Three shapes of result: binary as bytes, bulk numerics as one flat array, anything else
        // as a document. None of them is an encoding of the others.
        byte[] data = MassifApi.getData(result, "data");
        double[] numbers = MassifApi.getDoubles(result);
        String shape = data.length > 0 ? " bytes=" + data.length
                     : numbers.length > 0 ? " doubles=" + numbers.length + " first=" + numbers[0]
                     : " json=" + MassifApi.getString(result, "", "-");
        Log.i(TAG, "apiCall " + what + " -> handle=" + result + shape);
    }

    private com.massifmaps.api.MassifMap sugarMap;
    private com.massifmaps.api.Subscription sugarClick;
    private com.massifmaps.api.Subscription sugarFeature;

    /**
     * The same events as applyApiEvents, through the Java sugar (#146), so the two can be compared
     * on one device:
     *
     *   adb shell am broadcast -a ...CONFIG --es apiSugar true
     *
     * The point of the comparison is the handler bodies. Above: getInt(payload, "featureId", -1)
     * and a JSON string to parse. Here: e.featureId() and a MapPos already in lon/lat.
     */
    private void applyApiSugar(boolean enable) {
        if (!enable) {
            if (sugarClick != null) { sugarClick.close(); sugarClick = null; }
            if (sugarFeature != null) { sugarFeature.close(); sugarFeature = null; }
            Log.i(TAG, "apiSugar off");
            return;
        }
        if (sugarMap == null) {
            sugarMap = com.massifmaps.api.MassifMap.attach(demo.mapView, "demo")
                                                   .eventProjection("EPSG:4326");
        }

        sugarClick = sugarMap.onClick(e ->
            Log.i(TAG, "sugar map.clicked at " + e.position() + " type=" + e.clickType()));

        // A layer the demo built through the registry, else ADOPT the first vector one - the demo
        // builds its layers with the object API, which is exactly the migration case.
        com.massifmaps.api.MassifLayer layer = sugarMap.layer("demoApiLayer");
        if (layer == null) {
            layer = sugarMap.adoptFirst("demoBase", com.massifmaps.layers.VectorTileLayer.class);
        }
        if (layer != null) {
            sugarFeature = layer.onFeatureClick(e ->
                Log.i(TAG, "sugar feature " + e.featureId()
                        + " layer=" + e.layerName()
                        + " at=" + e.position()
                        + " name=" + e.property("name")
                        + " geojsonLen=" + e.geoJson().length()));
        }
        Log.i(TAG, "apiSugar on, map=" + sugarMap.options().handle()
                + " fogRangeStart=" + sugarMap.fog().getDouble("rangeStart", -1)
                + " layer=" + layer);
    }

    /** Kept alive for as long as it is subscribed, or the director would be collected. */
    private EventListener apiListener;
    private EventListener apiCallListener;
    /** The last async call, so --es apiCancel true has something to cancel. */
    private int apiCall;
    private int apiSubscription;

    /**
     * Subscribes to the map's events through the facade (#146), so real callbacks can be seen
     * arriving as facade events with a readable payload:
     *
     *   adb shell am broadcast -a ...CONFIG --es apiEvents true
     *
     * The bridge chains to whatever listener the demo already installed, so the normal demo
     * behaviour keeps working alongside it.
     */
    private void applyApiEvents(boolean enable) {
        if (!enable) {
            Log.i(TAG, "apiEvents off, removed=" + MassifApi.off(apiSubscription));
            apiSubscription = 0;
            apiListener = null;
            return;
        }
        int handle = MassifApi.findObject("options", "demo");
        if (handle == 0) {
            handle = MassifApi.adopt("options", "demo", demo.mapView.getOptions());
        }
        demo.mapView.setMapEventListener(
            MassifApi.createEventBridge(handle, demo.mapView.getMapEventListener()));

        apiListener = new EventListener() {
            @Override
            public boolean onEvent(int target, String event, int payload) {
                Log.i(TAG, "apiEvent " + event + " payload=" + payload
                        + " clickPos=" + MassifApi.getString(payload, "clickPos", "-")
                        + " lonlat=" + MassifApi.getPos(payload, "clickPos", "EPSG:4326"));
                return false;
            }
        };
        apiSubscription = MassifApi.on(handle, "map.clicked", apiListener, false, 0, false);

        // The layer-level one, which is where a feature payload comes from.
        com.massifmaps.layers.VectorTileLayer vector = null;
        for (int i = 0; i < demo.mapView.getLayers().count(); i++) {
            if (demo.mapView.getLayers().get(i) instanceof com.massifmaps.layers.VectorTileLayer) {
                vector = (com.massifmaps.layers.VectorTileLayer) demo.mapView.getLayers().get(i);
                break;
            }
        }
        if (vector != null) {
            vector.setVectorTileEventListener(MassifApi.createVectorTileEventBridge(
                handle, vector.getVectorTileEventListener()));
            MassifApi.on(handle, "vectortile.clicked", new EventListener() {
                @Override
                public boolean onEvent(int target, String event, int payload) {
                    Log.i(TAG, "apiEvent " + event
                            + " id=" + MassifApi.getInt(payload, "featureId", -1)
                            + " layer=" + MassifApi.getString(payload, "featureLayerName", "-")
                            + " type=" + MassifApi.getInt(payload, "feature.geometry.type", -1)
                            // The subscription asked for EPSG:4326, so a plain read is lon/lat
                            // already; the source projection still needs naming per read.
                            + " pos=" + MassifApi.getString(payload, "featurePos", "-")
                            + " merc=" + MassifApi.getPos(payload, "featurePos", "EPSG:3857")
                            + " name=" + MassifApi.getString(payload, "feature.properties.name", "-")
                            + " geojsonLen=" + MassifApi.getString(payload, "feature.geometryGeoJSON", "").length());
                    return false;
                }
            }, false, 0, false, "EPSG:4326");
        }
        Log.i(TAG, "apiEvents on, handle=" + handle + " subscription=" + apiSubscription
                + " vectorLayer=" + (vector != null ? vector.getClass().getSimpleName() : "none"));
    }

    private static boolean has(Bundle extras, String[] keys) {
        for (String key : keys) {
            if (extras.containsKey(key)) {
                return true;
            }
        }
        return false;
    }
}
