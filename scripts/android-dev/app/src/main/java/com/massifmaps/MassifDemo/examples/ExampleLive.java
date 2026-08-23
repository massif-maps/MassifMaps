package com.massifmaps.MassifDemo.examples;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;

import com.massifmaps.api.MassifMap;
import com.massifmaps.api.PropertyGroup;
import com.massifmaps.core.MapPos;

import java.util.HashMap;
import java.util.Map;

/**
 * Changes a knob on a RUNNING example, from adb, with no relaunch:
 *
 *   adb shell am broadcast -a com.massifmaps.MassifDemo.CONFIG --es exaggeration 1.6
 *   adb shell am broadcast -a com.massifmaps.MassifDemo.CONFIG --es shadow 0.5 --es sunAltitude 25
 *
 * The bench has had this for a long time (see demo/DemoLive.java) and the examples did not, which
 * made the gallery useless for the thing the bench is used for: watching one value change without
 * rebuilding every cache around it.
 *
 * This one is NOT DemoLive. DemoLive drives DemoMap - the bench's own builder - through the object
 * API; an example only ever has a {@link MassifMap}. So every knob here is a facade property PATH
 * and the whole receiver is `set(path, value)`, which is also the point: if a knob needs a special
 * case here, the facade is missing something.
 *
 * The short adb key names are DemoLive's, so muscle memory carries over; the value on the right of
 * each entry is the facade path it writes.
 */
public final class ExampleLive extends BroadcastReceiver {

    public static final String ACTION = "com.massifmaps.MassifDemo.CONFIG";

    private static final String TAG = "ExampleLive";

    /** adb key -> the property group it belongs to, and the name inside it. */
    private static final Map<String, String[]> KNOBS = new HashMap<>();
    static {
        // terrain
        knob("terrain", "terrain", "enabled");
        knob("exaggeration", "terrain", "exaggeration");
        knob("meshResolution", "terrain", "meshResolution");
        knob("drape", "terrain", "drapeFillsEnabled");
        knob("drapeLines", "terrain", "drapeLinesEnabled");
        knob("drapeResolution", "terrain", "drapeResolution");
        knob("clearance", "terrain", "cameraClearance");
        knob("viewDistance", "terrain", "viewDistanceFactor");
        knob("viewDistanceMeters", "terrain", "viewDistance");
        knob("coarsening", "terrain", "maxTileZoomCoarsening");
        knob("stitch", "terrain", "tileEdgeStitchingEnabled");
        knob("textOcclusion", "terrain", "textOcclusionOpacity");
        knob("occlusionTolerance", "terrain", "billboardOcclusionTolerance");
        // light and shadows
        knob("terrainLight", "light", "terrainLightingEnabled");
        knob("sunAzimuth", "light", "sunAzimuth");
        knob("sunAltitude", "light", "sunAltitude");
        knob("sunIntensity", "light", "sunIntensity");
        knob("ambient", "light", "ambientIntensity");
        knob("shadow", "light", "shadowStrength");
        knob("shadowSoftness", "light", "shadowSoftness");
        knob("shadowDistance", "light", "shadowDistance");
        knob("shadowCascades", "light", "shadowCascades");
        knob("shadowNormalOffset", "light", "shadowNormalOffset");
        knob("shadowBias", "light", "shadowBias");
        // fog
        knob("fog", "fog", "enabled");
        knob("fogRangeStart", "fog", "rangeStart");
        knob("fogRangeEnd", "fog", "rangeEnd");
        knob("fogBlend", "fog", "horizonBlend");
        knob("fogHorizon", "fog", "horizonAngle");
        knob("fogStars", "fog", "starIntensity");
        // sky
        knob("sky", "sky", "enabled");
        knob("sunDisc", "sky", "sunDiscEnabled");
    }

    private static void knob(String key, String group, String property) {
        KNOBS.put(key, new String[] { group, property });
    }

    private static final String[] CAMERA_KEYS = { "lon", "lat", "zoom", "tilt", "rotation" };

    private final MassifMap map;

    public ExampleLive(MassifMap map) {
        this.map = map;
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        Bundle extras = intent != null ? intent.getExtras() : null;
        if (map == null || extras == null || extras.isEmpty()) {
            return;
        }
        Log.i(TAG, "config " + extras);
        for (String key : extras.keySet()) {
            String[] knob = KNOBS.get(key);
            if (knob == null) {
                continue;
            }
            // Everything arrives as a string (--es), and the facade coerces to the property's own
            // type - so "true", "1.6" and "64" all land correctly with no per-knob parsing here.
            String value = String.valueOf(extras.get(key));
            group(knob[0]).set(knob[1], value);
            Log.i(TAG, knob[0] + "." + knob[1] + " = " + value);
        }
        applyCamera(extras);
    }

    /**
     * An example that never asked for terrain has an EMPTY TerrainOptions, and writing through it
     * does nothing - Options starts with these unset. Nothing is built here on purpose: a knob
     * silently doing nothing on an example that has no terrain is the honest answer, and the
     * example that wants one says so in its own source.
     */
    private PropertyGroup group(String kind) {
        switch (kind) {
        case "light": return map.light();
        case "fog":   return map.fog();
        case "sky":   return map.sky();
        default:      return map.terrain();
        }
    }

    private void applyCamera(Bundle extras) {
        boolean any = false;
        for (String key : CAMERA_KEYS) {
            any |= extras.containsKey(key);
        }
        if (!any) {
            return;
        }
        MapPos at = map.camera().position();
        double lon = num(extras, "lon", at.getX());
        double lat = num(extras, "lat", at.getY());
        float zoom = (float) num(extras, "zoom", map.camera().zoom());
        float rotation = (float) num(extras, "rotation", map.camera().rotation());
        float tilt = (float) num(extras, "tilt", map.camera().tilt());
        map.camera().moveTo(new MapPos(lon, lat), zoom, rotation, tilt);
        Log.i(TAG, String.format("camera lon=%.5f lat=%.5f zoom=%.2f rotation=%.0f tilt=%.0f",
                                 lon, lat, zoom, rotation, tilt));
    }

    private static double num(Bundle extras, String key, double fallback) {
        Object value = extras.get(key);
        if (value == null) {
            return fallback;
        }
        try {
            return Double.parseDouble(String.valueOf(value));
        } catch (NumberFormatException e) {
            return fallback;
        }
    }
}
