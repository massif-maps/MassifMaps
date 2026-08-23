package com.massifmaps.MassifDemo.examples.search;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifObject;
import com.massifmaps.api.Spec;
import com.massifmaps.core.MapPos;

/**
 * Searching the vector tiles the map is already showing, and pinning what comes back.
 */
@ExampleInfo(
    id = "search-features",
    title = "Search the map's own tiles",
    description = "Every filter on a search request is an ordinary property. Runs async, because "
                + "a search fetches and decodes every tile in range.",
    section = Sections.SEARCH,
    order = 10)
public class SearchFeaturesExample extends MapExample {

    private static final MapPos CENTRE = new MapPos(5.7245, 45.1885);
    /** Degrees around the centre. A search with NO geometry scans the whole world at its zoom. */
    private static final double SPAN = 0.08;

    /** The style every result marker shares. */
    private MassifObject hitStyle;

    @Override
    public void onStart(final ExampleHost host) {
        final MassifMap map = host.map();

        map.style("alpine", Spec.of("mbvt")
            .set("project", Spec.of("project")
                .set("assets", Spec.of("zip")
                    .set("data", Spec.of("url").set("url", "assets://styles/alpine.zip")))));

        map.addLayer("basemap", Spec.of("vector")
            .set("source", Spec.of("http")
                .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                .set("maxZoom", 14)
                .set("HTTPHeaders", Spec.object().set("User-Agent", "MassifMapsExamples/1.0")))
            .set("style", "alpine"));

        map.camera().moveTo(CENTRE, 13.5f);

        // The service is built FROM THE LAYER: it takes the source and the decoder the layer is
        // already showing, so there is nothing to keep in step.
        final MassifObject service = map.object("search", "poi",
            Spec.of("vectortile").set("layer", "basemap"));
        service.set("minZoom", 14).set("maxZoom", 14).set("maxResults", 12);
        // A struct property: a JSON array, not a call that had to be added for it.
        service.set("layers", new String[] { "place", "poi", "mountain_peak" });

        hitStyle = map.elements().style("hit",
            Spec.of("marker").set("size", 20).set("color", 0xFF3A6EA5));

        // The filter is an ECMAScript regex, which has no inline flags - "(?i)" is a parse error,
        // not a case-insensitive match, and the search comes back as a failure.
        host.button("Search \"gare\"", new Runnable() {
            @Override
            public void run() {
                search(host, map, service, ".*[Gg]are.*");
            }
        });
        host.button("Search \"parc\"", new Runnable() {
            @Override
            public void run() {
                search(host, map, service, ".*[Pp]arc.*");
            }
        });
        host.caption("Grenoble. Tap a search - results are pinned as markers.");
    }

    private void search(final ExampleHost host, final MassifMap map, MassifObject service,
                        String regex) {
        map.elements().clear();
        host.caption("Searching...");

        MassifObject request = map.object("search", "request-" + regex.hashCode(),
                                          Spec.of("request"));
        request.set("regexFilter", regex);
        // Bound it, or the search walks every tile in the world at zoom 14.
        request.set("geometry", map.object("geometry", "area", Spec.of("geojson")
            .set("geojson", Spec.object()
                .set("type", "Polygon")
                .set("coordinates", new Object[] { new Object[] {
                    corner(-SPAN, -SPAN), corner(SPAN, -SPAN), corner(SPAN, SPAN),
                    corner(-SPAN, SPAN), corner(-SPAN, -SPAN) } }))));
        request.set("projection", map.object("projection", "wgs84", Spec.of("EPSG:4326")));

        // ASYNC: findFeatures fetches and decodes every tile in range - on the caller's thread
        // that is an ANR, measured at zoom 14 over this camera.
        service.callAsync("findFeatures", new MassifObject.Callback() {
            @Override
            public void onResult(MassifObject found) {
                if (found == null) {
                    host.caption("Search failed.");
                    return;
                }
                int count = (int) found.getLong("featureCount", 0);
                for (int i = 0; i < count; i++) {
                    MassifObject feature = found.call("getFeature", i);
                    try {
                        MapPos at = feature.getPos("geometry.centerPos", "EPSG:4326");
                        if (at != null) {
                            map.addMarker(Spec.of("marker")
                                .set("position", new double[] { at.getX(), at.getY() })
                                .set("style", hitStyle.id()));
                        }
                    } finally {
                        feature.close();
                    }
                }
                host.caption(count + " result" + (count == 1 ? "" : "s") + ".");
            }
        }, request.handle());
    }

    private static double[] corner(double dLon, double dLat) {
        return new double[] { CENTRE.getX() + dLon, CENTRE.getY() + dLat };
    }
}
