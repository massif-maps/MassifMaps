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
 * Shields that give their name the free side of the icon, font icons, plates and callout labels.
 */
@ExampleInfo(
    id = "label-styling",
    title = "Shields, font icons and callouts",
    description = "Label styling is CartoCSS alone: a name takes the free side of its icon, an icon "
                + "is a glyph of an image or a font on a plate, a trail's ref repeats along it, and "
                + "summit names lift onto leader lines instead of hiding.",
    section = Sections.STYLES,
    order = 30)
public class LabelStylingExample extends MapExample {

    /** A tile server wants to know who is asking: a real app identifies itself. */
    private static final String UA = "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    /** Chamonix and the summits above it, at OpenFreeMap's own positions so they sit on the basemap. */
    private static final String POIS = collection(
        poi("Gare de Chamonix", "railway", null, "#3b6fd8", 6.87384, 45.92278),
        poi("Montenvers train", "railway", null, "#3b6fd8", 6.87533, 45.92263),
        poi("Aiguille du Midi cable car", "aerialway", null, "#3b6fd8", 6.87014, 45.91814),
        poi("Planpraz gondola", "aerialway", null, "#3b6fd8", 6.86319, 45.92404),
        poi("Musée Alpin", "museum", null, "#c7801a", 6.87126, 45.92402),
        poi("Tourist office", null, "i", "#0f766e", 6.86835, 45.92344),
        poi("Parking du Mont Blanc", null, "P", "#1d4ed8", 6.87284, 45.92495),
        poi("Refuge de Bellachat", "alpine_hut", null, "#2f855a", 6.82961, 45.92218),
        poi("Refuge du Plan de l’Aiguille", "alpine_hut", null, "#2f855a", 6.88273, 45.90561),
        poi("Refuge des Cosmiques", "alpine_hut", null, "#2f855a", 6.88558, 45.87324));

    private static final String PEAKS = collection(
        peak("Mont Blanc", 4807, 6.86517, 45.8327),
        peak("Aiguille du Midi", 3842, 6.88735, 45.87864),
        peak("Aiguille du Plan", 3673, 6.90722, 45.8917),
        peak("Aiguille de Blaitière", 3522, 6.91304, 45.89928));

    /** The TMB from Les Houches over the Brévent to La Flégère. */
    private static final String TRAIL = collection(
        "{\"type\":\"Feature\",\"properties\":{\"ref\":\"TMB\"},\"geometry\":{\"type\":\"LineString\",\"coordinates\":["
        + "[6.7985,45.8905],[6.8135,45.9075],[6.82961,45.92218],[6.83783,45.93392],"
        + "[6.85259,45.93585],[6.8712,45.9481],[6.8889,45.9607]]}}");

    private MassifMap map;
    private boolean freeSide = true;
    private boolean callouts = true;
    private int generation;

    @Override
    public void onStart(ExampleHost host) {
        map = host.map();

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
                .set("assets", massifAssets())
                .set("name", "streets"))));

        MassifSource data = map.source("label-data", Spec.of("geojson").set("maxZoom", 14));
        data.setLayerGeoJSON(data.createLayer("trail"), TRAIL);
        data.setLayerGeoJSON(data.createLayer("poi"), POIS);
        data.setLayerGeoJSON(data.createLayer("peak"), PEAKS);
        show();

        map.camera().moveTo(new Position(6.878, 45.9017), 11.5f);

        host.toggle("Free side", true, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                freeSide = on;
                show();
            }
        });
        host.toggle("Callouts", true, new ExampleHost.OnToggle() {
            @Override
            public void onToggle(boolean on) {
                callouts = on;
                show();
            }
        });
        host.caption("Names take the free side of their icon, summits lift theirs onto a leader line.");
    }

    /** Placement is fixed when a tile is decoded, so a switch is a new layer. */
    private void show() {
        generation++;
        map.addLayer("labels." + generation, Spec.of("vector")
            .set("source", "label-data")
            .set("style", Spec.of("mbvt").set("cartocss", Spec.of("cartocss")
                .set("css", labelStyle(freeSide, callouts))
                // Over Massif's own files, so `shield-file` finds its icon glyphs.
                .set("assets", massifAssets()))));
        if (generation > 1) {
            map.removeLayer("labels." + (generation - 1));
        }
    }

    private static Spec massifAssets() {
        return Spec.of("zip").set("data", Spec.of("url").set("url", "assets://styles/massif.zip"));
    }

    /** One CartoCSS for the three layers; the toggles flip the two properties it takes. */
    private static String labelStyle(boolean freeSide, boolean callouts) {
        return String.join("\n",
            "@medium: 'ios:Helvetica Neue Medium, Roboto Medium, sans-serif Medium';",
            "@bold: 'ios:Helvetica Neue Bold, Roboto Bold, sans-serif Bold';",
            "#trail {",
            "  line-color: #d6322b;",
            "  line-width: 3;",
            "  line-dasharray: 8, 4;",
            "}",
            // The road-shield placement: upright, repeated along the line, on a plate.
            "#trail::ref {",
            "  text-name: [ref];",
            "  text-face-name: @bold;",
            "  text-size: 11;",
            "  text-fill: #ffffff;",
            "  text-placement: billboard-line-repeat;",
            "  text-spacing: 100;",
            "  text-background-fill: #d6322b;",
            "  text-background-radius: 3;",
            "  text-background-padding-x: 4;",
            "  text-background-padding-y: 2;",
            "  text-background-border-fill: #ffffff;",
            "  text-background-border-width: 1.5;",
            "}",
            "#poi {",
            "  shield-name: [name];",
            "  shield-face-name: @medium;",
            "  shield-size: 12;",
            "  shield-fill: #1f2937;",
            "  shield-halo-fill: #ffffff;",
            "  shield-halo-radius: 1.5;",
            "  shield-wrap-width: 90;",
            "  shield-icon-fill: #ffffff;",
            "  shield-icon-background-fill: [color];",
            "  shield-icon-background-width: 22;",
            "  shield-icon-background-height: 22;",
            "  shield-icon-background-radius: 11;",
            "  shield-icon-background-border-fill: #ffffff;",
            "  shield-icon-background-border-width: 1.5;",
            "  shield-anchors: '" + (freeSide ? "right,left,top,bottom" : "right") + "';",
            "  shield-text-optional: true;",
            "  shield-text-dx: 4;",
            "  shield-text-horizontal-alignment: 'auto';",
            "}",
            // A glyph of Massif's icon set (styles/massif/carto/icons-glyph), a distance field the style tints.
            "#poi[icon != null] {",
            "  shield-file: 'icons-glyph/' + [icon] + '.png';",
            "  shield-sdf: true;",
            "  shield-unlock-image: true;",
            "  shield-image-scale: 0.2;",
            "}",
            // A glyph of a font. The icon face takes ONE name, not a list.
            "#poi[letter != null] {",
            "  shield-icon-name: [letter];",
            "  shield-icon-face-name: 'Arial Bold';",
            "  shield-icon-size: 15;",
            "  shield-placement-priority: 1;",
            "}",
            "#peak {",
            "  marker-width: 7;",
            "  marker-fill: #3f2a1d;",
            "  marker-line-color: #ffffff;",
            "  marker-line-width: 1.5;",
            "}",
            "#peak::name {",
            "  text-name: [name];",
            "  text-secondary-name: [ele] + ' m';",
            "  text-secondary-scale: 0.8;",
            "  text-face-name: @bold;",
            "  text-size: 12;",
            "  text-fill: #3f2a1d;",
            "  text-placement-priority: [ele];",
            "  text-background-fill: #fffaf0;",
            "  text-background-opacity: 0.9;",
            "  text-background-radius: 4;",
            "  text-background-border-fill: #3f2a1d;",
            "  text-background-border-width: 1;",
            callouts
                ? String.join("\n",
                    "  text-placement: callout;",
                    "  text-callout-offset: 22;",
                    "  text-callout-step: 18;",
                    "  text-callout-max-rows: 5;",
                    "  text-callout-line-anchor: bottom;",
                    "  text-callout-line-width: 1.5;")
                : "  text-dy: -18;",
            "}");
    }

    private static String poi(String name, String icon, String letter, String color, double lon, double lat) {
        return point("\"name\":\"" + name + "\",\"color\":\"" + color + "\","
            + (icon != null ? "\"icon\":\"" + icon + "\"" : "\"letter\":\"" + letter + "\""), lon, lat);
    }

    private static String peak(String name, int ele, double lon, double lat) {
        return point("\"name\":\"" + name + "\",\"ele\":" + ele, lon, lat);
    }

    private static String point(String properties, double lon, double lat) {
        return "{\"type\":\"Feature\",\"properties\":{" + properties + "},"
            + "\"geometry\":{\"type\":\"Point\",\"coordinates\":[" + lon + "," + lat + "]}}";
    }

    private static String collection(String... features) {
        return "{\"type\":\"FeatureCollection\",\"features\":[" + String.join(",", features) + "]}";
    }
}
