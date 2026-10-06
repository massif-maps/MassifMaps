package com.massifmaps.MassifDemo.examples.search;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifObject;
import com.massifmaps.api.MassifSource;
import com.massifmaps.api.Position;
import com.massifmaps.api.Spec;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * Turn arrows cut from a route at each maneuver, the head drawn by the line style itself.
 */
@ExampleInfo(
    id = "maneuver-arrows",
    title = "Navigation maneuver arrows",
    description = "A drive round Annecy station - turns, roundabouts, a U-turn and a lane change - each "
                + "cut from the route and drawn as one line whose head is a line property: no marker, no "
                + "bitmap, and a casing that outlines shaft and head alike.",
    section = Sections.SEARCH,
    order = 30)
public class ManeuverArrowsExample extends MapExample {

    /** A tile server wants to know who is asking: a real app identifies itself. */
    private static final String UA = "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    /** A drive round Annecy station, as OSRM routes it: roundabouts, a U-turn, both turns. */
    private static final double[][] ROUTE = {
        { 6.121812, 45.901688 }, { 6.121752, 45.901660 }, { 6.121601, 45.901624 }, { 6.120724, 45.901472 }, { 6.119987, 45.901336 },
        { 6.119685, 45.901347 }, { 6.119667, 45.901362 }, { 6.119623, 45.901381 }, { 6.119589, 45.901386 }, { 6.119537, 45.901382 },
        { 6.119480, 45.901356 }, { 6.119457, 45.901330 }, { 6.119447, 45.901301 }, { 6.119213, 45.901192 }, { 6.118270, 45.901021 },
        { 6.118021, 45.900993 }, { 6.118270, 45.901021 }, { 6.119213, 45.901192 }, { 6.119505, 45.901219 }, { 6.119555, 45.901205 },
        { 6.119609, 45.901206 }, { 6.119664, 45.901077 }, { 6.119682, 45.901049 }, { 6.119713, 45.901022 }, { 6.119974, 45.900895 },
        { 6.120225, 45.900750 }, { 6.120287, 45.900686 }, { 6.120363, 45.900643 }, { 6.120416, 45.900599 }, { 6.120501, 45.900591 },
        { 6.120565, 45.900594 }, { 6.121097, 45.900714 }, { 6.121143, 45.900719 }, { 6.121209, 45.900716 }, { 6.121222, 45.900697 },
        { 6.121259, 45.900678 }, { 6.121321, 45.900679 }, { 6.121345, 45.900689 }, { 6.121366, 45.900709 }, { 6.121374, 45.900734 },
        { 6.121366, 45.900758 }, { 6.121409, 45.900806 }, { 6.121458, 45.900838 }, { 6.121733, 45.900903 }, { 6.121889, 45.900912 },
        { 6.121956, 45.900900 }, { 6.122027, 45.900879 }, { 6.122141, 45.900825 }, { 6.122171, 45.900807 }, { 6.122211, 45.900767 },
        { 6.122356, 45.900473 }, { 6.122629, 45.899880 }, { 6.122331, 45.899798 }, { 6.121724, 45.899628 }, { 6.121764, 45.899520 },
        { 6.122008, 45.899000 }, { 6.122070, 45.898810 }, { 6.121217, 45.898719 },
    };

    /**
     * Route point index of each maneuver as a routing engine reports it, the metres of route kept
     * before and after it, and a sideways shift in metres for a lane change (0 = follow the route).
     */
    private static final Object[][] MANEUVERS = {
        { 3, 30.0, 35.0, 3.5, "Move to the left lane on Rue de l'Industrie" },
        { 5, 20.0, 45.0, 0.0, "At the roundabout, take the exit onto Avenue de Chevêne" },
        { 15, 30.0, 30.0, 0.0, "Make a U-turn on Avenue de Chevêne" },
        { 18, 30.0, 30.0, 0.0, "At the small roundabout, keep right on Avenue de Chevêne" },
        { 33, 25.0, 45.0, 0.0, "At the roundabout, take the exit onto Rue Vaugelas" },
        { 51, 30.0, 30.0, 0.0, "Turn right onto Rue Royale" },
        { 53, 30.0, 30.0, 0.0, "Turn left onto Rue de la Gare" },
        { 56, 30.0, 30.0, 0.0, "Turn right: you have arrived" },
    };

    private static final double METRES_PER_DEGREE = 111319.5;

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
        final MassifObject builder = map.object("geometry", "maneuver-arrows", Spec.of("maneuver-arrow"));
        maneuvers.setLayerGeoJSON(layer, arrows(builder, HEADS[head]));
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
                host.caption((step + 1) + "/" + MANEUVERS.length + ": " + MANEUVERS[step][4] + ".");
            }
        });
        host.button("Head shape", new Runnable() {
            @Override
            public void run() {
                head = (head + 1) % HEADS.length;
                maneuvers.setLayerGeoJSON(layer, arrows(builder, HEADS[head]));
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
        map.camera().animate(seconds).moveTo(new Position(6.1203, 45.9002), 16.3f, 0, 80);
        host.caption("One arrow per maneuver, cut from the route either side of it.");
    }

    private static String arrows(MassifObject builder, String head) {
        try {
            JSONArray features = new JSONArray();
            for (Object[] maneuver : MANEUVERS) {
                int index = (Integer) maneuver[0];
                double after = (Double) maneuver[2], shift = (Double) maneuver[3];
                // A lane change leaves the route: the builder walks the part behind, the shift is drawn ahead.
                builder.set("lengthBefore", maneuver[1]).set("lengthAfter", shift != 0 ? 0.0 : after);
                MassifObject result = builder.call("buildArrowAtIndex", ROUTE, index);
                JSONArray arrow = new JSONObject(result.json()).getJSONArray("features");
                result.close();
                for (int i = 0; i < arrow.length(); i++) {
                    JSONObject feature = arrow.getJSONObject(i);
                    if (shift != 0) {
                        JSONArray coordinates = feature.getJSONObject("geometry").getJSONArray("coordinates");
                        JSONArray last = coordinates.getJSONArray(coordinates.length() - 1);
                        if (Math.abs(last.getDouble(0) - ROUTE[index][0]) + Math.abs(last.getDouble(1) - ROUTE[index][1]) < 1e-9) {
                            coordinates.remove(coordinates.length() - 1);
                        }
                        for (double[] point : laneChange(index, after, shift)) {
                            coordinates.put(new JSONArray().put(point[0]).put(point[1]));
                        }
                    }
                    features.put(feature.put("properties", new JSONObject().put("head", head)));
                }
            }
            return new JSONObject().put("type", "FeatureCollection").put("features", features).toString();
        } catch (JSONException e) {
            throw new IllegalStateException(e);
        }
    }

    /** Ahead of point {@code index} along its segment, moving {@code shift} metres left over the first 40%. */
    private static double[][] laneChange(int index, double after, double shift) {
        double[] at = ROUTE[index], next = ROUTE[index + 1];
        double k = Math.cos(Math.toRadians(at[1]));
        double dx = (next[0] - at[0]) * k, dy = next[1] - at[1], d = Math.hypot(dx, dy);
        double left = shift / METRES_PER_DEGREE;
        double[][] out = new double[2][];
        for (int i = 0; i < 2; i++) {
            double along = (i == 0 ? 0.4 : 1.0) * after / METRES_PER_DEGREE;
            out[i] = new double[] { at[0] + (dx * along - dy * left) / d / k, at[1] + (dy * along + dx * left) / d };
        }
        return out;
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
