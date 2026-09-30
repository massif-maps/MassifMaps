package com.massifmaps.MassifDemo.examples.basics;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.ApiNames;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.Spec;
import com.massifmaps.api.Position;

/**
 * The smallest thing that is a map: one layer with the Massif style, and a camera.
 */
@ExampleInfo(
    id = "display-a-map",
    title = "Display a map",
    description = "One vector layer from one spec - the source, and the Massif streets style over it - "
                + "and a camera pointed at it.",
    section = Sections.BASICS,
    order = 10)
public class DisplayMapExample extends MapExample {

    /** A tile server wants to know who is asking: a real app identifies itself. */
    private static final String UA = "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    @Override
    public void onStart(ExampleHost host) {
        MassifMap map = host.map();

        // A spec describes the whole stack: the layer, the source under it and the style over it - the
        // Massif streets style, bundled by the build as assets/styles/massif.zip from styles/massif/carto.
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

        // The same property two ways. The string is the API; ApiNames is the GENERATED constant
        // set, which completes in an editor and carries the value's type - passing a boolean to
        // OPACITY is a compile error rather than a warning in the log.
        map.layer("basemap").set("opacity", 1.0);
        map.layer("basemap").set(ApiNames.OPACITY, 1.0);

        // Positions are lon/lat: the map view was set up with EPSG:4326 as its base projection.
        map.camera().moveTo(new Position(6.8652, 45.8326), 11);

        host.caption("Mont Blanc, drawn by the Massif streets style over OpenFreeMap vector tiles.");
    }
}
