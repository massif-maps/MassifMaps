package com.massifmaps.MassifDemo.examples;

import android.content.Context;
import android.graphics.Color;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

import com.massifmaps.api.MassifMap;

/**
 * The gear panel: the lighting, LOD and terrain knobs, on top of whatever the example set.
 *
 * It talks to the map through the FACADE and nothing else, so it works on every example without
 * knowing what any of them built - a path and a range is the whole description of a row. The bench
 * has its own panel (demo/DemoPanel.java) and cannot be reused here: that one writes DemoConfig and
 * calls back into DemoMap, neither of which an example has.
 *
 * Each row reads its starting value back off the map, so the panel opens showing what the example
 * actually set rather than a default the example overrode.
 */
public final class ExampleSettings {

    /** Which facade group a row writes to. */
    private enum Group { LIGHT, OPTIONS, TERRAIN, FOG }

    private static final Object[][] ROWS = {
        { "Light",                  null,                          null,   null,  null },
        { "sun altitude",           Group.LIGHT,   "sunAltitude",              -90f,   90f },
        { "sun azimuth",            Group.LIGHT,   "sunAzimuth",                 0f,  360f },
        { "sun intensity",          Group.LIGHT,   "sunIntensity",               0f,    2f },
        { "ambient intensity",      Group.LIGHT,   "ambientIntensity",           0f,    1f },
        { "shadow strength",        Group.LIGHT,   "shadowStrength",             0f,    1f },
        { "shadow softness",        Group.LIGHT,   "shadowSoftness",             0f,    4f },
        { "shadow distance",        Group.LIGHT,   "shadowDistance",             0f,    8f },

        { "Tiles",                  null,                          null,   null,  null },
        // The two halves of the LOD area test. The first coarsens everything as it gets further
        // away; the second bounds what the GRAZING angle alone may take, which is what stops a
        // tilted horizon swapping detail from side to side as the camera turns. 0 = no limit.
        { "tile LOD factor",        Group.OPTIONS, "tileLODFactor",              0f,    4f },
        { "LOD grazing limit",      Group.OPTIONS, "tileLODForeshorteningLimit", 0f,    4f },

        { "Terrain",                null,                          null,   null,  null },
        { "exaggeration",           Group.TERRAIN, "exaggeration",               0f,    3f },
        { "view distance factor",   Group.TERRAIN, "viewDistanceFactor",         0f,    4f },
        { "tile coarsening",        Group.TERRAIN, "maxTileZoomCoarsening",      0f,    6f },

        { "Fog",                    null,                          null,   null,  null },
        { "range start",            Group.FOG,     "rangeStart",                 0f,    8f },
        { "range end",              Group.FOG,     "rangeEnd",                   0f,   16f },
    };

    /** Builds the panel's contents into `panel`. Called once, when the gear is first opened. */
    public static void build(Context context, LinearLayout panel, final MassifMap map) {
        for (Object[] row : ROWS) {
            if (row[1] == null) {
                panel.addView(heading(context, (String) row[0]));
                continue;
            }
            addSlider(context, panel, map, (Group) row[1], (String) row[2],
                      (String) row[0], (Float) row[3], (Float) row[4]);
        }
    }

    private static TextView heading(Context context, String text) {
        TextView view = new TextView(context);
        view.setText(text);
        view.setTextColor(0xFFFFFFFF);
        view.setTextSize(13);
        view.setPadding(0, dp(context, 10), 0, dp(context, 2));
        return view;
    }

    private static void addSlider(Context context, LinearLayout panel, final MassifMap map,
                                  final Group group, final String path, final String label,
                                  final float min, final float max) {
        final TextView title = new TextView(context);
        title.setTextColor(0xCCFFFFFF);
        title.setTextSize(12);

        // Read back, so the panel opens on what the example set rather than on a default it
        // overrode. A group the example never built answers with the SDK's own value.
        double current = read(map, group, path, min);
        final SeekBar bar = new SeekBar(context);
        bar.setMax(1000);
        bar.setProgress((int) Math.max(0, Math.min(1000, (current - min) / (max - min) * 1000)));
        title.setText(label + "  " + format(current));

        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if (!fromUser) {
                    return;
                }
                double value = min + (max - min) * progress / 1000.0;
                title.setText(label + "  " + format(value));
                write(map, group, path, value);
            }

            @Override public void onStartTrackingTouch(SeekBar seekBar) { }
            @Override public void onStopTrackingTouch(SeekBar seekBar) { }
        });

        panel.addView(title);
        panel.addView(bar);
    }

    private static double read(MassifMap map, Group group, String path, float fallback) {
        try {
            switch (group) {
            case LIGHT:   return map.light().getDouble(path, fallback);
            case TERRAIN: return map.terrain().getDouble(path, fallback);
            case FOG:     return map.fog().getDouble(path, fallback);
            default:      return map.options().getDouble(path, fallback);
            }
        } catch (Exception ignored) {
            return fallback;
        }
    }

    private static void write(MassifMap map, Group group, String path, double value) {
        // An example with no terrain or no fog still has the option objects, so this always lands;
        // a path the build does not carry is a warning rather than a crash, and the panel is a
        // debugging tool - it must not take the map down with it.
        try {
            switch (group) {
            case LIGHT:   map.light().set(path, value); break;
            case TERRAIN: map.terrain().set(path, value); break;
            case FOG:     map.fog().set(path, value); break;
            default:      map.options().set(path, value); break;
            }
        } catch (Exception ignored) {
        }
    }

    private static String format(double value) {
        return Math.abs(value) >= 10 ? String.format(java.util.Locale.US, "%.0f", value)
                                     : String.format(java.util.Locale.US, "%.2f", value);
    }

    private static int dp(Context context, int value) {
        return Math.round(value * context.getResources().getDisplayMetrics().density);
    }

    private ExampleSettings() {
    }
}
