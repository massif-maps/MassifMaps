package com.massifmaps.MassifDemo.examples.sources;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifSource;
import com.massifmaps.api.Spec;
import com.massifmaps.api.Position;

/**
 * A line coloured along its length: the geojson source tags every tile piece of a line with the part
 * of the route it covers, and the style turns that into a colour ramp.
 */
@ExampleInfo(
    id = "line-gradient",
    title = "Colour a line along its length",
    description = "With lineMetrics on, a line's colour is a ramp over how far along it you are, "
                + "set per route by a string on the feature, smooth or in hard steps.",
    section = Sections.SOURCES,
    order = 15)
public class LineGradientExample extends MapExample {

    /** A tile server wants to know who is asking: a real app identifies itself. */
    private static final String UA = "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    /** A stretch of the Tour du Mont Blanc, coloured smoothly, and a spur coloured in steps. */
    private static final String ROUTE =
        "{\"type\":\"FeatureCollection\",\"features\":["
        + "{\"type\":\"Feature\",\"properties\":{\"gradient\":\"0:#2ecc71,0.5:#f1c40f,1:#e74c3c\"},\"geometry\":"
        + "{\"type\":\"LineString\",\"coordinates\":["
        + "[6.8694,45.9237],[6.8290,45.9081],[6.8027,45.8862],[6.7861,45.8548],"
        + "[6.8062,45.8281],[6.8556,45.8090],[6.9016,45.7992],[6.9584,45.8138],"
        + "[6.9821,45.8452],[6.9612,45.8813],[6.9163,45.9096],[6.8694,45.9237]]}},"
        + "{\"type\":\"Feature\",\"properties\":{\"gradient\":\"step;0:#3b82f6,0.33:#8b5cf6,0.66:#ec4899\"},\"geometry\":"
        + "{\"type\":\"LineString\",\"coordinates\":["
        + "[6.7400,45.9300],[6.7800,45.9500],[6.8300,45.9600],[6.8800,45.9650]]}}]}";

    private static final String STYLE = String.join("\n",
        "#tour {",
        "  line-gradient: [gradient];",
        "  line-width: linear([view::zoom], (8, 3), (14, 8));",
        "  line-join: round;",
        "  line-cap: round;",
        "}");

    @Override
    public void onStart(ExampleHost host) {
        MassifMap map = host.map();

        // The Massif streets style (styles/massif/carto, bundled by the build as assets/styles/massif.zip)
        // over OpenFreeMap's vector tiles, cached on disk: a free service a demo would re-fetch every run.
        map.addLayer("basemap", Spec.of("vector")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("openfreemap.db"))
                .set("capacity", 100 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                    .set("maxZoom", 14)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA))))
            .set("style", Spec.of("mbvt").set("project", Spec.of("project")
                .set("assets", Spec.of("zip").set("data", Spec.of("url").set("url", "assets://styles/massif.zip")))
                .set("name", "streets"))));

        // The source re-tiles whatever it is given, so an update is one call rather than a rebuild.
        // lineMetrics is what lets the style know where along a route each tile piece sits.
        MassifSource tour = map.source("tour-data", Spec.of("geojson").set("maxZoom", 14).set("lineMetrics", true));
        int layer = tour.createLayer("tour");
        tour.setLayerGeoJSON(layer, ROUTE);

        map.addLayer("tour", Spec.of("vector")
            .set("source", "tour-data")
            .set("style", Spec.of("mbvt")
                .set("cartocss", Spec.of("cartocss").set("css", STYLE))));

        map.camera().moveTo(new Position(6.862, 45.886), 10.2f);
        host.caption("A ramp along each route, smooth or in steps, from a string on the feature.");
    }
}
