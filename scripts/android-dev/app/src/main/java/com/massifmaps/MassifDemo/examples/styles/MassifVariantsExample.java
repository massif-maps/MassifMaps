package com.massifmaps.MassifDemo.examples.styles;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifLayer;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifObject;
import com.massifmaps.api.MassifSource;
import com.massifmaps.api.Spec;
import com.massifmaps.api.Position;

/**
 * The Massif style family: five maps from one project, switched by a style parameter.
 */
@ExampleInfo(
    id = "massif-variants",
    title = "The Massif styles",
    description = "Streets, outdoor, topo, hybrid and e-ink are ONE CartoCSS project: the variant is "
                + "a style parameter, so switching loads nothing - `params.variant`. Relief and contours "
                + "are slots of every variant, filled here for outdoor and topo.",
    section = Sections.STYLES,
    order = 5)
public class MassifVariantsExample extends MapExample {

    private static final String UA = "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    /** CompositeSourceType, as the facade takes it. */
    private static final long SOURCE_HILLSHADE = 1;
    private static final long SOURCE_VECTOR = 2;

    private MassifLayer base;
    private MassifSource dem;
    private MassifSource contours;

    private static final String[][] VARIANTS = {
        { "streets", "Streets", "the everyday map" },
        { "outdoor", "Outdoor", "trails by difficulty, peaks, huts, cliffs" },
        { "topo", "Topo", "outdoor on a cooler, map-like ground" },
        { "hybrid", "Hybrid", "roads and labels over imagery" },
        { "eink", "E-ink", "black on white, for e-paper; inverts at night" },
    };

    @Override
    public void onStart(final ExampleHost host) {
        MassifMap map = host.map();

        // Hybrid draws over imagery the app supplies: a raster under the vector layer, shown for it alone.
        final MassifLayer imagery = map.addLayer("imagery", Spec.of("raster")
            .set("visible", false)
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("world-imagery.db"))
                .set("capacity", 200 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://server.arcgisonline.com/ArcGIS/rest/services/"
                              + "World_Imagery/MapServer/tile/{z}/{y}/{x}")
                    .set("maxZoom", 18)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA)))));

        // ONE project for all five (styles/massif/carto, bundled as assets/styles/massif.zip).
        final MassifObject style = map.style("massif", Spec.of("mbvt")
            .set("project", Spec.of("project")
                .set("assets", Spec.of("zip")
                    .set("data", Spec.of("url").set("url", "assets://styles/massif.zip")))
                .set("name", "streets")));

        // Every variant has a `hillshade` and a `contour` slot; a composite layer fills them with the
        // app's own DEM, here only for outdoor and topo - see showRelief.
        base = map.addLayer("basemap", Spec.of("composite-vector")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("openfreemap.db"))
                .set("capacity", 100 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                    .set("maxZoom", 14)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA))))
            .set("style", "massif"));
        dem = map.source("dem", Spec.of("persistent-cache")
            .set("databasePath", host.cachePath("mapterhorn-dem.db"))
            .set("capacity", 200 * 1024 * 1024)
            .set("source", Spec.of("http")
                .set("url", "https://tiles.mapterhorn.com/{z}/{x}/{y}.webp")
                .set("minZoom", 1)
                .set("maxZoom", 16)
                .set("metaData", Spec.object().set("dem_encoding", "terrarium"))));
        contours = map.source("contours", Spec.of("contour").set("source", "dem").set("baseInterval", 20));

        // Grenoble's Bastille: trails, a cable car, POIs and the old town in one view.
        map.camera().moveTo(new Position(5.7262, 45.1968), 14.5f);

        for (final String[] variant : VARIANTS) {
            host.button(variant[1], new Runnable() {
                @Override
                public void run() {
                    style.set("params.variant", variant[0]);
                    imagery.set("visible", "hybrid".equals(variant[0]));
                    showRelief("outdoor".equals(variant[0]) || "topo".equals(variant[0]));
                    host.caption("Massif " + variant[1] + ": " + variant[2] + ".");
                }
            });
        }
        host.caption("Massif Streets: the everyday map. Pick another variant.");
    }

    /** Relief and contours are in every variant's style; the app decides where they draw. */
    private void showRelief(boolean on) {
        if (on) {
            base.call("addExternalDataSource", "hillshade", dem.handle(), SOURCE_HILLSHADE);
            base.call("addExternalDataSource", "contour", contours.handle(), SOURCE_VECTOR);
        } else {
            base.call("removeExternalDataSource", "hillshade");
            base.call("removeExternalDataSource", "contour");
        }
    }
}
