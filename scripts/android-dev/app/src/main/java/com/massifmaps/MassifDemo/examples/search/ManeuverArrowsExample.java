package com.massifmaps.MassifDemo.examples.search;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifSource;
import com.massifmaps.api.Position;
import com.massifmaps.api.Spec;

/**
 * Turn arrows cut from a route at each maneuver, the head drawn by the line style itself.
 */
@ExampleInfo(
    id = "maneuver-arrows",
    title = "Navigation maneuver arrows",
    description = "The route cut 30 m either side of each turn, drawn as one line whose head is a "
                + "line property: no marker, no bitmap, and a casing that outlines shaft and head alike.",
    section = Sections.SEARCH,
    order = 30)
public class ManeuverArrowsExample extends MapExample {

    /** A tile server wants to know who is asking: a real app identifies itself. */
    private static final String UA = "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    /** A drive through the Eixample, Barcelona, along its one-way streets. */
    private static final double[][] ROUTE = {
        { 2.16274, 41.39225 }, { 2.16321, 41.39260 }, { 2.16365, 41.39294 }, { 2.16422, 41.39249 }, { 2.16433, 41.39244 },
        { 2.16476, 41.39210 }, { 2.16493, 41.39196 }, { 2.16522, 41.39168 }, { 2.16583, 41.39119 }, { 2.16597, 41.39130 },
        { 2.16606, 41.39136 }, { 2.16613, 41.39141 }, { 2.16615, 41.39143 }, { 2.16657, 41.39174 }, { 2.16697, 41.39204 },
        { 2.16729, 41.39229 }, { 2.16755, 41.39229 }, { 2.16763, 41.39233 }, { 2.16813, 41.39195 }, { 2.16861, 41.39158 },
        { 2.16940, 41.39098 }, { 2.16971, 41.39075 }, { 2.16987, 41.39063 }, { 2.17061, 41.39117 }, { 2.17077, 41.39135 },
        { 2.17097, 41.39153 }, { 2.17086, 41.39162 }, { 2.17016, 41.39215 },
    };

    /** Route point index of each maneuver, as a routing engine reports it. */
    private static final Object[][] MANEUVERS = {
        { 2, "Turn right onto Passeig de Gràcia" },
        { 8, "Turn left onto Carrer del Consell de Cent" },
        { 17, "Turn right onto Carrer de Pau Claris" },
        { 22, "Turn left onto Gran Via de les Corts Catalanes" },
        { 25, "Turn left onto Carrer de Roger de Llúria" },
    };

    private static final String[] HEADS = { "classic", "wide", "long" };

    private static final String ROUTE_STYLE = String.join("\n",
        "#route::case { line-color: #0D47A1; line-width: linear([view::zoom], (12, 3.5), (17, 11)); line-join: round; line-cap: round; }",
        "#route { line-color: #1A73E8; line-width: linear([view::zoom], (12, 2.4), (17, 7.5)); line-join: round; line-cap: round; }");

    // Casing first, head over its shaft; the casing's head numbers are smaller because they are read
    // against its own wider line (docs/features/maneuver-arrows.md).
    private static final String ARROW_STYLE = String.join("\n",
        "#maneuver::case { line-color: #0D47A1; line-width: linear([view::zoom], (12, 3.9), (17, 13)); line-join: round; line-cap: round; }",
        "#maneuver::fill { line-color: #FFFFFF; line-width: linear([view::zoom], (12, 2.4), (17, 8)); line-join: round; line-cap: round; }",
        "#maneuver::headcase {",
        "  line-color: #0D47A1; line-width: linear([view::zoom], (12, 3.9), (17, 13));",
        "  line-end-arrow: true; line-arrow-only: true; line-arrow-width: 2.18; line-arrow-length: 1.72;",
        "  [head='wide'] { line-arrow-width: 2.94; line-arrow-length: 1.38; }",
        "  [head='long'] { line-arrow-width: 1.71; line-arrow-length: 2.51; }",
        "}",
        "#maneuver::head {",
        "  line-color: #FFFFFF; line-width: linear([view::zoom], (12, 2.4), (17, 8));",
        "  line-end-arrow: true; line-arrow-only: true; line-arrow-width: 2.4; line-arrow-length: 1.9;",
        "  [head='wide'] { line-arrow-width: 3.2; line-arrow-length: 1.5; }",
        "  [head='long'] { line-arrow-width: 1.9; line-arrow-length: 2.8; }",
        "}");

    private static final double METRES_PER_DEGREE = 111319.5;

    private int step = -1;
    private int head = 0;

    @Override
    public void onStart(final ExampleHost host) {
        final MassifMap map = host.map();

        // The Massif streets style (styles/massif/carto, bundled by the build as assets/styles/massif.zip)
        // over OpenFreeMap's vector tiles, cached on disk: a free service a demo would re-fetch every run.
        map.addLayer("basemap", Spec.of("vector")
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("openfreemap.db"))
                .set("capacity", 100 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf")
                    .set("maxZoom", 14)
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA))))
            .set("style", Spec.of("mbvt").set("project", Spec.of("project")
                .set("assets", Spec.of("zip").set("data", Spec.of("url").set("url", "assets://styles/massif.zip")))
                .set("name", "streets"))));

        MassifSource route = map.source("route-data", Spec.of("geojson").set("maxZoom", 18));
        route.setLayerGeoJSON(route.createLayer("route"),
            "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\",\"properties\":{},"
            + "\"geometry\":{\"type\":\"LineString\",\"coordinates\":" + coordinates(ROUTE) + "}}]}");
        map.addLayer("route", Spec.of("vector")
            .set("source", "route-data")
            .set("style", Spec.of("mbvt").set("cartocss", Spec.of("cartocss").set("css", ROUTE_STYLE))));

        // A layer of its own, added last: it draws over the route and every layer before it.
        final MassifSource maneuvers = map.source("maneuver-data", Spec.of("geojson").set("maxZoom", 18));
        final int layer = maneuvers.createLayer("maneuver");
        maneuvers.setLayerGeoJSON(layer, arrows(HEADS[head]));
        map.addLayer("maneuver", Spec.of("vector")
            .set("source", "maneuver-data")
            .set("style", Spec.of("mbvt").set("cartocss", Spec.of("cartocss").set("css", ARROW_STYLE))));

        overview(map, host, 0);

        host.button("Next maneuver", new Runnable() {
            @Override
            public void run() {
                step = (step + 1) % MANEUVERS.length;
                int index = (Integer) MANEUVERS[step][0];
                map.camera().animate(1.5f).moveTo(new Position(ROUTE[index][0], ROUTE[index][1]),
                                                  17, (float) -bearing(index), 70);
                host.caption((step + 1) + "/" + MANEUVERS.length + ": " + MANEUVERS[step][1] + ".");
            }
        });
        host.button("Head shape", new Runnable() {
            @Override
            public void run() {
                head = (head + 1) % HEADS.length;
                maneuvers.setLayerGeoJSON(layer, arrows(HEADS[head]));
                host.caption(HEADS[head] + " head: line-arrow-width and -length, no marker and no bitmap.");
            }
        });
        host.button("Overview", new Runnable() {
            @Override
            public void run() {
                overview(map, host, 1.5f);
            }
        });
    }

    private static void overview(MassifMap map, ExampleHost host, float seconds) {
        map.camera().animate(seconds).moveTo(new Position(2.1674, 41.3916), 16.1f, 0, 80);
        host.caption("One arrow per maneuver, cut from the route 30 m either side of the turn.");
    }

    /**
     * The route from {@code before} metres behind point {@code index} to {@code after} metres past it,
     * clamped at the ends. The facade has no ManeuverArrowBuilder.buildArrow yet: this is its walk.
     */
    private static double[][] arrowAt(int index, double before, double after) {
        double[][] back = walk(index, -1, before), ahead = walk(index, 1, after);
        double[][] arrow = new double[back.length + 1 + ahead.length][];
        for (int i = 0; i < back.length; i++) {
            arrow[i] = back[back.length - 1 - i];
        }
        arrow[back.length] = ROUTE[index];
        System.arraycopy(ahead, 0, arrow, back.length + 1, ahead.length);
        return arrow;
    }

    private static double[][] walk(int index, int step, double length) {
        double k = Math.cos(Math.toRadians(ROUTE[index][1]));
        java.util.List<double[]> out = new java.util.ArrayList<double[]>();
        double[] at = ROUTE[index];
        for (int i = index + step; i >= 0 && i < ROUTE.length && length > 0; i += step) {
            double[] next = ROUTE[i];
            double d = Math.hypot((next[0] - at[0]) * k, next[1] - at[1]) * METRES_PER_DEGREE;
            double t = d > length ? length / d : 1;
            out.add(new double[] { at[0] + (next[0] - at[0]) * t, at[1] + (next[1] - at[1]) * t });
            length -= d;
            at = next;
        }
        return out.toArray(new double[0][]);
    }

    private static String arrows(String head) {
        StringBuilder json = new StringBuilder("{\"type\":\"FeatureCollection\",\"features\":[");
        for (int i = 0; i < MANEUVERS.length; i++) {
            json.append(i > 0 ? "," : "")
                .append("{\"type\":\"Feature\",\"properties\":{\"head\":\"").append(head).append("\"},")
                .append("\"geometry\":{\"type\":\"LineString\",\"coordinates\":")
                .append(coordinates(arrowAt((Integer) MANEUVERS[i][0], 30, 30))).append("}}");
        }
        return json.append("]}").toString();
    }

    private static String coordinates(double[][] points) {
        StringBuilder json = new StringBuilder("[");
        for (int i = 0; i < points.length; i++) {
            json.append(i > 0 ? "," : "").append('[').append(points[i][0]).append(',').append(points[i][1]).append(']');
        }
        return json.append(']').toString();
    }

    /** Compass bearing of the route as it arrives at point {@code index}, for a heading-up camera. */
    private static double bearing(int index) {
        double[] a = ROUTE[index - 1], b = ROUTE[index];
        return Math.toDegrees(Math.atan2((b[0] - a[0]) * Math.cos(Math.toRadians(b[1])), b[1] - a[1]));
    }
}
