package com.massifmaps.MassifDemo.examples.terrain;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifLayer;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.Position;
import com.massifmaps.api.Spec;

@ExampleInfo(
    id = "hillshade",
    title = "Hillshade",
    description = "Shaded relief computed on the fly from an elevation source and laid over the "
                + "basemap: pick the algorithm, scale the relief.",
    section = Sections.TERRAIN,
    order = 50)
public class HillshadeExample extends MapExample {

    private static final String UA = "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    private static final String[] METHODS = { "IGOR", "COMBINED", "MULTIDIRECTIONAL", "STANDARD", "BASIC" };

    private int method = 0;

    @Override
    public void onStart(final ExampleHost host) {
        MassifMap map = host.map();

        map.style("massif", Spec.of("mbvt")
            .set("project", Spec.of("project")
                .set("assets", Spec.of("zip")
                    .set("data", Spec.of("url").set("url", "assets://styles/massif.zip")))
                .set("name", "streets")));
        map.addLayer("basemap", Spec.of("vector")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("openfreemap.db"))
                .set("capacity", 100 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                    .set("maxZoom", 14)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA))))
            .set("style", "massif"));
        // heightScale 1 is the true slope, what MapLibre shades; the default is a twentieth of it.
        final MassifLayer hillshade = map.addLayer("hillshade", Spec.of("hillshade")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("mapterhorn-dem.db"))
                .set("capacity", 200 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.mapterhorn.com/{z}/{x}/{y}.webp")
                    .set("minZoom", 1)
                    .set("maxZoom", 16)
                    .set("metaData", Spec.object().set("dem_encoding", "terrarium"))))
            .set("hillshadeMethod", METHODS[method])
            .set("heightScale", 1));
        map.camera().moveTo(new Position(5.83, 45.25), 10.5f);

        host.button("Method", new Runnable() {
            @Override
            public void run() {
                method = (method + 1) % METHODS.length;
                hillshade.set("hillshadeMethod", METHODS[method]);
                host.caption(METHODS[method] + " hillshade.");
            }
        });
        // A shader uniform: the relief changes without a tile being decoded again.
        host.slider("Exaggeration", 0f, 3f, 1f, new ExampleHost.OnValue() {
            @Override
            public void onValue(float value) {
                hillshade.set("exaggeration", value);
            }
        });
        host.caption("Chartreuse, Vercors and Belledonne, shaded from a terrarium DEM.");
    }
}
