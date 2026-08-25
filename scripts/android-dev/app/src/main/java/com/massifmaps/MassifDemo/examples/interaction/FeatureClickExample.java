package com.massifmaps.MassifDemo.examples.interaction;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MapEvents;
import com.massifmaps.api.MassifLayer;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.Spec;
import com.massifmaps.api.Position;

/**
 * Reading the feature under a tap, without parsing a tile.
 */
@ExampleInfo(
    id = "feature-click",
    title = "Get the feature under a tap",
    description = "The click payload is read lazily by path, so asking for one property never "
                + "parses the whole feature - and a handler that found what it wanted returns "
                + "true to claim the click, so nothing after it sees the tap.",
    section = Sections.INTERACTION,
    order = 10)
public class FeatureClickExample extends MapExample {

    @Override
    public void onStart(final ExampleHost host) {
        MassifMap map = host.map();

        map.style("alpine", Spec.of("mbvt")
            .set("project", Spec.of("project")
                .set("assets", Spec.of("zip")
                    .set("data", Spec.of("url").set("url", "assets://styles/alpine.zip")))));

        MassifLayer base = map.addLayer("basemap", Spec.of("vector")
            // Cached on disk in front of the server: openfreemap is a free service, and a demo
            // that gets panned around re-fetches the same tiles on every run.
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("openfreemap.db"))
                .set("capacity", 100 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                    .set("maxZoom", 14)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", "MassifMapsExamples/1.0"))))
            .set("style", "alpine"));

        map.camera().moveTo(new Position(5.7245, 45.1885), 14.5f);

        // consumeFeatureClick, not onFeatureClick: returning true CLAIMS the click, so nothing
        // after this handler sees it - no other subscriber, and no map.clicked. That is how you
        // stop as soon as you have found the feature you care about, instead of letting the tap
        // fall through and also drop a pin.
        //
        // It runs on the thread the click came from, because the SDK asks whether the event was
        // consumed NOW rather than later. Keep it short and do not touch the map from it.
        base.consumeFeatureClick(new MapEvents.ConsumingHandler<MapEvents.VectorTileClick>() {
            @Override
            public boolean handle(MapEvents.VectorTileClick e) {
                // Each of these is one read out of the payload. Nothing else is touched.
                String name = e.property("name");
                String kind = e.property("class");
                if (name == null) {
                    // Nothing worth stopping for - let the click carry on to the map.
                    return false;
                }
                Position where = e.position();
                host.caption("took " + e.layerName() + " - " + name
                             + (kind != null ? " (" + kind + ")" : "")
                             + String.format("  %.5f, %.5f", where.lat, where.lng));
                return true;
            }
        });

        // Only reached when the handler above declined: a named feature never gets here.
        map.onClick(new MapEvents.Handler<MapEvents.Click>() {
            @Override
            public void handle(MapEvents.Click e) {
                host.caption("nothing named there - the click fell through to the map");
            }
        });

        host.caption("Tap a named road or building, then somewhere empty.");
    }
}
