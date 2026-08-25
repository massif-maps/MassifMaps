package com.massifmaps.MassifDemo.examples.terrain;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.Position;
import com.massifmaps.api.Spec;

/**
 * Switching between a flat map and a 3D view as ONE animation: the camera flies while the terrain
 * rises under it.
 */
@ExampleInfo(
    id = "terrain-2d-3d",
    title = "2D / 3D switch",
    description = "One animation moves the camera and raises the terrain together. Going back, the "
                + "top-down view re-centres on where the camera was standing, not on what it was "
                + "looking at.",
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
    private static final float FLIGHT_SECONDS = 2.5f;
    /** How often the terrain height is stepped to follow the flight. */
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
    private boolean animating = false;

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

        // Built flat: terrain off, and the height at 0 so the first rise starts from nothing.
        map.terrain(Spec.of("terrain").set("source", dem(host)))
           .apply(Spec.object()
               .set("enabled", false)
               .set("exaggeration", 0)
               // This example drives the switch itself, so the automatic one has to be out of the
               // way - otherwise it flattens the map again the moment the tilt passes 88.
               .set("autoFlattenTilt", 0)
               .set("autoFlattenParallax", 0)
               .set("cameraClearance", 40));
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
        host.caption("Flat, top-down. Tap to rise into the terrain.");
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
        if (animating) {
            return;
        }
        animating = true;
        if (in3D) {
            flattenTo2D();
        } else {
            riseTo3D();
        }
    }

    private void riseTo3D() {
        // The flight goes FIRST. Turning the terrain on clears every tile cache, and that re-decode
        // landing on the flight's frame zero starved it of frames - the first switch jumped while
        // every later one animated, because only the first one is cold. Started a tick later it
        // lands during the rise instead, where the exaggeration is still near 0 and a flat-decoded
        // tile renders exactly like the 2D map. The flight is never made to wait
        // (https://github.com/massif-maps/MassifMaps/issues/177 removes the re-decode entirely).
        map.camera().animate(FLIGHT_SECONDS).moveTo(SUMMIT, ZOOM, ROTATION, TILT_3D);
        host.postDelayed(new Runnable() {
            @Override
            public void run() {
                map.terrain().set("enabled", true);
            }
        }, TICK_MS);
        // The terrain follows the FLIGHT rather than a clock of its own, so the two cannot drift
        // apart if the flight is interrupted or the frame rate drops.
        ramp(true);
        host.caption("Rising. The terrain follows the flight, not a separate clock.");
    }

    private void flattenTo2D() {
        // Where the camera IS, not what it is looking at: at tilt 20 the focus is kilometres out in
        // front, so re-centring on it would jump the map forward. This is the viewpoint.
        Position eye = map.camera().eyePosition();
        map.camera().animate(FLIGHT_SECONDS).moveTo(eye, ZOOM, ROTATION, TILT_2D);
        ramp(false);
        host.caption("Back down, centred on where the camera was standing.");
    }

    /** Steps the terrain height with the flight's own progress, and settles when it lands. */
    private void ramp(final boolean rising) {
        host.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (map.camera().isMoving()) {
                    float progress = map.camera().progress();
                    map.terrain().set("exaggeration", rising ? progress : 1 - progress);
                    ramp(rising);
                    return;
                }
                map.terrain().set("exaggeration", rising ? 1 : 0);
                if (!rising) {
                    // Only now, with the map already flat: flipping the flag re-decodes every tile,
                    // and at exaggeration 0 there is nothing of that to see.
                    map.terrain().set("enabled", false);
                }
                in3D = rising;
                animating = false;
                host.caption(rising ? "3D. Tap to go back to the flat map."
                                    : "Flat, top-down. Tap to rise into the terrain.");
            }
        }, TICK_MS);
    }
}
