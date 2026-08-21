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

    /** Kept alive for as long as it is subscribed, or the director would be collected. */
    private EventListener apiListener;
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
