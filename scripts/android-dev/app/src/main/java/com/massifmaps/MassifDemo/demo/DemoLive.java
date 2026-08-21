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
        if (extras.containsKey("apiSet")) {
            applyApiSet(extras.getString("apiSet"));
        }
        if (extras.containsKey("apiEvents")) {
            applyApiEvents("true".equals(extras.getString("apiEvents")));
        }
        if (extras.containsKey("apiCall")) {
            applyApiCall(extras.getString("apiCall"), "true".equals(extras.getString("apiAsync")));
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
     * Drives a property through the facade API (#146) instead of the typed setters, so the
     * generated property table and its dotted path walking can be exercised on a device.
     *
     *   --es apiSet fogOptions.rangeStart=2.5            (the map's options, the default target)
     *   --es apiSet layer:demoApiLayer:opacity=0.3       (anything else in the registry)
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
            handle = MassifApi.registerOptions(kind, id, demo.mapView.getOptions());
        }
        if (handle == 0) {
            Log.w(TAG, "apiSet: nothing registered as " + kind + ":" + id);
            return;
        }

        int result;
        double before;
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
                MassifApi.on(handle, event, apiCallListener, 1, false);
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

    private void logCallResult(String what, int result) {
        if (result == 0) {
            Log.i(TAG, "apiCall " + what + " -> failed");
            return;
        }
        // Three shapes of result: binary as bytes, bulk numerics as one flat array, anything else
        // as a document. None of them is an encoding of the others.
        com.massifmaps.core.BinaryData data = MassifApi.getData(result, "data");
        double[] numbers = MassifApi.getDoubles(result);
        String shape = data != null ? " bytes=" + data.size()
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

        // A layer by id if the demo built one through the registry, otherwise the first vector one.
        com.massifmaps.api.MassifLayer layer = sugarMap.layer("demoApiLayer");
        if (layer == null) {
            for (int i = 0; i < sugarMap.layerCount(); i++) {
                if (sugarMap.rawLayer(i) instanceof com.massifmaps.layers.VectorTileLayer) {
                    layer = sugarMap.layer("base");
                    break;
                }
            }
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
            handle = MassifApi.registerOptions("options", "demo", demo.mapView.getOptions());
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
        apiSubscription = MassifApi.on(handle, "map.clicked", apiListener, 0, false);

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
            }, 0, false, "EPSG:4326");
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
