package com.massifmaps.MassifDemo.examples.styles;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifObject;
import com.massifmaps.api.Spec;
import com.massifmaps.api.Position;

/**
 * A style's runtime parameters, and the two kinds a style can declare: Massif's own, changed live.
 */
@ExampleInfo(
    id = "style-parameters",
    title = "Change a style at runtime",
    description = "A style project declares `param::` values the app sets while the map runs, as "
                + "properties: `params.<name>`. A value swaps live; one used in a filter "
                + "re-decodes the tiles.",
    section = Sections.STYLES,
    order = 10)
public class StyleParametersExample extends MapExample {

    @Override
    public void onStart(ExampleHost host) {
        MassifMap map = host.map();

        // Registered under an id of its own rather than inlined in the layer spec, because the
        // example talks to it afterwards - a layer's style property cannot be read back as a handle.
        // The params are part of the spec, so the first frame is already right.
        final MassifObject style = map.style("massif", Spec.of("mbvt")
            .set("project", Spec.of("project")
                .set("assets", Spec.of("zip")
                    .set("data", Spec.of("url").set("url", "assets://styles/massif.zip")))
                .set("name", "streets"))
            .set("params", Spec.object().set("poiStyle", "badge")));

        map.addLayer("basemap", Spec.of("vector")
            // Cached on disk in front of the server: openfreemap is a free service, and a demo
            // that gets panned around re-fetches the same tiles on every run.
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("openfreemap.db"))
                .set("capacity", 100 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                    .set("maxZoom", 14)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", "MassifMapsExamples/1.0"))))
            .set("style", "massif"));

        map.camera().moveTo(new Position(5.7245, 45.1885), 15.5f);

        host.toggle("POI discs", true, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                // A style parameter is a PROPERTY: the rest of the path is the parameter's name.
                // LIVE: the decoded tiles point at this value, so the discs come and go with a redraw.
                style.set("params.poiStyle", on ? "badge" : "plain");
            }
        });
        host.toggle("Boundaries", true, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                // In a FILTER: this decides what the tile contains, so every tile decodes again. A
                // string, converted against the DECLARED default - 1 here, so "0" becomes the number 0.
                style.set("params.show_boundaries", on ? "1" : "0");
            }
        });
        host.button("Walker", new Runnable() {
            @Override
            public void run() {
                // Several at once, in ONE crossing - which is what a theme swap is.
                style.apply(Spec.object().set("params", Spec.object()
                    .set("highlight_drinking_water", "1")
                    .set("path_min_zoom", "12")
                    .set("sac_scale_labels", "1")));
            }
        });
        host.caption("Two parameters, two costs: a value swaps live, a filter re-decodes.");
    }
}
