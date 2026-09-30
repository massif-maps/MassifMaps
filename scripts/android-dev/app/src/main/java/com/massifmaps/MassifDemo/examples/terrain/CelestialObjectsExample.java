package com.massifmaps.MassifDemo.examples.terrain;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MapEvents;
import com.massifmaps.api.MassifLayer;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifObject;
import com.massifmaps.api.Position;
import com.massifmaps.api.Spec;

/**
 * Stars and a constellation over the Matterhorn at dusk, and a camera that can look up at them.
 */
@ExampleInfo(
    id = "celestial-objects",
    title = "Objects in the sky",
    description = "Stars, the figure of Orion and their names drawn in the sky over 3D terrain, "
                + "setting behind the Matterhorn as the sky turns. The camera looks up at them, "
                + "and a tap names the star.",
    section = Sections.TERRAIN,
    order = 30)
public class CelestialObjectsExample extends MapExample {

    private static final String UA =
        "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    private static final Position MATTERHORN = new Position(7.6586, 45.9763);

    // Name, right ascension in hours, declination in degrees (J2000), magnitude, colour.
    private static final Object[][] STARS = {
        { "Betelgeuse", 5.9195, 7.407, 0.5, "#ffc58f" },
        { "Rigel", 5.2423, -8.2016, 0.13, "#cad8ff" },
        { "Bellatrix", 5.4189, 6.3497, 1.64, "#d5e2ff" },
        { "Mintaka", 5.5334, -0.2991, 2.2, "#d5e2ff" },
        { "Alnilam", 5.6036, -1.2019, 1.69, "#d5e2ff" },
        { "Alnitak", 5.6793, -1.9426, 1.77, "#d5e2ff" },
        { "Saiph", 5.7959, -9.6696, 2.06, "#d5e2ff" },
        { "Meissa", 5.5856, 9.9342, 3.39, "#d5e2ff" },
        { "Sirius", 6.7525, -16.7161, -1.46, "#e6eeff" },
        { "Procyon", 7.655, 5.225, 0.34, "#fff4e8" },
        { "Aldebaran", 4.5987, 16.5093, 0.86, "#ffcf9e" },
        { "Capella", 5.2782, 45.998, 0.08, "#fff1c9" },
        { "Pollux", 7.7553, 28.026, 1.14, "#ffe3b8" },
        { "Castor", 7.5767, 31.888, 1.58, "#e6eeff" },
    };
    private static final String[] NAMED = {
        "Betelgeuse", "Rigel", "Sirius", "Procyon", "Aldebaran", "Capella", "Pollux",
    };
    private static final String[] ORION = {
        "Betelgeuse", "Bellatrix", "Betelgeuse", "Alnitak", "Bellatrix", "Mintaka", "Mintaka", "Alnilam",
        "Alnilam", "Alnitak", "Alnitak", "Saiph", "Mintaka", "Rigel", "Betelgeuse", "Meissa", "Meissa", "Bellatrix",
    };
    // The sun of an April evening: it lights the sky from under the horizon.
    private static final double[] SUN = { 1.5, 9.5 };

    private ExampleHost host;
    private MassifObject figure;
    private MassifObject title;
    private final MassifObject[] stars = new MassifObject[STARS.length];
    private final MassifObject[] names = new MassifObject[NAMED.length];
    private double siderealTime = 132;
    private boolean turning;

    /** Equatorial to horizontal: the SDK only knows directions, the astronomy is the app's. */
    private static double[] direction(double ra, double dec, double siderealTime) {
        double hourAngle = Math.toRadians(siderealTime - ra * 15);
        double phi = Math.toRadians(MATTERHORN.lat);
        double delta = Math.toRadians(dec);
        double alt = Math.asin(Math.sin(phi) * Math.sin(delta) + Math.cos(phi) * Math.cos(delta) * Math.cos(hourAngle));
        double az = Math.atan2(Math.sin(hourAngle), Math.cos(hourAngle) * Math.sin(phi) - Math.tan(delta) * Math.cos(phi));
        return new double[] { (Math.toDegrees(az) + 540) % 360, Math.toDegrees(alt) };
    }

    private double[] at(String name) {
        for (Object[] star : STARS) {
            if (star[0].equals(name)) {
                return direction((Double) star[1], (Double) star[2], siderealTime);
            }
        }
        throw new IllegalArgumentException(name);
    }

    @Override
    public void onStart(ExampleHost exampleHost) {
        this.host = exampleHost;
        final MassifMap map = host.map();
        float density = host.context().getResources().getDisplayMetrics().density;

        map.addLayer("satellite", Spec.of("raster")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("world-imagery.db"))
                .set("capacity", 200 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://server.arcgisonline.com/ArcGIS/rest/services/"
                              + "World_Imagery/MapServer/tile/{z}/{y}/{x}")
                    .set("maxZoom", 18)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA)))));
        map.style("hybrid", Spec.of("mbvt")
            .set("project", Spec.of("project")
                .set("assets", Spec.of("zip")
                    .set("data", Spec.of("url").set("url", "assets://styles/massif.zip")))
                .set("name", "hybrid")));
        map.addLayer("labels", Spec.of("vector")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("openfreemap.db"))
                .set("capacity", 100 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                    .set("maxZoom", 14)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA))))
            .set("style", "hybrid"));
        map.terrain(Spec.of("terrain").set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("mapterhorn-dem.db"))
                .set("capacity", 200 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.mapterhorn.com/{z}/{x}/{y}.webp")
                    .set("minZoom", 1)
                    .set("maxZoom", 16)
                    .set("metaData", Spec.object().set("dem_encoding", "terrarium")))))
           .apply(Spec.object().set("viewDistanceFactor", 3).set("cameraClearance", 40));
        map.sky(Spec.of("sky").set("atmosphereLuminance", 2.4));
        map.fog(Spec.of("fog")
            .set("rangeStart", 2)
            .set("rangeEnd", 10)
            .set("color", 0xff2a3450)
            .set("highColor", 0xff1c2a4a)
            .set("spaceColor", 0xff070b18)
            .set("starIntensity", 0.6));
        map.light(Spec.of("light")
            .set("terrainLightingEnabled", true)
            .set("sunIntensity", 0)
            .set("ambientColor", 0xff7080b0)
            .set("ambientIntensity", 0.28));

        // First in the stack: the terrain then hides whatever sets behind a ridge.
        MassifLayer sky = map.addLayer("sky", Spec.of("celestial")).moveTo(0);
        figure = add(sky, "orion", Spec.of("arc").set("color", "#93c5fd80").set("width", 1.5 * density)
            .set("belowHorizonVisible", true));
        for (int i = 0; i < STARS.length; i++) {
            String name = (String) STARS[i][0];
            double magnitude = (Double) STARS[i][3];
            stars[i] = add(sky, "star." + name, Spec.of("sprite")
                .set("screenSize", Math.max(3, 8 - 1.6 * magnitude) * density)
                .set("color", STARS[i][4])
                .set("softness", 0.5)
                .set("clickRadius", 1.5)
                .set("metaData", Spec.object().set("name", name)));
        }
        for (int i = 0; i < NAMED.length; i++) {
            names[i] = label(sky, "name." + NAMED[i], NAMED[i], 12);
        }
        title = label(sky, "name.Orion", "ORION", 14);
        title.call("setAnchorPoint", -1, 0).close();
        title.call("setOffset", 10, 0).close();

        place();
        // Tilt 90 is straight down; below 0 the camera keeps its place and only looks up.
        map.apply(Spec.object()
            .set("freeRoamMode", "FREE_ROAM_MODE_LOOK")
            .set("tiltRange", new double[] { -90, 90 }));
        map.camera().moveTo(MATTERHORN, 12.5f, 110f, -18f);

        sky.on(MapEvents.CELESTIAL_CLICKED, new MapEvents.Handler<MapEvents.Event>() {
            @Override
            public void handle(MapEvents.Event e) {
                String name = e.get("celestialObject.metaData.name");
                if (name != null) {
                    host.caption(name);
                }
            }
        });

        host.button("Look up", new Runnable() {
            @Override
            public void run() {
                boolean up = map.camera().tilt() > -30;
                map.camera().animate(1.2f).tilt(up ? -55 : -18);
            }
        });
        host.toggle("Figures", true, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                figure.set("visible", on);
                title.set("visible", on);
                for (MassifObject name : names) {
                    name.set("visible", on);
                }
            }
        });
        host.toggle("Turn the sky", false, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                turning = on;
                step();
            }
        });
        host.caption("Orion setting over the Matterhorn. Drag to look around, tap a star.");
    }

    @Override
    public void onStop() {
        turning = false;
    }

    private MassifObject add(MassifLayer layer, String id, Spec spec) {
        MassifObject object = host.map().object("celestial", id, spec);
        layer.call("add", object.handle()).close();
        return object;
    }

    private MassifObject label(MassifLayer layer, String id, String text, float fontSize) {
        MassifObject object = add(layer, id, Spec.of("label")
            .set("text", text)
            .set("fontSize", fontSize)
            .set("textColor", "#e2e8f0")
            .set("haloColor", "#0f172acc")
            .set("haloWidth", 3));
        object.call("setOffset", 0, 8).close();
        return object;
    }

    private void place() {
        for (int i = 0; i < STARS.length; i++) {
            double[] d = at((String) STARS[i][0]);
            stars[i].call("setDirection", d[0], d[1], 0).close();
        }
        for (int i = 0; i < NAMED.length; i++) {
            double[] d = at(NAMED[i]);
            names[i].call("setDirection", d[0], d[1], 0).close();
        }
        double[] bellatrix = at("Bellatrix");
        title.call("setDirection", bellatrix[0], bellatrix[1], 0).close();
        double[] segments = new double[ORION.length * 2];
        for (int i = 0; i < ORION.length; i++) {
            double[] d = at(ORION[i]);
            segments[i * 2] = d[0];
            segments[i * 2 + 1] = d[1];
        }
        figure.call("setSegments", segments).close();
        double[] sun = direction(SUN[0], SUN[1], siderealTime);
        host.map().light().apply(Spec.object().set("sunAzimuth", sun[0]).set("sunAltitude", sun[1]));
    }

    private void step() {
        if (!turning) {
            return;
        }
        siderealTime += 0.5;
        place();
        host.postDelayed(new Runnable() {
            @Override
            public void run() {
                step();
            }
        }, 100);
    }
}
