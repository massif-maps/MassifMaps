package com.massifmaps.MassifDemo.examples.styles;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifSource;
import com.massifmaps.api.Position;
import com.massifmaps.api.Spec;

/**
 * glTF models placed at points by a CartoCSS `model` rule: Massif's own trees, and a wind turbine
 * whose rotor spins - the way an app adds models of its own.
 */
@ExampleInfo(
    id = "3d-models",
    title = "Draw 3D models",
    description = "A CartoCSS `model` rule places a .glb at every point: trees with a random "
                + "model, size, heading and tint each, and a wind turbine whose rotor spins. "
                + "Massif's own trees take `params.models_3d` and their .glb files added to the style package.",
    section = Sections.STYLES,
    order = 25)
public class ModelsExample extends MapExample {

    private static final String UA = "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    /** The models Massif ships (styles/massif/models), read straight from the repository. */
    private static final String MODELS = "https://raw.githubusercontent.com/massif-maps/MassifMaps/master/styles/massif/models/";

    /** The trees of Grenoble's Jardin de Ville, from OpenStreetMap (natural=tree). */
    private static final String TREES =
        "{\"type\":\"Feature\",\"properties\":{},\"geometry\":{\"type\":\"MultiPoint\",\"coordinates\":["
        + "[5.72924,45.19367],[5.72935,45.19370],[5.72937,45.19366],[5.72985,45.19367],[5.72698,45.19293],[5.72688,45.19293],"
        + "[5.72797,45.19306],[5.72786,45.19304],[5.72771,45.19271],[5.72909,45.19364],[5.72911,45.19272],[5.72685,45.19239],"
        + "[5.72692,45.19224],[5.72710,45.19240],[5.72709,45.19226],[5.72781,45.19245],[5.72788,45.19247],[5.72691,45.19200],"
        + "[5.72714,45.19220],[5.72717,45.19214],[5.72710,45.19207],[5.72706,45.19200],[5.72717,45.19207],[5.72684,45.19175],"
        + "[5.72694,45.19173],[5.72697,45.19195],[5.72699,45.19189],[5.72722,45.19189],[5.72705,45.19181],[5.72712,45.19175],"
        + "[5.72717,45.19178],[5.72723,45.19182],[5.72725,45.19176],[5.72683,45.19165],[5.72681,45.19153],[5.72700,45.19169],"
        + "[5.72718,45.19168],[5.72705,45.19155],[5.72753,45.19172],[5.72989,45.19364],[5.72992,45.19361],[5.72989,45.19258],"
        + "[5.73045,45.19224],[5.72996,45.19179],[5.72997,45.19179],[5.72998,45.19179],[5.72993,45.19178],[5.72993,45.19176],"
        + "[5.72993,45.19175],[5.72993,45.19174],[5.73048,45.19214],[5.73103,45.19252],[5.73100,45.19249],[5.73119,45.19265],"
        + "[5.73115,45.19261],[5.73109,45.19257],[5.73103,45.19243],[5.73109,45.19240],[5.73111,45.19239],[5.73117,45.19236],"
        + "[5.73110,45.19181],[5.72992,45.19170],[5.72992,45.19169],[5.72992,45.19167],[5.72992,45.19166],[5.72992,45.19164],"
        + "[5.72991,45.19163],[5.72991,45.19161],[5.73000,45.19163],[5.72987,45.19149],[5.72986,45.19148],[5.73003,45.19153],"
        + "[5.73001,45.19146],[5.73009,45.19154],[5.73018,45.19154],[5.73021,45.19152],[5.73068,45.19167],[5.73068,45.19160],"
        + "[5.73065,45.19152],[5.73012,45.19146],[5.73006,45.19144],[5.73016,45.19144],[5.73020,45.19143]"
        + "]}}";

    /** Three turbines along the Isere quay, where no real wind farm stands. */
    private static final String TURBINES =
        "{\"type\":\"Feature\",\"properties\":{},\"geometry\":{\"type\":\"MultiPoint\",\"coordinates\":["
        + "[5.7232,45.1938],[5.7262,45.1939],[5.7292,45.1941]]}}";

    private static final String STYLE = String.join("\n",
        // random() is seeded by each point's position: every tree keeps its own model, size,
        // heading and tint through every zoom, and the MultiPoint's trees differ from each other.
        "#trees {",
        "  model-file: step(random(0, 3), (0, url('" + MODELS + "tree-spreading.glb')),",
        "    (1, url('" + MODELS + "tree-layered.glb')), (2, url('" + MODELS + "tree-clumped.glb')));",
        "  model-scale: random(0.8, 1.2);",
        "  model-rotation: random(0, 360);",
        "  model-color: #b4dcb4;",
        "  model-color-mix: random(0, 0.3);",
        "}",
        // A spinning part is a model of its own, its origin on the axis, lifted onto the tower.
        "#turbines::tower {",
        "  model-file: url('" + MODELS + "wind-turbine-tower.glb');",
        "  model-rotation: 270;",
        "}",
        "#turbines::rotor {",
        "  model-file: url('" + MODELS + "wind-turbine-rotor.glb');",
        "  model-rotation: 270;",
        "  model-z-offset: 81.4;",
        "  model-spin: 14;",
        "  model-spin-axis: y;",
        "}");

    @Override
    public void onStart(ExampleHost host) {
        final MassifMap map = host.map();

        // Massif's own trees and turbines: models_3d draws them at the tiles' poi points of class
        // tree and wind_turbine. Their .glb files are NOT in the style package - download the five
        // from https://github.com/massif-maps/MassifMaps/tree/master/styles/massif/models
        // and put them in a models/ folder at the package root, beside project.json. OpenFreeMap's
        // tiles carry no such points anyway, so the models below are drawn on the app's own.
        map.style("massif", Spec.of("mbvt")
            .set("project", Spec.of("project")
                .set("assets", Spec.of("zip").set("data", Spec.of("url").set("url", "assets://styles/massif.zip")))
                .set("name", "streets"))
            .set("params", Spec.object().set("models_3d", "1")));
        map.addLayer("basemap", Spec.of("vector")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("openfreemap.db"))
                .set("capacity", 100 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                    .set("maxZoom", 14)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA))))
            .set("style", "massif"));

        MassifSource points = map.source("model-points", Spec.of("geojson").set("maxZoom", 14));
        points.setLayerGeoJSON(points.createLayer("trees"), TREES);
        points.setLayerGeoJSON(points.createLayer("turbines"), TURBINES);
        map.addLayer("models", Spec.of("vector")
            .set("source", "model-points")
            .set("style", Spec.of("mbvt").set("cartocss", Spec.of("cartocss").set("css", STYLE))));

        // A terrain and a lit ground, for the shadows: cast shadows land on the terrain surface.
        map.terrain(Spec.of("terrain")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("mapterhorn-dem.db"))
                .set("capacity", 200 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.mapterhorn.com/{z}/{x}/{y}.webp")
                    .set("minZoom", 1)
                    .set("maxZoom", 16)
                    .set("metaData", Spec.object().set("dem_encoding", "terrarium")))));
        map.light(Spec.of("light")
            .set("sunOverridingStyle", true)
            .set("sunAzimuth", 220)
            .set("sunAltitude", 40)
            .set("terrainLightingEnabled", true)
            .set("shadowStrength", 1.5));

        map.camera().moveTo(new Position(5.7262, 45.1928), 17.0f, 0f, 32f);

        host.toggle("Spin", true, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                // Off, the rotors stand still and the map stops redrawing every frame for them.
                map.set("modelAnimationsEnabled", on);
            }
        });
        host.toggle("Model shadows", true, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                // On a slow GPU the trees' shadows can cost more than the trees themselves.
                map.set("modelShadowsEnabled", on);
            }
        });
        host.caption("Every tree draws its own model, size and heading; the rotors spin.");
    }
}
