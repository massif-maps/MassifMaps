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
                + "parses the whole feature - and the geometry is only serialised if you want it.",
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
            .set("source", Spec.of("http")
                .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                .set("maxZoom", 14)
                .set("HTTPHeaders", Spec.object().set("User-Agent", "MassifMapsExamples/1.0")))
            .set("style", "alpine"));

        map.camera().moveTo(new Position(5.7245, 45.1885), 14.5f);

        base.onFeatureClick(new MapEvents.Handler<MapEvents.VectorTileClick>() {
            @Override
            public void handle(MapEvents.VectorTileClick e) {
                // Each of these is one read out of the payload. Nothing else is touched.
                String name = e.property("name");
                String kind = e.property("class");
                Position where = e.position();
                host.caption(e.layerName()
                             + (name != null ? " - " + name : "")
                             + (kind != null ? " (" + kind + ")" : "")
                             + String.format("  %.5f, %.5f", where.lat, where.lng));
            }
        });
        host.caption("Tap a road, a building or the water.");
    }
}
