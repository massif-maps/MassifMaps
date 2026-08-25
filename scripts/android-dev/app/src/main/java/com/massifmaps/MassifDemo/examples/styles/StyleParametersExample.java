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
 * A CartoCSS style project, and the two kinds of runtime parameter it can declare.
 */
@ExampleInfo(
    id = "style-parameters",
    title = "Change a style at runtime",
    description = "A style project declares `param::` values the app sets while the map runs, as "
                + "properties: `params.<name>`. A colour swaps live; one used in a filter "
                + "re-decodes the tiles.",
    section = Sections.STYLES,
    order = 10)
public class StyleParametersExample extends MapExample {

    private static final String[] WATER = { "#8fb8d8", "#2f6f4f", "#7f5af0" };

    private int water;

    @Override
    public void onStart(ExampleHost host) {
        MassifMap map = host.map();

        // The style project is a zip in the assets: alpine.json (which declares the parameters)
        // plus style.mss. See app/src/main/style-projects/alpine.
        //
        // Registered under an id of its own rather than inlined in the layer spec, because the
        // example talks to it afterwards - a layer's style property cannot be read back as a
        // handle. A spec key that is a STRING is looked up in the registry, which is what
        // "style": "alpine" below does.
        //
        // The parameters are part of the spec, so the style is built with them already applied
        // rather than being corrected on the first frame.
        final MassifObject style = map.style("alpine", Spec.of("mbvt")
            .set("project", Spec.of("project")
                .set("assets", Spec.of("zip")
                    .set("data", Spec.of("url").set("url", "assets://styles/alpine.zip"))))
            .set("params", Spec.object()
                .set("water_color", WATER[0])
                .set("show_buildings", "true")));

        map.addLayer("basemap", Spec.of("vector")
            .set("source", Spec.of("http")
                .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                .set("maxZoom", 14)
                .set("HTTPHeaders", Spec.object().set("User-Agent", "MassifMapsExamples/1.0")))
            .set("style", "alpine"));

        map.camera().moveTo(new Position(5.7245, 45.1885), 13.5f);

        host.button("Water colour", new Runnable() {
            @Override
            public void run() {
                water = (water + 1) % WATER.length;
                // A style parameter is a PROPERTY: the rest of the path is the parameter's name.
                // LIVE: the decoded tiles already point at this value, so it swaps and redraws.
                style.set("params.water_color", WATER[water]);
            }
        });
        host.toggle("Buildings", true, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                // In a FILTER: this decides what the tile contains, so every tile decodes again.
                style.set("params.show_buildings", String.valueOf(on));
            }
        });
        host.button("Night", new Runnable() {
            @Override
            public void run() {
                // Several at once, in ONE crossing - which is what a theme swap is.
                style.apply(Spec.object().set("params", Spec.object()
                    .set("water_color", "#0b2b4a")
                    .set("show_buildings", "false")));
            }
        });
        host.caption("Two parameters, two costs: a colour swaps live, a filter re-decodes.");
    }
}
