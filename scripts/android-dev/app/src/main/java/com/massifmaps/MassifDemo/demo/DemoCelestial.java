package com.massifmaps.MassifDemo.demo;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffXfermode;
import android.graphics.RectF;
import android.widget.Toast;

import com.massifmaps.api.MapEvents;
import com.massifmaps.api.Massif;
import com.massifmaps.api.MassifLayer;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifObject;
import com.massifmaps.api.Spec;



/**
 * The sun, the moon and their paths across the day, built on the generic celestial API.
 *
 * NOTHING here is a sun or a moon as far as the SDK is concerned: they are two sprites and two
 * arcs, placed by direction. The astronomy lives in {@link DemoAstro}, which is the point - the
 * same API carries an aircraft ({@link #addAircraft}) or a star catalogue ({@link DemoStars}) with
 * no SDK change at all.
 *
 * The positions are the REAL ones for the demo's date, hour and location, and the arcs are sampled
 * from the same ephemeris over the whole day, so the disc always sits exactly on its own arc. That
 * is a free correctness check on both: they are computed independently of each other.
 */
public final class DemoCelestial {

    private static final String META_NAME = "name";
    private static final int MOON_BITMAP_SIZE = 128;
    /** The sampling step of a daily path, in minutes. 10 is smooth at any field of view. */
    private static final int PATH_STEP_MINUTES = 10;

    private MassifLayer layer;
    private MassifObject sun;
    private MassifObject moon;
    private MassifObject sunPath;
    private MassifObject moonPath;
    private MassifMap map;
    private double lastMoonPhase = -1;

    /** Builds the layer and its objects. Added FIRST, so the map and the terrain draw over them. */
    public MassifLayer createLayer(final MassifMap map, final android.content.Context context) {
        this.map = map;
        Massif.destroy("layer", "demo-celestial");
        // Drawn after any post-process effect (still depth-tested, so a path still goes behind
        // the ridges): the relief look is for the ground, not for the objects over it.
        layer = Massif.layer("demo-celestial", Spec.of("celestial").set("postProcessed", false));

        sun = add("demo-sun", Spec.of("sprite")
                .set("angularSize", DemoConfig.CELESTIAL_SUN_SIZE)
                .set("color", argb(255, 244, 214, 255))
                .set("softness", 0.35)
                .set("clickRadius", 3)
                .set("metaData", Spec.object().set(META_NAME, "Sun")));
        moon = add("demo-moon", Spec.of("sprite")
                .set("angularSize", DemoConfig.CELESTIAL_MOON_SIZE)
                .set("color", argb(245, 245, 235, 255))
                .set("softness", 0.25)
                .set("clickRadius", 3)
                .set("metaData", Spec.object().set(META_NAME, "Moon")));
        sunPath = add("demo-sun-path", Spec.of("arc")
                .set("color", argb(255, 216, 120, 160))
                .set("width", DemoConfig.CELESTIAL_ARC_WIDTH)
                .set("belowHorizonVisible", false)
                .set("clickRadius", 2)
                .set("metaData", Spec.object().set(META_NAME, "Sun path")));
        moonPath = add("demo-moon-path", Spec.of("arc")
                .set("color", argb(170, 190, 255, 130))
                .set("width", DemoConfig.CELESTIAL_ARC_WIDTH)
                .set("belowHorizonVisible", false)
                .set("clickRadius", 2)
                .set("metaData", Spec.object().set(META_NAME, "Moon path")));
        this.context = context;

        layer.on(MapEvents.CELESTIAL_CLICKED, e -> {
            final String message = e.get("celestialObject.metaData." + META_NAME)
                    + "  az " + Math.round(Double.parseDouble(e.get("celestialObject.azimuth")))
                    + "°  alt " + Math.round(Double.parseDouble(e.get("celestialObject.altitude"))) + "°";
            Toast.makeText(context, message, Toast.LENGTH_SHORT).show();
        });
        return layer;
    }

    private android.content.Context context;

    /** A colour as the facade's ARGB int. */
    private static int argb(int r, int g, int b, int a) {
        return (a & 0xFF) << 24 | (r & 0xFF) << 16 | (g & 0xFF) << 8 | (b & 0xFF);
    }

    /** Builds an object under an id and puts it on the layer. */
    private MassifObject add(String id, Spec spec) {
        Massif.destroy("celestial", id);
        MassifObject object = Massif.object("celestial", id, spec);
        layer.call("add", object.handle()).close();
        return object;
    }

    /**
     * Places the objects for the demo's current date, time and location.
     *
     * Both bodies come from {@link DemoAstro} rather than from the light options, because the panel
     * can drive the sun's azimuth and altitude by hand: what is drawn here is always where the body
     * really is on that date, which is the only version of it whose daily arc means anything.
     */
    public void update() {
        if (layer == null) {
            return;
        }
        double hourUtc = DemoConfig.currentHourUtc();
        double lat = DemoConfig.START_LAT;
        double lon = DemoConfig.START_LON;
        double n = DemoAstro.daysSinceJ2000(DemoConfig.SUN_YEAR, DemoConfig.SUN_MONTH, DemoConfig.SUN_DAY, hourUtc);

        double[] sunDir = DemoAstro.sunHorizon(n, lat, lon);
        sun.call("setDirection", sunDir[0], sunDir[1], 0).close();

        double[] moonDir = DemoAstro.moonHorizon(n, lat, lon);
        moon.call("setDirection", moonDir[0], moonDir[1], 0).close();
        updateMoonPhase(n);

        // The path across the day, sampled from the same ephemeris every PATH_STEP_MINUTES from
        // midnight to midnight. A circle about the celestial pole would be a good enough sun path
        // (declination barely moves in a day), but the moon's does move - a quarter of the sky in a
        // day - so both are sampled and the two arcs are then the same kind of object.
        sunPath.call("setDirections", (Object) dailyPath(true, lat, lon)).close();
        moonPath.call("setDirections", (Object) dailyPath(false, lat, lon)).close();

        sun.set("visible", DemoConfig.CELESTIAL_SUN);
        moon.set("visible", DemoConfig.CELESTIAL_MOON);
        sunPath.set("visible", DemoConfig.CELESTIAL_ARC);
        moonPath.set("visible", DemoConfig.CELESTIAL_MOON_ARC);
        android.util.Log.i("DemoCelestial", "sun az " + Math.round(sunDir[0]) + " alt " + Math.round(sunDir[1])
                + ", moon az " + Math.round(moonDir[0]) + " alt " + Math.round(moonDir[1])
                + ", hour " + hourUtc + " UTC " + DemoConfig.SUN_YEAR + "-" + DemoConfig.SUN_MONTH + "-" + DemoConfig.SUN_DAY);
    }

    /** The body's track over the configured date, as alternating azimuth/altitude degrees. */
    private static double[] dailyPath(boolean isSun, double lat, double lon) {
        int count = 24 * 60 / PATH_STEP_MINUTES + 1;
        double[] directions = new double[2 * count];
        for (int i = 0; i < count; i++) {
            double n = DemoAstro.daysSinceJ2000(DemoConfig.SUN_YEAR, DemoConfig.SUN_MONTH, DemoConfig.SUN_DAY, i * PATH_STEP_MINUTES / 60.0);
            double[] horizon = isSun ? DemoAstro.sunHorizon(n, lat, lon) : DemoAstro.moonHorizon(n, lat, lon);
            directions[2 * i] = horizon[0];
            directions[2 * i + 1] = horizon[1];
        }
        return directions;
    }

    /**
     * Draws the moon with the phase it really has: a disc with a bite taken out of it by a second
     * ellipse, which is what a terminator is - the projection of the circle dividing the lit and
     * unlit halves. Painting it into the sprite's bitmap keeps this out of the SDK entirely.
     */
    private void updateMoonPhase(double n) {
        double[] phase = DemoAstro.moonPhase(n);
        if (!DemoConfig.CELESTIAL_MOON_PHASE) {
            if (lastMoonPhase >= 0) {
                moon.set("bitmap", null);
                lastMoonPhase = -1;
            }
            return;
        }
        double illuminated = phase[0];
        double signedPhase = phase[1] * illuminated;
        if (Math.abs(signedPhase - lastMoonPhase) < 0.01) {
            return; // a hundredth of a phase is invisible; do not rebuild the texture for it
        }
        lastMoonPhase = signedPhase;

        android.graphics.Bitmap bitmap = android.graphics.Bitmap.createBitmap(MOON_BITMAP_SIZE, MOON_BITMAP_SIZE, android.graphics.Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        float radius = MOON_BITMAP_SIZE * 0.5f;
        paint.setColor(0xFFF5F5EB);
        canvas.drawCircle(radius, radius, radius - 1, paint);

        // The terminator: an ellipse whose half-width goes from the full radius at new moon, through
        // zero at the quarter, to the full radius again at full moon - the same circle seen edge on.
        float terminator = (float) Math.abs(1.0 - 2.0 * illuminated) * (radius - 1);
        Paint eraser = new Paint(Paint.ANTI_ALIAS_FLAG);
        eraser.setXfermode(new PorterDuffXfermode(PorterDuff.Mode.CLEAR));
        boolean waxing = phase[1] > 0;
        // Waxing: lit on the side towards the sun, the western limb - drawn on the right, so the
        // dark half is the left one (angles run clockwise from 3 o'clock in Canvas).
        RectF disc = new RectF(1, 1, MOON_BITMAP_SIZE - 1, MOON_BITMAP_SIZE - 1);
        canvas.drawArc(disc, waxing ? 90 : -90, 180, true, eraser);
        RectF terminatorOval = new RectF(radius - terminator, 1, radius + terminator, MOON_BITMAP_SIZE - 1);
        if (illuminated < 0.5) {
            // Crescent: the terminator bulges INTO the lit half, so the ellipse erases as well.
            canvas.drawOval(terminatorOval, eraser);
        } else {
            // Gibbous: it bulges into the dark half, so the ellipse paints the moon back in.
            canvas.drawOval(terminatorOval, paint);
        }
        Massif.destroy("bitmap", "demo-moon-bitmap");
        moon.set("bitmap", Massif.object("bitmap", "demo-moon-bitmap", pngSpec(bitmap)));
    }

    static Spec pngSpec(android.graphics.Bitmap bitmap) {
        java.io.ByteArrayOutputStream png = new java.io.ByteArrayOutputStream();
        bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, png);
        return Spec.object().set("base64", android.util.Base64.encodeToString(png.toByteArray(), android.util.Base64.NO_WRAP));
    }

    /**
     * An aircraft or a satellite: the same API, anchored above a real place instead of by
     * direction, so it parallaxes with the map like anything else that has a location.
     */
    public MassifObject addAircraft(double lon, double lat, double altitudeMeters) {
        MassifObject aircraft = add("demo-aircraft", Spec.of("sprite")
                .set("screenSize", 24)
                .set("color", 0xFFFFFFFF)
                .set("metaData", Spec.object().set(META_NAME, "Aircraft")));
        aircraft.call("setPosition", new double[] { lon, lat }, altitudeMeters).close();
        return aircraft;
    }
}
