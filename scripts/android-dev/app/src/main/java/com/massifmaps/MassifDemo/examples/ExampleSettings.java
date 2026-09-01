package com.massifmaps.MassifDemo.examples;

import android.content.Context;
import android.view.View;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifObject;
import com.massifmaps.api.PropertyGroup;

/**
 * The gear panel: the light, sky, fog, terrain and LOD knobs, on top of whatever the example set.
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
    private enum Group { LIGHT, OPTIONS, TERRAIN, FOG, SKY }

    private enum Kind { SLIDER, INT, TOGGLE, CHOICE, AUTO_FLATTEN }

    /**
     * { Kind, Group, path, label, min|values, max } - or { null, section title, expanded } for a
     * heading, which collapses everything under it.
     */
    private static final Object[][] ROWS = {
        { null, "Light", Boolean.TRUE },
        { Kind.TOGGLE, Group.LIGHT, "dayCycleLightsEnabled", "day cycle lights" },
        { Kind.TOGGLE, Group.LIGHT, "sunOverridingStyle",    "sun overrides style" },
        { Kind.TOGGLE, Group.LIGHT, "terrainLightingEnabled", "light the ground" },
        // Forcing the sun by hand. An example that drives it off an hour rewrites both the next
        // time its own slider moves.
        { Kind.SLIDER, Group.LIGHT, "sunAltitude",      "sun altitude",      -90f,  90f },
        { Kind.SLIDER, Group.LIGHT, "sunAzimuth",       "sun azimuth",         0f, 360f },
        { Kind.SLIDER, Group.LIGHT, "sunIntensity",     "sun intensity",       0f,   2f },
        { Kind.SLIDER, Group.LIGHT, "ambientIntensity", "ambient intensity",   0f,   1f },

        { null, "Shadows", Boolean.FALSE },
        // Past 1 on purpose: 1 is the physical depth (the SDK scales it by the sun's share of the
        // light), and the panel has to be able to exaggerate it to see what a knob does.
        { Kind.SLIDER, Group.LIGHT, "shadowStrength", "strength", 0f, 2f },
        { Kind.SLIDER, Group.LIGHT, "shadowSoftness", "softness", 0f, 4f },
        { Kind.SLIDER, Group.LIGHT, "shadowDistance", "distance", 0f, 8f },
        { Kind.INT,    Group.LIGHT, "shadowCascades", "cascades", 1f, 4f },
        { Kind.INT,    Group.LIGHT, "shadowMapSize",  "map size", 512f, 4096f },

        // The atmosphere reads the SAME sun as the light, so an example that sweeps an hour gets a
        // sky that follows it with nothing else set - this is where it is turned off or made flat.
        { null, "Sky", Boolean.FALSE },
        { Kind.TOGGLE, Group.SKY, "enabled", "sky" },
        { Kind.CHOICE, Group.SKY, "type",    "type",
          new String[] { "SKY_TYPE_ATMOSPHERE", "SKY_TYPE_GRADIENT" } },
        { Kind.CHOICE, Group.SKY, "quality", "quality",
          new String[] { "SKY_QUALITY_LOW", "SKY_QUALITY_MEDIUM", "SKY_QUALITY_HIGH" } },
        { Kind.TOGGLE, Group.SKY, "sunDiscEnabled", "sun disc" },
        { Kind.SLIDER, Group.SKY, "atmosphereSunIntensity", "atmosphere sun",   0f,  30f },
        { Kind.SLIDER, Group.SKY, "atmosphereLuminance",    "exposure (lower = brighter)", 0.1f, 4f },
        { Kind.SLIDER, Group.SKY, "horizonBlend",           "horizon blend",    0f,  45f },

        { null, "Fog", Boolean.FALSE },
        { Kind.TOGGLE, Group.FOG, "enabled", "fog" },
        // In multiples of the camera-to-focus distance, so one pair holds at every zoom.
        { Kind.SLIDER, Group.FOG, "rangeStart",         "range start",     0f,     8f },
        { Kind.SLIDER, Group.FOG, "rangeEnd",           "range end",       0f,    16f },
        { Kind.SLIDER, Group.FOG, "horizonBlend",       "horizon blend",   0f,     1f },
        { Kind.SLIDER, Group.FOG, "starIntensity",      "stars",           0f,     1f },
        // Metres: the altitudes the haze fades out between, so summits stand clear of it.
        { Kind.SLIDER, Group.FOG, "verticalRangeStart", "vertical start",  0f,  4000f },
        { Kind.SLIDER, Group.FOG, "verticalRangeEnd",   "vertical end",    0f,  8000f },

        { null, "Terrain", Boolean.FALSE },
        { Kind.TOGGLE, Group.TERRAIN, "enabled",   "3D terrain" },
        { Kind.TOGGLE, Group.TERRAIN, "flattened", "flat (2D)" },
        // The auto 2D/3D switch, both halves at once: it fires past a tilt, or once the terrain's
        // on-screen parallax drops below a few pixels.
        { Kind.AUTO_FLATTEN, Group.TERRAIN, null, "auto 2D/3D" },
        { Kind.SLIDER, Group.TERRAIN, "autoFlattenTilt",     "auto tilt",       0f,  90f },
        { Kind.SLIDER, Group.TERRAIN, "autoFlattenParallax", "auto parallax px", 0f,  16f },
        { Kind.SLIDER, Group.TERRAIN, "autoFlattenDuration", "auto duration",    0f,   2f },
        { Kind.SLIDER, Group.TERRAIN, "exaggeration",        "exaggeration",     0f,   3f },
        { Kind.INT,    Group.TERRAIN, "meshResolution",      "mesh resolution",  8f, 128f },
        { Kind.TOGGLE, Group.TERRAIN, "drapeFillsEnabled",   "drape fills" },
        { Kind.TOGGLE, Group.TERRAIN, "drapeLinesEnabled",   "drape lines" },
        { Kind.INT,    Group.TERRAIN, "drapeResolution",     "drape resolution", 256f, 2048f },
        { Kind.SLIDER, Group.TERRAIN, "viewDistanceFactor",  "view distance",    0f,   4f },
        { Kind.INT,    Group.TERRAIN, "maxTileZoomCoarsening", "tile coarsening", 0f,  6f },
        { Kind.SLIDER, Group.TERRAIN, "cameraClearance",     "camera clearance", 0f, 500f },
        { Kind.SLIDER, Group.TERRAIN, "textOcclusionOpacity", "text behind relief", 0f, 1f },

        { null, "Tiles", Boolean.FALSE },
        // The two halves of the LOD area test. The first coarsens everything as it gets further
        // away; the second bounds what the GRAZING angle alone may take, which is what stops a
        // tilted horizon swapping detail from side to side as the camera turns. 0 = no limit.
        { Kind.SLIDER, Group.OPTIONS, "tileLODFactor",              "tile LOD factor",   0f, 4f },
        { Kind.SLIDER, Group.OPTIONS, "tileLODForeshorteningLimit", "LOD grazing limit", 0f, 4f },
        { Kind.INT,    Group.OPTIONS, "tileDrawSize",               "tile draw size",  128f, 512f },
        { Kind.TOGGLE, Group.OPTIONS, "debugTileBorders",           "tile borders" },
    };

    /** Builds the panel's contents into `panel`. Called once, when the gear is first opened. */
    public static void build(Context context, LinearLayout panel, final MassifMap map) {
        LinearLayout section = panel;
        for (Object[] row : ROWS) {
            if (row[0] == null) {
                section = addSection(context, panel, (String) row[1], (Boolean) row[2]);
                continue;
            }
            Kind kind = (Kind) row[0];
            Group group = (Group) row[1];
            String path = (String) row[2];
            String label = (String) row[3];
            switch (kind) {
            case TOGGLE:
                addToggle(context, section, map, group, path, label);
                break;
            case CHOICE:
                addChoice(context, section, map, group, path, label, (String[]) row[4]);
                break;
            case AUTO_FLATTEN:
                addAutoFlatten(context, section, map);
                break;
            default:
                addSlider(context, section, map, group, path, label,
                          (Float) row[4], (Float) row[5], kind == Kind.INT);
                break;
            }
        }
    }

    /** A collapsible heading; returns the container its rows go into. */
    private static LinearLayout addSection(Context context, LinearLayout panel, final String title,
                                           boolean expanded) {
        final TextView heading = new TextView(context);
        heading.setTextColor(0xFFFFFFFF);
        heading.setTextSize(13);
        heading.setPadding(0, dp(context, 10), 0, dp(context, 2));

        final LinearLayout body = new LinearLayout(context);
        body.setOrientation(LinearLayout.VERTICAL);
        body.setVisibility(expanded ? View.VISIBLE : View.GONE);
        heading.setText((expanded ? "▾ " : "▸ ") + title);
        heading.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                boolean show = body.getVisibility() != View.VISIBLE;
                body.setVisibility(show ? View.VISIBLE : View.GONE);
                heading.setText((show ? "▾ " : "▸ ") + title);
            }
        });

        panel.addView(heading);
        panel.addView(body);
        return body;
    }

    private static void addSlider(Context context, LinearLayout panel, final MassifMap map,
                                  final Group group, final String path, final String label,
                                  final float min, final float max, final boolean integer) {
        final TextView title = new TextView(context);
        title.setTextColor(0xCCFFFFFF);
        title.setTextSize(12);

        // Read back, so the panel opens on what the example set rather than on a default it
        // overrode. A group the example never built answers with the SDK's own value.
        double current = read(map, group, path, min);
        final SeekBar bar = new SeekBar(context);
        bar.setMax(integer ? Math.round(max - min) : 1000);
        bar.setProgress((int) Math.max(0, Math.min(bar.getMax(),
            (current - min) / (max - min) * bar.getMax())));
        title.setText(label + "  " + format(current));

        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if (!fromUser) {
                    return;
                }
                double value = min + (max - min) * progress / (double) seekBar.getMax();
                title.setText(label + "  " + format(value));
                write(map, group, path, integer ? (Object) Long.valueOf(Math.round(value))
                                                : (Object) Double.valueOf(value));
            }

            @Override public void onStartTrackingTouch(SeekBar seekBar) { }
            @Override public void onStopTrackingTouch(SeekBar seekBar) { }
        });

        panel.addView(title);
        panel.addView(bar);
    }

    private static void addToggle(Context context, LinearLayout panel, final MassifMap map,
                                  final Group group, final String path, String label) {
        CheckBox box = new CheckBox(context);
        box.setText(label);
        box.setTextColor(0xCCFFFFFF);
        box.setTextSize(12);
        box.setChecked(readBool(map, group, path));
        box.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton button, boolean checked) {
                write(map, group, path, Boolean.valueOf(checked));
            }
        });
        panel.addView(box);
    }

    /** An enum, cycled by tapping: its constants are few and the panel is one column wide. */
    private static void addChoice(Context context, LinearLayout panel, final MassifMap map,
                                  final Group group, final String path, final String label,
                                  final String[] values) {
        final TextView view = new TextView(context);
        view.setTextColor(0xCCFFFFFF);
        view.setTextSize(12);
        view.setPadding(0, dp(context, 4), 0, dp(context, 4));
        final int[] index = { (int) Math.max(0, Math.min(values.length - 1,
                                                         readLong(map, group, path))) };
        view.setText(label + "  " + shortName(values[index[0]]));
        view.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                index[0] = (index[0] + 1) % values.length;
                view.setText(label + "  " + shortName(values[index[0]]));
                // An enum is written by its constant NAME; an unknown name is refused rather than
                // read as 0.
                write(map, group, path, values[index[0]]);
            }
        });
        panel.addView(view);
    }

    /**
     * The auto 2D/3D switch as ONE control: off writes 0 to both halves, on puts back what they
     * held - or the SDK's own 88 degrees / 2 px when the example started with it off.
     */
    private static void addAutoFlatten(Context context, LinearLayout panel, final MassifMap map) {
        final double[] remembered = {
            read(map, Group.TERRAIN, "autoFlattenTilt", 88f),
            read(map, Group.TERRAIN, "autoFlattenParallax", 2f),
        };
        CheckBox box = new CheckBox(context);
        box.setText("auto 2D/3D on tilt");
        box.setTextColor(0xCCFFFFFF);
        box.setTextSize(12);
        box.setChecked(remembered[0] > 0 || remembered[1] > 0);
        box.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton button, boolean checked) {
                if (!checked) {
                    remembered[0] = read(map, Group.TERRAIN, "autoFlattenTilt", 0f);
                    remembered[1] = read(map, Group.TERRAIN, "autoFlattenParallax", 0f);
                }
                write(map, Group.TERRAIN, "autoFlattenTilt",
                      Double.valueOf(checked ? (remembered[0] > 0 ? remembered[0] : 88) : 0));
                write(map, Group.TERRAIN, "autoFlattenParallax",
                      Double.valueOf(checked ? (remembered[1] > 0 ? remembered[1] : 2) : 0));
            }
        });
        panel.addView(box);
    }

    private static double read(MassifMap map, Group group, String path, float fallback) {
        try {
            return group(map, group).getDouble(path, fallback);
        } catch (Exception ignored) {
            return fallback;
        }
    }

    private static boolean readBool(MassifMap map, Group group, String path) {
        try {
            return group(map, group).getBool(path, false);
        } catch (Exception ignored) {
            return false;
        }
    }

    private static long readLong(MassifMap map, Group group, String path) {
        try {
            return group(map, group).getLong(path, 0);
        } catch (Exception ignored) {
            return 0;
        }
    }

    private static void write(MassifMap map, Group group, String path, Object value) {
        // A path the example's build does not carry is a warning rather than a crash - the panel is
        // a debugging tool and must not take the map down with it.
        try {
            ensure(map, group);
            group(map, group).set(path, value);
        } catch (Exception ignored) {
        }
    }

    /**
     * Options starts with the light, sky and fog objects EMPTY, and writing through an empty one is
     * an error - so an example that never asked for a sky gets one here, on the first write. The
     * terrain is not built: it needs an elevation source only the example can name.
     */
    private static void ensure(MassifMap map, Group group) {
        if (group == Group.OPTIONS || group == Group.TERRAIN) {
            return;
        }
        String property = group == Group.LIGHT ? "lightOptions"
                        : group == Group.SKY ? "skyOptions" : "fogOptions";
        MassifObject child = map.options().child(property);
        if (child != null) {
            child.close();
            return;
        }
        switch (group) {
        case LIGHT: map.light(null); break;
        case SKY:   map.sky(null); break;
        default:    map.fog(null); break;
        }
    }

    private static PropertyGroup group(MassifMap map, Group group) {
        switch (group) {
        case LIGHT:   return map.light();
        case TERRAIN: return map.terrain();
        case FOG:     return map.fog();
        case SKY:     return map.sky();
        default:      return map.group("");
        }
    }

    /** `SKY_TYPE_ATMOSPHERE` reads as `atmosphere` in a row one column wide. */
    private static String shortName(String constant) {
        int cut = constant.lastIndexOf('_');
        return constant.substring(cut + 1).toLowerCase(java.util.Locale.US);
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
