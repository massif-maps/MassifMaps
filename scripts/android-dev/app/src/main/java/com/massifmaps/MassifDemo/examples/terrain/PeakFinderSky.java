package com.massifmaps.MassifDemo.examples.terrain;

import com.massifmaps.api.MassifException;
import com.massifmaps.api.MassifLayer;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifObject;
import com.massifmaps.api.Position;
import com.massifmaps.api.Spec;

import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * The peak finder's sun, ported from web/examples/peak-finder/sun.mjs: the day's path, hour marks,
 * and where it rises and sets over the TERRAIN in front of the viewpoint.
 */
final class PeakFinderSky {

    // Under LOW the sun is below any skyline, over HIGH above it: only between is the terrain measured.
    private static final double LOW = -4;
    private static final double HIGH = 40;
    private static final String[] COMPASS = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
    };

    private final MassifMap map;
    private final MassifObject path;
    private final MassifObject marks;
    private final MassifObject glow;
    private final MassifObject sun;
    private final MassifObject riseLabel;
    private final MassifObject setLabel;
    private final MassifObject[] hourLabels = new MassifObject[24];
    private final SimpleDateFormat clock = new SimpleDateFormat("HH:mm", Locale.getDefault());

    private double[] times = new double[0];
    private double[] azimuths = new double[0];
    private double[] altitudes = new double[0];
    private String pathKey = "";
    private String planKey = "";
    private long plannedAt;
    private String report = "";

    /** The path goes on `below`, under the summit names; the sun and the times on `above`. */
    PeakFinderSky(MassifMap map, MassifLayer below, MassifLayer above, float density) {
        this.map = map;
        // Widths are device pixels. Drawn under the horizon too: the terrain in front hides that part.
        path = add(below, "sky.path", Spec.of("arc").set("color", "#f59e0bd0").set("width", 3 * density)
            .set("belowHorizonVisible", true));
        marks = add(below, "sky.marks", Spec.of("arc").set("color", "#b45309").set("width", 2 * density)
            .set("belowHorizonVisible", true));
        glow = add(above, "sky.glow", Spec.of("sprite").set("screenSize", 56 * density)
            .set("color", "#fbbf2466").set("softness", 1));
        sun = add(above, "sky.sun", Spec.of("sprite").set("screenSize", 20 * density)
            .set("color", "#f59e0b").set("softness", 0.15));
        for (int hour = 0; hour < 24; hour++) {
            hourLabels[hour] = label(above, "sky.hour." + hour, Spec.of("label")
                .set("fontName", "sans-serif Medium").set("fontSize", 13).set("textColor", "#b45309")
                .set("haloColor", "#fffffff2").set("haloWidth", 4), 4);
        }
        riseLabel = label(above, "sky.rise", plate(), 14);
        setLabel = label(above, "sky.set", plate(), 14);
        // The rise and the set name the ridge they cross, so the ridge must not hide them.
        riseLabel.set("occludedByMap", false);
        setLabel.set("occludedByMap", false);
    }

    private static Spec plate() {
        return Spec.of("label").set("fontName", "sans-serif Bold").set("fontSize", 15)
            .set("textColor", "#92400e").set("backgroundColor", "#ffffffe6").set("backgroundRadius", 7)
            .set("paddingX", 7).set("paddingY", 3);
    }

    private MassifObject add(MassifLayer layer, String id, Spec spec) {
        MassifObject object = map.object("celestial", id, spec);
        layer.call("add", object.handle()).close();
        return object;
    }

    private MassifObject label(MassifLayer layer, String id, Spec spec, double lift) {
        MassifObject object = add(layer, id, spec.set("visible", false));
        object.call("setOffset", 0, lift).close();
        return object;
    }

    /**
     * Replanned when the view changes and once a second besides: the terrain the skyline is measured
     * on keeps arriving after a move. Returns the rise and set, for the caption.
     */
    String update(double lat, double lon, double eye, boolean hours) {
        Calendar midnight = Calendar.getInstance();
        long now = midnight.getTimeInMillis();
        midnight.set(Calendar.HOUR_OF_DAY, 0);
        midnight.set(Calendar.MINUTE, 0);
        midnight.set(Calendar.SECOND, 0);
        midnight.set(Calendar.MILLISECOND, 0);
        long day = midnight.getTimeInMillis();

        double[] position = sunAt(now, lat, lon);
        for (MassifObject object : new MassifObject[] { sun, glow }) {
            object.call("setDirection", position[0], apparent(position[1]), 0).close();
            object.set("visible", position[1] > -1.5);
        }
        String key = String.format(Locale.ROOT, "%d|%.5f|%.5f|%.1f|%b", day, lat, lon, eye, hours);
        if (!key.equals(planKey) || now - plannedAt > 1000) {
            planKey = key;
            plannedAt = now;
            planPath(day, lat, lon);
            planCrossings(lat, lon, eye, hours);
        }
        return report;
    }

    /** The whole circle the sun runs through that day, every 2 minutes. */
    private void planPath(long day, double lat, double lon) {
        String key = String.format(Locale.ROOT, "%d|%.3f|%.3f", day, lat, lon);
        if (key.equals(pathKey)) {
            return;
        }
        pathKey = key;
        int count = 1440 / 2 + 1;
        times = new double[count];
        azimuths = new double[count];
        altitudes = new double[count];
        double[] directions = new double[count * 2];
        for (int i = 0; i < count; i++) {
            times[i] = day + i * 2 * 60000.0;
            double[] position = sunAt(times[i], lat, lon);
            azimuths[i] = position[0];
            altitudes[i] = apparent(position[1]);
            directions[i * 2] = azimuths[i];
            directions[i * 2 + 1] = altitudes[i];
        }
        path.call("setDirections", directions).close();
    }

    /** Rise and set over the terrain: the skyline coarsely where the sun is low, finely where it crosses. */
    private void planCrossings(double lat, double lon, double eye, boolean hours) {
        Position viewpoint = new Position(lon, lat);
        List<Integer> coarse = new ArrayList<>();
        for (int i = 0; i < times.length; i += 4) {
            if (altitudes[i] > LOW && altitudes[i] < HIGH) {
                coarse.add(i);
            }
        }
        double[] coarseSkyline = skyline(viewpoint, eye, coarse);
        final double[] knownTimes = new double[coarse.size()];
        final double[] knownHeights = new double[coarse.size()];
        for (int k = 0; k < coarse.size(); k++) {
            knownTimes[k] = times[coarse.get(k)];
            knownHeights[k] = height(coarseSkyline, k);
        }

        double[] rise = null;
        double[] set = null;
        int step = 4;
        for (int index = step; index < times.length; index += step) {
            boolean up = margin(index - step, knownTimes, knownHeights) >= 0;
            if (up == (margin(index, knownTimes, knownHeights) >= 0)) {
                continue;
            }
            List<Integer> fine = new ArrayList<>();
            for (int i = index - step; i <= index; i++) {
                fine.add(i);
            }
            double[] fineSkyline = skyline(viewpoint, eye, fine);
            double[] fineMargin = new double[fine.size()];
            for (int k = 0; k < fine.size(); k++) {
                fineMargin[k] = altitudes[fine.get(k)] - height(fineSkyline, k);
            }
            int k = 1;
            while (k < fine.size() - 1 && (fineMargin[k] >= 0) == up) {
                k++;
            }
            double denominator = fineMargin[k - 1] - fineMargin[k];
            double fraction = Math.max(0, Math.min(1, fineMargin[k - 1] / (denominator != 0 ? denominator : 1)));
            int a = fine.get(k - 1);
            int b = fine.get(k);
            double[] crossing = {
                times[a] + fraction * (times[b] - times[a]),
                azimuths[a] + fraction * (azimuths[b] - azimuths[a]),
                altitudes[a] + fraction * (altitudes[b] - altitudes[a]),
            };
            if (!up) {
                rise = rise != null ? rise : crossing;
            } else {
                set = crossing;
            }
        }
        String riseText = rise != null ? "↑ " + clock((long) rise[0]) : "";
        String setText = set != null ? "↓ " + clock((long) set[0]) : "";
        report = rise == null && set == null ? "no sun over the terrain"
            : riseText + (rise != null && set != null ? "  ·  " : "") + setText;
        show(riseLabel, riseText, rise);
        show(setLabel, setText, set);

        // On the hour, a short stroke across the path and its time, clear of a rise or a set.
        List<Double> ticks = new ArrayList<>();
        for (int hour = 0; hour < 24; hour++) {
            int sample = hour * 30;
            boolean clear = (rise == null || Math.abs(rise[0] - times[sample]) > 40 * 60000)
                            && (set == null || Math.abs(set[0] - times[sample]) > 40 * 60000);
            if (!hours || !clear || margin(sample, knownTimes, knownHeights) < 0.5) {
                show(hourLabels[hour], "", null);
                continue;
            }
            show(hourLabels[hour], clock((long) times[sample]),
                 new double[] { times[sample], azimuths[sample], altitudes[sample] });
            double cos = Math.cos(Math.toRadians(altitudes[sample]));
            double dAz = (azimuths[sample + 1] - azimuths[sample]) * cos;
            double dAlt = altitudes[sample + 1] - altitudes[sample];
            double length = Math.hypot(dAz, dAlt);
            length = length != 0 ? length : 1;
            double nAz = -dAlt / length / cos * 0.35;
            double nAlt = dAz / length * 0.35;
            ticks.add(azimuths[sample] - nAz);
            ticks.add(altitudes[sample] - nAlt);
            ticks.add(azimuths[sample] + nAz);
            ticks.add(altitudes[sample] + nAlt);
        }
        double[] segments = new double[ticks.size()];
        for (int i = 0; i < segments.length; i++) {
            segments[i] = ticks.get(i);
        }
        marks.call("setSegments", segments).close();
    }

    /** One apparent skyline altitude per sample's azimuth; empty while the terrain has no elevation yet. */
    private double[] skyline(Position viewpoint, double eye, List<Integer> samples) {
        if (samples.isEmpty()) {
            return new double[0];
        }
        double[] directions = new double[samples.size()];
        for (int k = 0; k < directions.length; k++) {
            directions[k] = azimuths[samples.get(k)];
        }
        try {
            MassifObject horizon = map.options().call("terrainOptions.calculateHorizon", viewpoint, eye,
                                                      directions, 200000);
            try {
                return horizon.doubles();
            } finally {
                horizon.close();
            }
        } catch (MassifException e) {
            return new double[0];
        }
    }

    private static double height(double[] skyline, int k) {
        return k < skyline.length && skyline[k] > -90 ? skyline[k] : 0;
    }

    private double margin(int sample, double[] knownTimes, double[] knownHeights) {
        double alt = altitudes[sample];
        return alt <= LOW ? -1 : alt >= HIGH ? 1 : alt - skylineAt(times[sample], knownTimes, knownHeights);
    }

    private static double skylineAt(double time, double[] knownTimes, double[] knownHeights) {
        if (knownTimes.length == 0) {
            return 0;
        }
        int after = 0;
        while (after < knownTimes.length && knownTimes[after] < time) {
            after++;
        }
        if (after == knownTimes.length) {
            after = knownTimes.length - 1;
        }
        int before = Math.max(0, knownTimes[after] == time ? after : after - 1);
        double t0 = knownTimes[before];
        double t1 = knownTimes[after];
        double h0 = knownHeights[before];
        return t1 == t0 ? h0 : h0 + (knownHeights[after] - h0) * (time - t0) / (t1 - t0);
    }

    private static void show(MassifObject object, String text, double[] at) {
        if (text.isEmpty()) {
            object.set("visible", false);
            return;
        }
        object.set("text", text);
        object.call("setDirection", at[1], at[2], 0).close();
        object.set("visible", true);
    }

    private String clock(long time) {
        return clock.format(new Date(time));
    }

    static String compass(double heading) {
        return COMPASS[(int) Math.round((((heading % 360) + 360) % 360) / 22.5) % 16];
    }

    /** NOAA's low-accuracy solar position - the SDK's own (LightOptions::setSunPositionFromTime). */
    static double[] sunAt(double time, double lat, double lon) {
        double n = time / 86400000 + 2440587.5 - 2451545.0;
        double meanLong = (280.460 + 0.9856474 * n) % 360;
        double meanAnom = Math.toRadians((357.528 + 0.9856003 * n) % 360);
        double eclipticLong = Math.toRadians(meanLong + 1.915 * Math.sin(meanAnom) + 0.020 * Math.sin(2 * meanAnom));
        double obliquity = Math.toRadians(23.439 - 0.0000004 * n);
        double rightAsc = Math.atan2(Math.cos(obliquity) * Math.sin(eclipticLong), Math.cos(eclipticLong));
        double decl = Math.asin(Math.sin(obliquity) * Math.sin(eclipticLong));
        double gmst = (((18.697374558 + 24.06570982441908 * n) % 24) + 24) % 24;
        double hourAngle = Math.toRadians(gmst * 15 + lon) - rightAsc;
        double phi = Math.toRadians(lat);
        double alt = Math.asin(Math.sin(phi) * Math.sin(decl) + Math.cos(phi) * Math.cos(decl) * Math.cos(hourAngle));
        double az = Math.atan2(Math.sin(hourAngle), Math.cos(hourAngle) * Math.sin(phi) - Math.tan(decl) * Math.cos(phi));
        return new double[] { (Math.toDegrees(az) + 540) % 360, Math.toDegrees(alt) };
    }

    /** Where the air lifts the sun to (Saemundsson), so it is compared with an apparent skyline. */
    private static double apparent(double alt) {
        return alt + (alt > -2 ? 1.02 / Math.tan(Math.toRadians(alt + 10.3 / (alt + 5.11))) / 60 : 0);
    }
}
