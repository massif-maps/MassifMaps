package com.massifmaps.MassifDemo.examples.terrain;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.Position;
import com.massifmaps.api.Spec;

/**
 * The 2D/3D switch, and every way of driving it: the SDK's own animation, a tilt gesture, and the
 * app's own clock for an exact match to a camera flight.
 */
@ExampleInfo(
    id = "terrain-2d-3d",
    title = "2D / 3D switch",
    description = "One flag switches the map between flat and 3D terrain. Full switch decides "
                + "whether a flat map still pays for 3D, auto by tilt lets a tilt gesture do the "
                + "switching, and match flight drives the terrain off the camera's own clock.",
    section = Sections.TERRAIN,
    order = 15)
public class Switch2D3DExample extends MapExample {

    private static final String UA =
        "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    /** What the 3D view looks AT. The viewpoint it is seen from is derived - see frameFlatStart. */
    private static final Position SUMMIT = new Position(7.6586, 45.9763);

    private static final float ZOOM = 12.5f;
    /** One rotation for both states: north up flat, looking north tilted. A switch that also spun
     *  the map 180 degrees made it impossible to tell where you had come out. */
    private static final float ROTATION = 0f;
    /** tilt 90 is straight down in this SDK, so 2D is 90 and a landscape view is a LOW tilt. */
    private static final float TILT_2D = 90f;
    private static final float TILT_3D = 20f;
    /** The tilt the auto rule switches at, and its default. */
    private static final float AUTO_TILT = 88f;
    /** How often the matched ramp samples the flight. */
    private static final long TICK_MS = 32;

    private static Spec dem(ExampleHost host) {
        return Spec.of("persistent-cache")
            // Same database as terrain-3d: the two examples warm each other's cache.
            .set("databasePath", host.cachePath("mapterhorn-dem.db"))
            .set("capacity", 200 * 1024 * 1024)
            .set("source", Spec.of("http")
                .set("url", "https://tiles.mapterhorn.com/{z}/{x}/{y}.webp")
                .set("minZoom", 1)
                .set("maxZoom", 16)
                // Picks the elevation decoder per TILE. Without it the SDK assumes mapbox encoding,
                // and mapbox-decoding terrarium tiles gives heights in the hundreds of kilometres.
                .set("metaData", Spec.object().set("dem_encoding", "terrarium")));
    }

    private ExampleHost host;
    private MassifMap map;
    private boolean in3D = false;
    private boolean autoByTilt = false;
    private boolean matchFlight = false;
    private float seconds = 2.5f;

    @Override
    public void onStart(ExampleHost host) {
        this.host = host;
        this.map = host.map();

        map.addLayer("basemap", Spec.of("raster")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("osm-raster.db"))
                .set("capacity", 100 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tile.openstreetmap.org/{z}/{x}/{y}.png")
                    .set("maxZoom", 19)
                    // OSM's tile policy REQUIRES an identifying User-Agent, or every tile is a 403.
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA)))));

        map.terrain(Spec.of("terrain").set("source", dem(host)))
           .apply(Spec.object()
               // Configured and left on. The switch is `flattened`, and it opens flat - set BEFORE
               // any layer decodes, so not one tile is built for a 3D the map has not shown.
               .set("enabled", true)
               .set("flattened", true)
               // The whole way: a flat map decodes and culls as if no terrain were attached. RENDER
               // (the default) only stops the terrain passes and keeps 3D's triangles.
               .set("flattenMode", "TERRAIN_FLATTEN_MODE_FULL")
               // Off to start with, so the button below is the only thing switching.
               .set("autoFlattenTilt", 0)
               .set("autoFlattenParallax", 0)
               .set("cameraClearance", 40));
        applySeconds(seconds);
        map.sky(Spec.of("sky"));
        map.fog(Spec.of("fog").set("rangeStart", 2.2).set("rangeEnd", 8));
        // The sun comes from BEHIND the camera or the face being looked at is the one in shadow.
        // This view is of the SOUTH side, so the light is south.
        map.light(Spec.of("light").set("terrainLightingEnabled", true)
                                  .set("sunAzimuth", 170).set("sunAltitude", 42));

        frameFlatStart();

        host.button("2D / 3D", new Runnable() {
            @Override
            public void run() {
                toggle();
            }
        });
        host.slider("seconds", 0f, 6f, seconds, new ExampleHost.OnValue() {
            @Override
            public void onValue(float value) {
                seconds = value;
                applySeconds(value);
            }
        });
        host.toggle("Match flight", false, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                matchFlight = on;
                host.caption(on
                    ? "Matched: the terrain reads the flight's own progress, so the two cannot drift."
                    : "Timed: two clocks of the same length. Close, but not the same clock.");
            }
        });
        host.toggle("Full switch", true, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                map.terrain().set("flattenMode",
                    on ? "TERRAIN_FLATTEN_MODE_FULL" : "TERRAIN_FLATTEN_MODE_RENDER");
                host.caption(on ? "FULL: flat costs nothing, each switch re-decodes the visible tiles."
                                : "RENDER: switching is free, but flat still carries 3D's triangles.");
            }
        });
        host.toggle("Auto by tilt", false, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                autoByTilt = on;
                map.terrain().set("autoFlattenTilt", on ? AUTO_TILT : 0);
                host.caption(on ? "Auto on: tilt with two fingers and it switches itself. The button "
                                + "still leads - the rule only fires when the tilt CROSSES 88."
                                : "Auto off: only the button switches.");
            }
        });
        host.caption(flatCaption());
    }

    /** One number for both animations, which is what makes them the same length. */
    private void applySeconds(float value) {
        map.terrain().apply(Spec.object()
            .set("autoFlattenDuration", value)
            // Timed apart from the sinking one: this is the direction that waited for its tiles.
            .set("autoFlattenRiseDuration", value));
    }

    /**
     * Opens the flat map exactly where a round trip through 3D lands, which is what makes the first
     * flight identical to every later one.
     *
     * That place cannot be a constant: the camera stands cameraDistance * cos(tilt) from its focus,
     * and cameraDistance comes from the viewport, so it differs per screen. So put the camera where
     * 3D would put it, ask where that left it standing, and drop to top-down there.
     */
    private void frameFlatStart() {
        map.camera().moveTo(SUMMIT, ZOOM, ROTATION, TILT_3D);
        map.camera().moveTo(map.camera().eyePosition(), ZOOM, ROTATION, TILT_2D);
    }

    private void toggle() {
        if (map.camera().isMoving()) {
            host.caption("Still flying - let it land first.");
            return;
        }
        // Read the SDK's state rather than count button presses. With auto by tilt on, the RULE
        // owns the state and a local flag drifts out of step with it - and then the button flies to
        // the tilt the map is already at, the rule never crosses its threshold, and nothing moves.
        in3D = map.terrain().getBool("flattened", true);
        if (matchFlight) {
            matched();
        } else {
            timed();
        }
    }

    /**
     * The SDK's own animation: ask for the state, and it ramps over autoFlattenDuration. Two timers
     * of the same length - which is close, and is all most apps need.
     */
    private void timed() {
        fly();
        // Written even with auto by tilt on: the rule fires on a THRESHOLD CROSSING, not every
        // frame, so it leaves an explicit ask alone and the terrain moves with the flight instead
        // of waiting for the tilt to reach 88.
        map.terrain().set("flattened", !in3D);
        host.caption(in3D ? riseCaption() : flatCaption());
    }

    /**
     * The app's own clock: feed the terrain the FLIGHT's progress, so the two cannot drift apart
     * even if the frame rate drops or the flight is interrupted.
     */
    private void matched() {
        if (!in3D) {
            fly();          // sinking has nothing to wait for
            rampWithFlight();
            host.caption("Sinking on the flight's own clock.");
            return;
        }
        // Rising does. Ask for 3D so its tiles start loading, and let the flight go only once the
        // switch stops holding the ground flat - driving the ratio up before then would be held
        // anyway, and the animation would start with a jump.
        map.terrain().set("flattened", false);
        host.caption("Loading the tiles 3D needs before the flight starts.");
        waitForTiles();
    }

    private void waitForTiles() {
        host.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (map.terrain().getBool("switching", false)) {
                    waitForTiles();
                    return;
                }
                fly();
                rampWithFlight();
                host.caption("Rising on the flight's own clock.");
            }
        }, TICK_MS);
    }

    private void fly() {
        // Where the camera IS, not what it is looking at: at tilt 20 the focus is kilometres out in
        // front, so re-centring on it would jump the map forward.
        Position target = in3D ? SUMMIT : map.camera().eyePosition();
        map.camera().animate(seconds).moveTo(target, ZOOM, ROTATION, in3D ? TILT_3D : TILT_2D);
    }

    /** Writing flattenRatio takes the ramp off the SDK's timer and puts it on the flight's. */
    private void rampWithFlight() {
        host.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (map.camera().isMoving()) {
                    float progress = map.camera().progress();
                    map.terrain().set("flattenRatio", in3D ? 1 - progress : progress);
                    rampWithFlight();
                    return;
                }
                map.terrain().set("flattenRatio", in3D ? 0 : 1);
                // Hand the ratio back, or the switch stays MANUAL - which also keeps auto-flattening
                // suspended, and a tilt gesture would then do nothing.
                map.terrain().set("flattened", !in3D);
                host.caption(in3D ? riseCaption() : flatCaption());
            }
        }, TICK_MS);
    }

    private String flatCaption() {
        return autoByTilt ? "Flat. Tilt, or tap, to rise into the terrain."
                          : "Flat, top-down. Tap to rise into the terrain.";
    }

    private String riseCaption() {
        return autoByTilt ? "3D - the tilt asked for it, not the button."
                          : "3D. The SDK waited for its tiles before lifting the ground.";
    }
}
