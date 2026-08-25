package com.massifmaps.MassifDemo.examples.camera;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.Spec;
import com.massifmaps.api.Position;

/**
 * Flying between places, moving everything in one flight.
 */
@ExampleInfo(
    id = "fly-to",
    title = "Fly to a location",
    description = "One flight moves position, zoom, rotation and tilt together. "
                + "Four separate setters would animate independently and visibly fight.",
    section = Sections.CAMERA,
    order = 10)
public class FlyToExample extends MapExample {

    /** OSM's tile usage policy: a real app identifies itself, or the tiles come back as 403s. */
    private static final String UA = "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    /** name, lon, lat, zoom, rotation, tilt */
    private static final Object[][] PLACES = {
        { "Mont Blanc", 6.8652, 45.8326, 12.5f, 0f, 60f },
        { "Grenoble",   5.7245, 45.1885, 13.5f, 25f, 45f },
        { "Verdon",     6.3320, 43.7500, 13f, -30f, 70f },
    };

    @Override
    public void onStart(ExampleHost host) {
        final MassifMap map = host.map();

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
        map.camera().moveTo(new Position(5.7245, 45.1885), 6);

        for (final Object[] place : PLACES) {
            host.button((String) place[0], new Runnable() {
                @Override
                public void run() {
                    // animate(seconds) applies to the next move only, then resets to 0.
                    map.camera().animate(3f).moveTo(
                        new Position((Double) place[1], (Double) place[2]),
                        (Float) place[3], (Float) place[4], (Float) place[5]);
                }
            });
        }
        host.button("Stop", new Runnable() {
            @Override
            public void run() {
                map.camera().stop();
            }
        });
        host.caption("Tap a place. Each flight moves all four camera values at once.");
    }
}
