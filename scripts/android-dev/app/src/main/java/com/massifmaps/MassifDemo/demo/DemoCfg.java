package com.massifmaps.MassifDemo.demo;

import android.content.Intent;
import android.util.Log;

/**
 * Intent-extra overrides for {@link DemoConfig}.
 *
 * WHY THIS EXISTS (do not remove - AI agents and scripted runs depend on it):
 * every default in DemoConfig can be overridden from adb without rebuilding the app, e.g.
 *
 *   adb shell am start -n com.massifmaps.MassifDemo/.MainActivity \
 *       --es base composite --es style dir --es hs true --es contour true \
 *       --es zoom 14.7 --es tilt 60 --es lon 5.760595 --es lat 45.244172
 *
 * The extras are read ONCE, in {@link DemoConfig#applyIntentOverrides()}, which is the single
 * place that maps a key name to a config field - look there for the full key list.
 *
 * Everything is a string extra ('--es'), because that is the only form that survives every adb
 * shell quoting variant. Numbers are parsed as floats, booleans with Boolean.parseBoolean.
 */
public final class DemoCfg {
    private static final String TAG = "DemoCfg";


    private static Intent sIntent;

    /** Called from the fragment before the config is read. */
    public static void attach(Intent intent) {
        sIntent = intent;
    }

    /**
     * Merges extras over the launch ones, so {@link DemoConfig#applyIntentOverrides()} can be run
     * again and see the union. This is what makes a knob changeable at RUNTIME - see
     * {@link DemoLive}, which is how a scripted run toggles something and screenshots the result
     * without relaunching the app (a relaunch rebuilds every cache and hides exactly the
     * stale-redraw bugs worth testing).
     */
    public static void merge(android.os.Bundle extras) {
        if (extras == null) {
            return;
        }
        if (sIntent == null) {
            sIntent = new Intent();
        }
        sIntent.putExtras(extras);
    }

    /** Raw extra value, or null when the extra was not passed. */
    public static String cfg(String key) {
        try {
            Object value = sIntent != null && sIntent.getExtras() != null ? sIntent.getExtras().get(key) : null;
            return value != null ? String.valueOf(value) : null;
        } catch (Exception e) {
            return null;
        }
    }

    public static String cfgStr(String key, String def) {
        String v = cfg(key);
        return v != null ? v : def;
    }

    public static boolean cfgBool(String key, boolean def) {
        String v = cfg(key);
        return v != null ? Boolean.parseBoolean(v) : def;
    }

    /**
     * A number, or the default when the extra is not one. `--es shadow true` used to take the whole
     * app down before the map was ever built: `shadow` is a STRENGTH, and one mistyped extra out of
     * thirty is not worth a crash on a bench.
     */
    public static float cfgFloat(String key, float def) {
        String v = cfg(key);
        if (v == null) {
            return def;
        }
        try {
            return Float.parseFloat(v);
        } catch (NumberFormatException e) {
            Log.w(TAG, "--es " + key + " " + v + " is not a number, keeping " + def);
            return def;
        }
    }

    public static int cfgInt(String key, int def) {
        return (int) cfgFloat(key, def);
    }

    /** '#' starts a comment in the adb shell, so colours are passed bare ("ff00ff"). */
    public static String cfgColor(String key, String def) {
        String v = cfg(key);
        if (v == null) {
            return def;
        }
        return v.startsWith("#") ? v : "#" + v;
    }

    /** ARGB int form of a colour extra; def is an ARGB int. */
    public static int cfgColorInt(String key, int def) {
        String v = cfg(key);
        if (v == null) {
            return def;
        }
        try {
            return android.graphics.Color.parseColor(v.startsWith("#") ? v : "#" + v);
        } catch (Exception e) {
            return def;
        }
    }

    /** Enum-valued extra, matched case-insensitively against the enum constant names. */
    public static <T extends Enum<T>> T cfgEnum(String key, T def, Class<T> type) {
        String v = cfg(key);
        if (v == null) {
            return def;
        }
        for (T candidate : type.getEnumConstants()) {
            if (candidate.name().equalsIgnoreCase(v)) {
                return candidate;
            }
        }
        return def;
    }

    private DemoCfg() {
    }
}
