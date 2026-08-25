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
 * The smallest thing that is a map: one raster layer and a camera.
 */
@ExampleInfo(
    id = "display-a-map",
    title = "Display a map",
    description = "One raster layer from one spec, and a camera pointed at it. "
                + "The whole map is six lines.",
    section = Sections.BASICS,
    order = 10)
public class DisplayMapExample extends MapExample {

    /** OSM's tile usage policy: a real app identifies itself, or the tiles come back as 403s. */
    private static final String UA = "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    @Override
    public void onStart(ExampleHost host) {
        MassifMap map = host.map();

        // A spec describes the whole stack: the layer, and the source underneath it. Anything the
        // constructor does not take is applied as a property, so "opacity" needs no special case.
        map.addLayer("basemap", Spec.of("raster")
            // Cached on disk in front of the server: OSM's tiles are a free service run on
            // donations, and a demo that gets panned around re-fetches the same ones every run.
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("osm-raster.db"))
                .set("capacity", 100 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tile.openstreetmap.org/{z}/{x}/{y}.png")
                    .set("maxZoom", 19)
                    // OSM's tile policy REQUIRES an identifying User-Agent; without one the server
                    // answers 403 and every tile comes back as an error image.
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA)))));

        // The same property two ways. The string is the API; ApiNames is the GENERATED constant
        // set, which completes in an editor and carries the value's type - passing a boolean to
        // OPACITY is a compile error rather than a warning in the log.
        map.layer("basemap").set("opacity", 1.0);
        map.layer("basemap").set(ApiNames.OPACITY, 1.0);

        // Positions are lon/lat: the map view was set up with EPSG:4326 as its base projection.
        map.camera().moveTo(new Position(6.8652, 45.8326), 11);

        host.caption("Mont Blanc, from OpenStreetMap raster tiles.");
    }
}
