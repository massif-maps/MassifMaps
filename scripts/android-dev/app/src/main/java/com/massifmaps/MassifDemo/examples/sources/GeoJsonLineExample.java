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
 * A GeoJSON document served AS vector tiles, so it goes through the ordinary style and renderer.
 */
@ExampleInfo(
    id = "geojson-line",
    title = "Add a GeoJSON line",
    description = "A \"geojson\" source re-tiles the document on the fly, so the features are "
                + "styled with CartoCSS and drawn by the same renderer as a tile server's.",
    section = Sections.SOURCES,
    order = 10)
public class GeoJsonLineExample extends MapExample {

    /** OSM's tile usage policy: a real app identifies itself, or the tiles come back as 403s. */
    private static final String UA = "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    /** A stretch of the Tour du Mont Blanc, plus the huts along it. */
    private static final String ROUTE =
        "{\"type\":\"FeatureCollection\",\"features\":["
        + "{\"type\":\"Feature\",\"properties\":{\"kind\":\"trail\"},\"geometry\":"
        + "{\"type\":\"LineString\",\"coordinates\":["
        + "[6.8694,45.9237],[6.8290,45.9081],[6.8027,45.8862],[6.7861,45.8548],"
        + "[6.8062,45.8281],[6.8556,45.8090],[6.9016,45.7992],[6.9584,45.8138],"
        + "[6.9821,45.8452],[6.9612,45.8813],[6.9163,45.9096],[6.8694,45.9237]]}},"
        + "{\"type\":\"Feature\",\"properties\":{\"kind\":\"hut\",\"name\":\"Lac Blanc\"},"
        + "\"geometry\":{\"type\":\"Point\",\"coordinates\":[6.8290,45.9081]}},"
        + "{\"type\":\"Feature\",\"properties\":{\"kind\":\"hut\",\"name\":\"Bonhomme\"},"
        + "\"geometry\":{\"type\":\"Point\",\"coordinates\":[6.8062,45.8281]}},"
        + "{\"type\":\"Feature\",\"properties\":{\"kind\":\"hut\",\"name\":\"Elisabetta\"},"
        + "\"geometry\":{\"type\":\"Point\",\"coordinates\":[6.9584,45.8138]}}]}";

    private static final String STYLE = String.join("\n",
        "#tour {",
        "  line-color: #E5484D;",
        "  line-width: linear([view::zoom], (8, 2), (14, 6));",
        "  line-join: round;",
        "  line-cap: round;",
        "}",
        "#tour['kind'='hut'] {",
        "  marker-fill: #FFFFFF;",
        "  marker-line-color: #E5484D;",
        "  marker-line-width: 2;",
        "  marker-width: 9;",
        "}");

    @Override
    public void onStart(ExampleHost host) {
        MassifMap map = host.map();

        map.addLayer("basemap", Spec.of("raster")
            .set("source", Spec.of("http")
                .set("url", "https://tile.openstreetmap.org/{z}/{x}/{y}.png")
                .set("maxZoom", 19)
                // OSM's tile policy REQUIRES an identifying User-Agent; without one the server
                // answers 403 and every tile comes back as an error image.
                .set("HTTPHeaders", Spec.object().set("User-Agent", UA))));

        // The source re-tiles whatever it is given, so an update is one call rather than a rebuild.
        MassifSource tour = map.source("tour-data", Spec.of("geojson").set("maxZoom", 14));
        int layer = tour.createLayer("tour");
        tour.setLayerGeoJSON(layer, ROUTE);

        map.addLayer("tour", Spec.of("vector")
            .set("source", "tour-data")
            .set("style", Spec.of("mbvt")
                .set("cartocss", Spec.of("cartocss").set("css", STYLE))));

        map.camera().moveTo(new Position(6.882, 45.866), 10.4f);
        host.caption("One FeatureCollection, tiled on the fly and styled with CartoCSS.");
    }
}
