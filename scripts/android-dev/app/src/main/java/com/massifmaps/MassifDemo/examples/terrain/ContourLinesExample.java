package com.massifmaps.MassifDemo.examples.terrain;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifSource;
import com.massifmaps.api.Position;
import com.massifmaps.api.Spec;

@ExampleInfo(
    id = "contour-lines",
    title = "On-the-fly contour lines",
    description = "Contour lines traced from an elevation source at runtime, no contour tileset "
                + "needed, and styled with CartoCSS like any vector layer.",
    section = Sections.TERRAIN,
    order = 51)
public class ContourLinesExample extends MapExample {

    private static final String UA = "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    // `div` is the largest round divisor of the elevation, so `div >= 100` picks the index lines.
    private static final String STYLE = String.join("\n",
        "@medium: 'ios:Helvetica Neue Medium, Roboto Medium, sans-serif Medium';",
        "#contour {",
        "  line-color: #a0663a;",
        "  line-opacity: 0.7;",
        "  line-width: 0.6;",
        "  [div >= 100] {",
        "    line-width: 1.3;",
        "    text-name: [ele] + ' m';",
        "    text-face-name: @medium;",
        "    text-size: 10;",
        "    text-fill: #7a4b28;",
        "    text-halo-fill: #ffffffcc;",
        "    text-halo-radius: 1.5;",
        "    text-placement: line;",
        "  }",
        "}");

    @Override
    public void onStart(final ExampleHost host) {
        MassifMap map = host.map();

        map.style("massif", Spec.of("mbvt")
            .set("project", Spec.of("project")
                .set("assets", Spec.of("zip")
                    .set("data", Spec.of("url").set("url", "assets://styles/massif.zip")))
                .set("name", "streets")));
        map.addLayer("basemap", Spec.of("vector")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("openfreemap.db"))
                .set("capacity", 100 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                    .set("maxZoom", 14)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA))))
            .set("style", "massif"));
        // The contour source wraps the DEM: the tiles are fetched and decoded once, for both.
        final MassifSource contours = map.source("contours", Spec.of("contour")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("mapterhorn-dem.db"))
                .set("capacity", 200 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.mapterhorn.com/{z}/{x}/{y}.webp")
                    .set("minZoom", 1)
                    .set("maxZoom", 16)
                    .set("metaData", Spec.object().set("dem_encoding", "terrarium"))))
            .set("baseInterval", 20));
        map.addLayer("contours", Spec.of("vector")
            .set("source", "contours")
            .set("style", Spec.of("mbvt").set("cartocss", Spec.of("cartocss").set("css", STYLE))));
        map.camera().moveTo(new Position(5.73, 45.215), 13.5f);

        for (final int interval : new int[] { 10, 20, 50 }) {
            // A live property: the source traces again, the layer is not rebuilt.
            host.button(interval + " m", new Runnable() {
                @Override
                public void run() {
                    contours.set("baseInterval", (double) interval);
                    host.caption("Every " + interval + " m, multiplied per zoom band.");
                }
            });
        }
        host.caption("Contours over the Bastille and the Chartreuse, traced from a terrarium DEM.");
    }
}
