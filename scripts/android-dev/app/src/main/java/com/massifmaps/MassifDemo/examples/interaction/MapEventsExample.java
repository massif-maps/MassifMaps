package com.massifmaps.MassifDemo.examples.interaction;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MapEvents;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.Position;
import com.massifmaps.api.Spec;

/**
 * The camera event an app should hang its data refresh on, and how to tell whose move it was.
 */
@ExampleInfo(
    id = "map-events",
    title = "Refresh data when the map settles",
    description = "map.stable fires once when a movement ends, with the reason that caused it - "
                + "so a data refresh runs once per gesture, and never for your own camera calls.",
    section = Sections.INTERACTION,
    order = 20)
public class MapEventsExample extends MapExample {

    private int moves;
    private int stables;

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

        map.eventProjection("EPSG:4326");
        map.camera().moveTo(new Position(6.8652, 45.8326), 11);

        // Every camera change, whatever caused it - 47 to 159 a second during a drag, which is
        // exactly why it is the wrong place to refresh anything. Throttled to 4 a second: events
        // inside the window are DROPPED, not delivered late, because the payload does not outlive
        // the emit. Good for a readout that should track the movement.
        map.onMove(new MapEvents.Handler<MapEvents.Move>() {
            @Override
            public void handle(MapEvents.Move e) {
                moves++;
                host.caption(String.format("moving (%s) - %d frames delivered", e.reason(), moves));
            }
        }, 250);

        // The end of a movement, once, with what caused it. A tap that did not move the camera
        // does not fire it, so there is no "did it actually move?" flag to keep.
        map.onStable(new MapEvents.Handler<MapEvents.Move>() {
            @Override
            public void handle(MapEvents.Move e) {
                stables++;
                // A refresh should follow the USER, not the app's own camera calls - otherwise
                // the "fly here" button below would trigger the very fetch it just made stale.
                String action = e.byUser() ? "refreshing" : "ignored (" + e.reason() + ")";
                host.caption(String.format("%d moves -> %d stable  ·  %s", moves, stables, action));
            }
        });

        host.button("Fly to Chamonix", new Runnable() {
            @Override
            public void run() {
                // Raises stable with reason "animation", not "gesture".
                map.camera().animate(1.5f).moveTo(new Position(6.8652, 45.9237), 13);
            }
        });

        host.caption("Drag the map, then press the button. Watch the reason change.");
    }
}
