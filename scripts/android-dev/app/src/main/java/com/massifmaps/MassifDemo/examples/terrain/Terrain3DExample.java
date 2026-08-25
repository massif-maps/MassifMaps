package com.massifmaps.MassifDemo.examples.terrain;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.Spec;
import com.massifmaps.api.Position;

/**
 * The flagship: satellite imagery draped over 3D terrain, with roads and summits on top.
 */
@ExampleInfo(
    id = "terrain-3d",
    title = "3D terrain, hybrid",
    description = "Satellite imagery draped over an elevation mesh, with roads and summit labels "
                + "above it. One DEM source drives the mesh, the hillshade and the elevation "
                + "queries.",
    section = Sections.TERRAIN,
    order = 10)
public class Terrain3DExample extends MapExample {

    /** Identifies the app to the tile servers, which both of them ask for. */
    private static final String UA =
        "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    /**
     * Looking SOUTH at the Matterhorn from high over Zermatt.
     *
     * Composed by hand against three constraints that fight each other: a LOW tilt drops the
     * camera into the slope, a CLOSE zoom hits the terrain's camera clearance and swings the view
     * into a hillside, and a HIGH tilt buries the pyramid in the ridge behind it. This is the
     * window where all three are satisfied and the massif fills the frame.
     */
    private static final Position VIEW = new Position(7.6586, 45.9763);

    /** Open DEM tiles, terrarium-encoded, cached on disk in front of the server. */
    private static Spec dem(ExampleHost host) {
        return Spec.of("persistent-cache")
            .set("databasePath", host.cachePath("mapterhorn-dem.db"))
            .set("capacity", 200 * 1024 * 1024)
            .set("source", Spec.of("http")
                .set("url", "https://tiles.mapterhorn.com/{z}/{x}/{y}.webp")
                .set("minZoom", 1)
                .set("maxZoom", 16)
                // What picks the elevation decoder, per TILE: the source stamps its meta data
                // on every tile it loads. Without it the SDK assumes mapbox encoding, and
                // mapbox-decoding terrarium tiles gives heights in the hundreds of kilometres -
                // the terrain inflates and the camera ends up inside it.
                //
                // Written as the whole map, not as a "metaData.dem_encoding" path: a NESTED spec
                // is applied by applySpecProperties, which matches a property by its full name
                // and has no indexed-path handling, so a dotted key there is dropped.
                //
                // It stays on the HTTP source, not on the cache in front of it: a wrapper source
                // with no map of its own answers with its wrapped source's.
                .set("metaData", Spec.object().set("dem_encoding", "terrarium")));
    }

    @Override
    public void onStart(ExampleHost host) {
        final MassifMap map = host.map();

        // Imagery underneath. The {y}/{x} order is this server's, and the template substitutes by
        // name, so any order works.
        map.addLayer("satellite", Spec.of("raster")
            // Cached on disk in front of the server: imagery tiles are big, and a demo that gets
            // panned around re-fetches the same ones on every run.
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("world-imagery.db"))
                .set("capacity", 200 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://server.arcgisonline.com/ArcGIS/rest/services/"
                              + "World_Imagery/MapServer/tile/{z}/{y}/{x}")
                    .set("maxZoom", 18)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA)))));

        // Roads, place names and summits ON TOP, from a style project with no background of its
        // own - see app/src/main/style-projects/hybrid.
        map.style("hybrid", Spec.of("mbvt")
            .set("project", Spec.of("project")
                .set("assets", Spec.of("zip")
                    .set("data", Spec.of("url").set("url", "assets://styles/hybrid.zip")))));
        map.addLayer("labels", Spec.of("vector")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("openfreemap.db"))
                .set("capacity", 100 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                    .set("maxZoom", 14)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA))))
            .set("style", "hybrid"));

        // apply, not three sets: one crossing for the whole group.
        map.terrain(Spec.of("terrain").set("source", dem(host)))
           .apply(Spec.object()
               .set("exaggeration", 1)
               // How far the ground goes on: multiples of the camera-to-focus distance, so one
               // value holds at every zoom. Pair a short one with fog or the ground ends on a
               // hard edge.
               .set("viewDistanceFactor", 1.6)
               // The terrain normally holds the camera 200 m above the ground, which swings a
               // close view into the nearest hillside. Lowered so it can sit among the peaks.
               .set("cameraClearance", 40));

        // Options starts with these EMPTY, so they are BUILT here rather than written through.
        map.sky(Spec.of("sky"));
        map.fog(Spec.of("fog").set("rangeStart", 2.2).set("rangeEnd", 8));
        // The sun has to come from BEHIND the camera, or the face being looked at is the one in
        // shadow: this view is of the north side, so the light is north-west. Mid altitude, because
        // a low sun here puts the whole massif in its own shadow.
        map.light(Spec.of("light")
            .set("terrainLightingEnabled", true)
            .set("sunAzimuth", 315)
            .set("sunAltitude", 42)
            .set("shadowStrength", 0.35)
            .set("shadowSoftness", 1.5));

        // tilt 90 is straight down in this SDK, so a landscape view is a LOW tilt.
        map.camera().moveTo(VIEW, 11.5f, 180f, 33f);

        host.toggle("Terrain", true, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                // A path off the map itself, with the readable spelling: "terrain" is an alias
                // for "terrainOptions", so this is map.options().set("terrainOptions.enabled").
                map.set("terrain.enabled", on);
            }
        });
        host.toggle("Labels", true, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                map.layer("labels").visible(on);
            }
        });
        host.button("Exaggerate", new Runnable() {
            @Override
            public void run() {
                double current = map.terrain().getDouble("exaggeration", 1);
                map.terrain().set("exaggeration", current >= 2 ? 1.0 : current + 0.35);
            }
        });
        host.caption("The Matterhorn. Imagery on the mesh, roads and summits above it.");
    }
}
