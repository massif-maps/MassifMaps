package com.massifmaps.MassifDemo.examples.terrain;

import com.massifmaps.MassifDemo.customlayer.CustomHeatmap;
import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifLayer;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifObject;
import com.massifmaps.api.Position;
import com.massifmaps.api.Spec;

/**
 * A layer the app draws itself, in C: a heatmap laid on the ground, and its colour legend.
 *
 * The SDK exports no C++ symbols to subclass a layer with, so the drawing is a plain GL ES 3.0
 * function registered over the C ABI (src/main/cpp/custom_layer_demo.c, mm_layer_create_custom). This
 * file is the map side: it builds the map, reads the ground height under each cell of the grid
 * from the DEM, hands the grid to the layer and switches between flat and 3D.
 */
@ExampleInfo(
    id = "custom-layer-heatmap",
    title = "Custom layer: heatmap",
    description = "A heatmap drawn by the app's own GL code - a custom layer registered over the C "
                + "ABI - on flat ground and on 3D terrain. The layer is depth tested, so a ridge in "
                + "front hides it, and a tap finds the cell under the finger.",
    section = Sections.TERRAIN,
    order = 40)
public class CustomLayerHeatmapExample extends MapExample {

    private static final String UA =
        "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    /** The Chartreuse and Belledonne ridges above Grenoble: relief on three sides of the grid. */
    private static final Position CENTER = new Position(5.76, 45.24);
    private static final float ZOOM = 12.2f;
    /** tilt 90 is straight down in this SDK, so a landscape view is a LOW tilt. */
    private static final float TILT_3D = 35f;
    private static final float TILT_2D = 90f;

    private MassifMap map;
    private double[] heights;

    @Override
    public void onStart(final ExampleHost host) {
        map = host.map();

        // One DEM behind the mesh, the hillshade and the height read.
        map.source("dem", Spec.of("persistent-cache")
            .set("databasePath", host.cachePath("mapterhorn-dem.db"))
            .set("capacity", 200 * 1024 * 1024)
            .set("source", Spec.of("http")
                .set("url", "https://tiles.mapterhorn.com/{z}/{x}/{y}.webp")
                .set("minZoom", 1)
                .set("maxZoom", 16)
                .set("metaData", Spec.object().set("dem_encoding", "terrarium"))));

        map.addLayer("satellite", Spec.of("raster")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("world-imagery.db"))
                .set("capacity", 200 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://server.arcgisonline.com/ArcGIS/rest/services/"
                              + "World_Imagery/MapServer/tile/{z}/{y}/{x}")
                    .set("maxZoom", 18)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA)))));
        map.addLayer("hillshade", Spec.of("hillshade").set("source", "dem")).opacity(0.45f);

        map.terrain(Spec.of("terrain").set("source", "dem"))
           .apply(Spec.object().set("exaggeration", 1).set("viewDistanceFactor", 1.6));
        map.sky(Spec.of("sky"));
        map.fog(Spec.of("fog").set("rangeStart", 2.2).set("rangeEnd", 8));

        map.camera().moveTo(CENTER, ZOOM, 0f, host.option("tilt", TILT_3D));

        // The layer draws what it is given: the ground height under each cell comes from the DEM,
        // through the hillshade layer, which reads it. A worker thread already, so blocking is fine.
        final MassifLayer heat = CustomHeatmap.create("heatmap", true);
        final MassifLayer legend = CustomHeatmap.create("heatmap-legend", false);
        if (heat == null || legend == null) {
            host.caption("The custom layer did not register.");
            return;
        }
        heights = readGround(CustomHeatmap.gridPositions(CENTER.lng, CENTER.lat));
        CustomHeatmap.setHeatmap(CENTER.lng, CENTER.lat, heights);
        map.add(heat);
        map.add(legend);

        host.toggle("3D", true, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                // Flat ground has no heights to lay the grid on, so it goes flat with it.
                CustomHeatmap.setHeatmap(CENTER.lng, CENTER.lat, on ? heights : null);
                map.terrain().set("flattened", !on);
                map.camera().animate(1.2f).tilt(on ? TILT_3D : TILT_2D);
            }
        });
        final boolean drape = host.option("drape", 1f) != 0;
        CustomHeatmap.setDrape(drape);
        host.toggle("Drape", drape, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                CustomHeatmap.setDrape(on);
            }
        });
        host.toggle("Heatmap", true, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                heat.visible(on);
                legend.visible(on);
            }
        });
        host.slider("Opacity", 0f, 1f, 1f, new ExampleHost.OnValue() {
            @Override
            public void onValue(float value) {
                heat.opacity(value);
            }
        });
        host.caption("Tilt the map, or switch 3D off. Drape off shows the ground cutting through the heatmap. Tap it to dim it.");
    }

    /** Metres at each position, 0 where the DEM has nothing. */
    private double[] readGround(double[] positions) {
        double[][] list = new double[positions.length / 2][];
        for (int k = 0; k < list.length; k++) {
            list[k] = new double[] { positions[2 * k], positions[2 * k + 1] };
        }
        MassifObject result = map.layer("hillshade").call("getElevations", (Object) list);
        try {
            return result.doubles();
        } finally {
            result.close();
        }
    }
}
