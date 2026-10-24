package com.massifmaps.MassifDemo.demo;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Typeface;
import android.util.Log;
import android.widget.Toast;

import com.massifmaps.api.MapEvents;
import com.massifmaps.api.Massif;
import com.massifmaps.api.MassifLayer;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifObject;
import com.massifmaps.api.Spec;



import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * The star demo: the real sky over the demo's position, right now.
 *
 * Every star is a celestial sprite and every constellation figure is one celestial arc
 * in segment mode, so the whole catalogue is ONE draw call for the stars plus one per figure, and
 * both are clickable. The planets are sprites too - the API does not know what any of them are.
 *
 * The astronomy is in {@link DemoAstro} and the data in {@link DemoStarCatalogue}; this file is the
 * mapping onto the SDK.
 */
public final class DemoStars {

    private static final String TAG = "DemoStars";
    private static final String META_NAME = "name";
    private static final String META_INFO = "info";

    private MassifLayer layer;
    private int counter;
    private final List<MassifObject> stars = new ArrayList<MassifObject>();
    private final List<double[]> starEquatorial = new ArrayList<double[]>();   // { ra degrees, dec }
    private final List<MassifObject> figures = new ArrayList<MassifObject>();
    private final List<MassifObject> figureLabels = new ArrayList<MassifObject>();
    private final List<double[][]> figureEquatorial = new ArrayList<double[][]>(); // per segment { ra, dec, ra, dec }
    private final List<MassifObject> planets = new ArrayList<MassifObject>();
    private MassifObject equator;

    /** Builds the layer: stars, constellation figures, planets. Positions come from {@link #update}. */
    public MassifLayer createLayer(final MassifMap map, final android.content.Context context) {
        stars.clear();
        starEquatorial.clear();
        figures.clear();
        figureLabels.clear();
        figureEquatorial.clear();
        planets.clear();
        counter = 0;
        Massif.destroy("layer", "demo-stars");
        // see DemoCelestial: the sky objects keep their own look
        layer = Massif.layer("demo-stars", Spec.of("celestial").set("postProcessed", false));
        // The catalogue sizes are in pixels at 160 dpi, the unit the rest of the demo uses.
        final float density = context.getResources().getDisplayMetrics().density;

        Map<String, double[]> byName = new HashMap<String, double[]>();
        for (String entry : DemoStarCatalogue.STARS) {
            String[] parts = entry.split("\\|");
            if (parts.length != 4) {
                Log.w(TAG, "bad catalogue entry: " + entry);
                continue;
            }
            String name = parts[0];
            double raDegrees = Double.parseDouble(parts[1]) * 15.0;
            double declination = Double.parseDouble(parts[2]);
            double magnitude = Double.parseDouble(parts[3]);
            byName.put(name, new double[] { raDegrees, declination });

            // Brighter is bigger, the way a star chart draws them: a magnitude step is a factor of
            // 2.5 in brightness, but on screen a linear size ramp reads better than the real one.
            stars.add(add(Spec.of("sprite")
                    .set("screenSize", magnitudeToPixels(magnitude) * density)
                    .set("color", argb(255, 255, 250, 255))
                    .set("softness", 0.45)
                    .set("clickRadius", 1.5)
                    .set("metaData", Spec.object().set(META_NAME, name).set(META_INFO, "mag " + magnitude))));
            starEquatorial.add(new double[] { raDegrees, declination });
        }

        for (Map.Entry<String, String[][]> figure : DemoStarCatalogue.figures().entrySet()) {
            List<double[]> segments = new ArrayList<double[]>();
            for (String[] segment : figure.getValue()) {
                double[] from = byName.get(segment[0]);
                double[] to = byName.get(segment[1]);
                if (from == null || to == null) {
                    Log.w(TAG, figure.getKey() + ": no such star " + (from == null ? segment[0] : segment[1]));
                    continue;
                }
                segments.add(new double[] { from[0], from[1], to[0], to[1] });
            }
            if (segments.isEmpty()) {
                continue;
            }
            figures.add(add(Spec.of("arc")
                    .set("color", argb(120, 170, 255, 110))
                    .set("width", DemoConfig.STARS_FIGURE_WIDTH * density)
                    .set("belowHorizonVisible", false)
                    .set("clickRadius", DemoConfig.STARS_FIGURE_CLICK_RADIUS)
                    .set("metaData", Spec.object().set(META_NAME, figure.getKey()).set(META_INFO, segments.size() + " lines"))));
            figureEquatorial.add(segments.toArray(new double[segments.size()][]));

            // The name, IN THE SKY, at the middle of the figure. It is a plain sprite with a
            // bitmap the demo paints - the SDK has no text of its own here, which is exactly what
            // makes it themeable from the app: change the paint, change the label.
            int[] side = new int[1];
            MassifObject labelBitmap = textBitmap(context, figure.getKey(), density, side);
            // The bitmap is square and the text fills its width, so drawing the quad at the
            // bitmap's own pixel size renders the text at the size it was painted - the same for
            // every name. A fixed quad size would shrink the long ones instead.
            figureLabels.add(add(Spec.of("sprite")
                    .set("bitmap", labelBitmap.id())
                    .set("screenSize", side[0] * DemoConfig.STARS_LABEL_SCALE)
                    .set("color", argb(255, 255, 255, (int) Math.round(255 * DemoConfig.STARS_LABEL_OPACITY)))
                    .set("clickRadius", 0) // the figure itself is the clickable thing
                    .set("metaData", Spec.object().set(META_NAME, figure.getKey()).set(META_INFO, ""))));
        }

        for (String name : DemoAstro.PLANET_NAMES) {
            planets.add(add(Spec.of("sprite")
                    .set("angularSize", DemoConfig.STARS_PLANET_SIZE)
                    .set("color", planetColor(name))
                    .set("softness", 0.4)
                    .set("clickRadius", 2.5)
                    .set("metaData", Spec.object().set(META_NAME, name).set(META_INFO, "planet"))));
        }

        // The celestial equator: the one line that makes the sky's rotation readable, and a circle
        // about the pole like the sun's daily path - the same object, radius 90 degrees.
        equator = add(Spec.of("arc")
                .set("color", argb(90, 200, 190, 90))
                .set("width", 1.5)
                .set("belowHorizonVisible", false)
                .set("clickRadius", 1.5)
                .set("metaData", Spec.object().set(META_NAME, "Celestial equator").set(META_INFO, "declination 0")));

        layer.on(MapEvents.CELESTIAL_CLICKED, e -> {
            StringBuilder message = new StringBuilder(String.valueOf(e.get("celestialObject.metaData." + META_NAME)));
            String info = e.get("celestialObject.metaData." + META_INFO);
            if (info != null && info.length() > 0) {
                message.append("  ").append(info);
            }
            String azimuth = e.get("celestialObject.azimuth");
            // Only a sprite HAS one direction - a curve is a set of them.
            if (azimuth != null && !Double.isNaN(Double.parseDouble(azimuth))) {
                message.append("   az ").append(Math.round(Double.parseDouble(azimuth)))
                       .append("° alt ").append(Math.round(Double.parseDouble(e.get("celestialObject.altitude")))).append("°");
            }
            final String text = message.toString();
            Log.i(TAG, "clicked " + text);
            Toast.makeText(context, text, Toast.LENGTH_SHORT).show();
        });
        return layer;
    }

    private MassifObject add(Spec spec) {
        String id = "demo-star-" + (++counter);
        Massif.destroy("celestial", id);
        MassifObject object = Massif.object("celestial", id, spec);
        layer.call("add", object.handle()).close();
        return object;
    }

    /** A colour as the facade's ARGB int. */
    private static int argb(int r, int g, int b, int a) {
        return (a & 0xFF) << 24 | (r & 0xFF) << 16 | (g & 0xFF) << 8 | (b & 0xFF);
    }

    /**
     * Places everything for a date and a position: the whole catalogue turned into the horizon
     * frame. This is what the sky really looks like from there at that moment.
     */
    public void update(double n, double lat, double lon) {
        if (layer == null) {
            return;
        }
        for (int i = 0; i < stars.size(); i++) {
            double[] eq = starEquatorial.get(i);
            double[] horizon = DemoAstro.toHorizon(eq[0], eq[1], n, lat, lon);
            MassifObject star = stars.get(i);
            star.call("setDirection", horizon[0], horizon[1], 0).close();
            // A star below the horizon is under the ground: not drawn, not clickable, not paid for.
            star.set("visible", DemoConfig.STARS_STARS && horizon[1] > -2.0);
        }

        for (int i = 0; i < figures.size(); i++) {
            double[][] segments = figureEquatorial.get(i);
            double[] directions = new double[segments.length * 4];
            for (int s = 0; s < segments.length; s++) {
                double[] segment = segments[s];
                double[] from = DemoAstro.toHorizon(segment[0], segment[1], n, lat, lon);
                double[] to = DemoAstro.toHorizon(segment[2], segment[3], n, lat, lon);
                directions[4 * s] = from[0];
                directions[4 * s + 1] = from[1];
                directions[4 * s + 2] = to[0];
                directions[4 * s + 3] = to[1];
            }
            figures.get(i).call("setSegments", (Object) directions).close();
            figures.get(i).set("visible", DemoConfig.STARS_FIGURES);

            // The label goes at the MEAN DIRECTION of the figure, averaged as vectors: averaging
            // azimuths would put a figure straddling north somewhere near south.
            double x = 0, y = 0, z = 0;
            for (int k = 0; k + 1 < directions.length; k += 2) {
                double az = Math.toRadians(directions[k]), alt = Math.toRadians(directions[k + 1]);
                x += Math.cos(alt) * Math.sin(az);
                y += Math.cos(alt) * Math.cos(az);
                z += Math.sin(alt);
            }
            MassifObject label = figureLabels.get(i);
            double length = Math.sqrt(x * x + y * y + z * z);
            if (length > 0) {
                double altitude = Math.toDegrees(Math.asin(z / length));
                double azimuth = DemoAstro.normalizeDegrees(Math.toDegrees(Math.atan2(x, y)));
                label.call("setDirection", azimuth, altitude, 0).close();
                label.set("visible", DemoConfig.STARS_FIGURES && DemoConfig.STARS_LABELS && altitude > 0);
            } else {
                label.set("visible", false);
            }
        }

        for (int i = 0; i < planets.size(); i++) {
            double[] horizon = DemoAstro.planetHorizon(i, n, lat, lon);
            MassifObject planet = planets.get(i);
            planet.call("setDirection", horizon[0], horizon[1], 0).close();
            planet.set("visible", DemoConfig.STARS_PLANETS && horizon[1] > -2.0);
        }

        // Declination 0 is 90 degrees from the pole, and the pole sits due north (south below the
        // equator) at an altitude equal to the latitude.
        equator.call("setCircle", lat >= 0 ? 0f : 180f, Math.abs(lat), 90f).close();
        equator.set("visible", DemoConfig.STARS_EQUATOR);
    }

    /**
     * The name painted into a square bitmap, which is what a celestial sprite draws. Square
     * because the sprite quad is: the text is centred in it and the transparent margin costs
     * nothing but texture.
     */
    private MassifObject textBitmap(android.content.Context context, String text, float density, int[] sideOut) {
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        paint.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
        paint.setTextSize(DemoConfig.STARS_LABEL_TEXT_SIZE * density);
        paint.setColor(0xFFFFFFFF);
        Rect bounds = new Rect();
        paint.getTextBounds(text, 0, text.length(), bounds);
        int side = Math.max(16, Math.max(bounds.width(), bounds.height()) + Math.round(8 * density));
        sideOut[0] = side;
        android.graphics.Bitmap bitmap = android.graphics.Bitmap.createBitmap(side, side, android.graphics.Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        // A dark outline, so a name stays readable over a bright sky as well as over black.
        Paint outline = new Paint(paint);
        outline.setStyle(Paint.Style.STROKE);
        outline.setStrokeWidth(3 * density);
        outline.setColor(0xC0000000);
        float x = side * 0.5f - bounds.exactCenterX();
        float y = side * 0.5f - bounds.exactCenterY();
        canvas.drawText(text, x, y, outline);
        canvas.drawText(text, x, y, paint);
        return DemoCelestial.pngBitmap("demo-star-label-" + (++counter), bitmap);
    }

    /** A star of this magnitude, in screen pixels. */
    private static double magnitudeToPixels(double magnitude) {
        double size = DemoConfig.STARS_BRIGHTEST_SIZE - DemoConfig.STARS_SIZE_PER_MAGNITUDE * (magnitude + 1.5);
        return Math.max(DemoConfig.STARS_FAINTEST_SIZE, size);
    }

    private static int planetColor(String name) {
        if ("Mars".equals(name)) {
            return argb(255, 130, 90, 255);
        }
        if ("Venus".equals(name)) {
            return argb(255, 250, 220, 255);
        }
        if ("Jupiter".equals(name)) {
            return argb(255, 235, 180, 255);
        }
        if ("Saturn".equals(name)) {
            return argb(240, 220, 160, 255);
        }
        return argb(220, 220, 230, 255);
    }
}
