package com.massifmaps.MassifDemo.examples.terrain;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifLayer;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifSource;
import com.massifmaps.api.Position;
import com.massifmaps.api.Spec;

/**
 * The 2D/3D switch on the map an app actually ships: a composite layer carrying Massif outdoor, a
 * hillshade and contours over one shared DEM, with shadows on top. Everything that costs something
 * when the ground moves is in the frame at once, which is the point - the switch is cheap on a
 * raster basemap with a toy style, and that is not what an app sees.
 */
@ExampleInfo(
    id = "terrain-2d-3d",
    title = "2D / 3D switch",
    description = "One flag switches a composite layer - Massif outdoor, hillshade and contours over "
                + "one shared DEM - between flat and 3D terrain. Full switch decides whether a "
                + "flat map still pays for 3D, auto by tilt lets a tilt gesture do the switching, "
                + "match flight drives the terrain off the camera's own clock, and shadows show what "
                + "the switch costs with a shadow pass in the frame.",
    section = Sections.TERRAIN,
    order = 15)
public class Switch2D3DExample extends MapExample {

    private static final String UA =
        "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    /** CompositeSourceType, as the facade takes it. RASTER is 0 and is not used here. */
    private static final long SOURCE_HILLSHADE = 1;
    private static final long SOURCE_VECTOR = 2;

    /** Where the map opens; later switches start from wherever the user has got to. */
    private static final Position SUMMIT = new Position(7.6586, 45.9763);

    /** The opening zoom only. A switch keeps the zoom it is given - see fly(). */
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
    /** Asked while rising: below 1, so the SDK holds the ground flat until the 3D tiles are in. */
    private static final float HOLD_RATIO = 0.999f;

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

        // ONE DEM behind all three consumers - the terrain mesh, the hillshade slot and the contour
        // generator - so a tile is fetched, cached and decoded once. Given an id because the specs
        // below reference it by name.
        MassifSource dem = map.source("dem", dem(host));
        // Contours are GENERATED from that DEM, as vector tiles carrying 'ele' and 'div'. Every 20 m:
        // Massif draws each line under an index, and 10 m turns a steep face into a brown mesh.
        MassifSource contours = map.source("contours",
            Spec.of("contour").set("source", "dem").set("baseInterval", 20));

        // ONE project for every Massif variant (styles/massif/carto, bundled as assets/styles/massif.zip).
        map.style("massif", Spec.of("mbvt")
            .set("project", Spec.of("project")
                .set("assets", Spec.of("zip")
                    .set("data", Spec.of("url").set("url", "assets://styles/massif.zip")))
                .set("name", "outdoor")));

        // A composite layer weaves the two sources into the STYLE's own layer order: Massif lists
        // "hillshade" under the contour lines, the lines under the roads, and the contour labels again
        // among the names - each entry drawn at its own depth.
        MassifLayer base = map.addLayer("basemap", Spec.of("composite-vector")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("openfreemap.db"))
                .set("capacity", 100 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                    .set("maxZoom", 14)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA))))
            .set("style", "massif"));
        // The slot NAME is the style layer name. The style's `#hillshade` rule carries the relief the
        // variant was tuned with, and gives it outdoor and topo only.
        base.call("addExternalDataSource", "hillshade", dem.handle(), SOURCE_HILLSHADE);
        base.call("addExternalDataSource", "contour", contours.handle(), SOURCE_VECTOR);

        map.terrain(Spec.of("terrain").set("source", "dem"))
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
                                  .set("sunAzimuth", 170).set("sunAltitude", 42)
                                  .set("shadowStrength", 0.0)
                                  .set("shadowSoftness", 1.5));

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
        host.toggle("Shadows", false, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                // 1 is the physically correct strength; 0 is off, and skips the shadow pass.
                map.light().set("shadowStrength", on ? 1.0 : 0.0);
                host.caption(on ? "Shadows on: watch them flatten WITH the ground, not after it - "
                                + "the cascades follow the same ratio the terrain is ramping."
                                : "Shadows off: no shadow pass, so the switch is as cheap as it gets.");
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
     * even if the frame rate drops or the flight is interrupted. Both ways fly at once.
     */
    private void matched() {
        fly();
        rampWithFlight(Float.NaN);
        host.caption(in3D ? "Flying; the ground rises once its tiles are in." : "Sinking on the flight's own clock.");
    }

    private void fly() {
        // Sinking centres on the eye's ground point (at tilt 20 the focus is km ahead); rising is the
        // inverse, or every round trip drifts back by that offset.
        Position target = in3D ? focusPutting3DCameraOver(map.camera().position())
                               : map.camera().eyePosition();
        // Zoom carries across so tilt alone sets the eye height and a round trip returns where it started.
        map.camera().animate(seconds).moveTo(target, map.camera().zoom(), ROTATION, in3D ? TILT_3D : TILT_2D);
    }

    /**
     * Focus whose 3D camera stands over pos. The offset depends on viewport, zoom and tilt, so probe
     * it; both moves are instant in one callback, so no frame is drawn in between.
     */
    private Position focusPutting3DCameraOver(Position pos) {
        Position was = map.camera().position();
        float wasZoom = map.camera().zoom();
        float wasRotation = map.camera().rotation();
        float wasTilt = map.camera().tilt();
        map.camera().moveTo(pos, wasZoom, ROTATION, TILT_3D);
        Position eye = map.camera().eyePosition();
        map.camera().moveTo(was, wasZoom, wasRotation, wasTilt);
        return new Position(2 * pos.lng - eye.lng, 2 * pos.lat - eye.lat);
    }

    /**
     * Writing flattenRatio takes the ramp off the SDK's timer and puts it on the flight's. Rising, the
     * SDK holds the asked ratio flat until the 3D tiles are in; the rise then spans what is left.
     */
    private void rampWithFlight(final float riseStart) {
        host.postDelayed(new Runnable() {
            @Override
            public void run() {
                float start = riseStart;
                if (map.camera().isMoving()) {
                    float progress = map.camera().progress();
                    if (!in3D) {
                        map.terrain().set("flattenRatio", progress);
                    } else if (Float.isNaN(start)) {
                        map.terrain().set("flattenRatio", HOLD_RATIO);
                        if (map.terrain().getDouble("flattenRatio", 1) < 1) {
                            start = progress;
                        }
                    } else {
                        map.terrain().set("flattenRatio", 1 - (progress - start) / Math.max(1e-3f, 1 - start));
                    }
                    rampWithFlight(start);
                    return;
                }
                // Landed still held: the SDK's own clock finishes the rise once the tiles are in.
                if (!in3D || !Float.isNaN(start)) {
                    map.terrain().set("flattenRatio", in3D ? 0 : 1);
                }
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
