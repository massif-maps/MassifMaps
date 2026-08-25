package com.massifmaps.MassifDemo.examples.annotations;

import com.massifmaps.MassifDemo.examples.ExampleHost;
import com.massifmaps.MassifDemo.examples.ExampleInfo;
import com.massifmaps.MassifDemo.examples.MapExample;
import com.massifmaps.MassifDemo.examples.Sections;
import com.massifmaps.api.MapEvents;
import com.massifmaps.api.MassifMap;
import com.massifmaps.api.MassifObject;
import com.massifmaps.api.Spec;
import com.massifmaps.api.Position;

/**
 * Markers and a popup, both described entirely by JSON - no style builder anywhere.
 */
@ExampleInfo(
    id = "markers",
    title = "Markers and popups",
    description = "addMarker takes a spec that carries the position AND the style. "
                + "Tap a summit for a balloon popup; tap the map to dismiss it.",
    section = Sections.ANNOTATIONS,
    order = 10)
public class MarkersExample extends MapExample {

    /** OSM's tile usage policy: a real app identifies itself, or the tiles come back as 403s. */
    private static final String UA = "MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

    /** name, lon, lat, metres */
    private static final Object[][] SUMMITS = {
        { "Mont Blanc",      6.8652, 45.8326, 4808 },
        { "Grandes Jorasses", 6.9866, 45.8697, 4208 },
        { "Aiguille Verte",  6.9256, 45.9036, 4122 },
        { "Mont Dolent",     7.0575, 45.9264, 3820 },
    };

    private MassifObject popup;

    @Override
    public void onStart(ExampleHost host) {
        final MassifMap map = host.map();

        map.addLayer("basemap", Spec.of("raster")
            // Cached on disk in front of the server: OSM's tiles are a free service run on
            // donations, and a demo that gets panned around re-fetches the same ones every run.
            .set("source", Spec.of("persistent-cache")
                .set("databasePath", host.cachePath("osm-raster.db"))
                .set("capacity", 100 * 1024 * 1024)
                .set("source", Spec.of("http")
                    .set("url", "https://tile.openstreetmap.org/{z}/{x}/{y}.png")
                    .set("maxZoom", 19)
                    // OSM's tile policy REQUIRES an identifying User-Agent; without one the server
                    // answers 403 and every tile comes back as an error image.
                    .set("HTTPHeaders", Spec.object().set("User-Agent", UA)))));

        // One style object shared by every marker - what matters once there are thousands of them.
        // A "style" key that is a STRING is looked up by id; an object would be built inline.
        MassifObject pin = map.elements().style("pin", Spec.of("marker")
            .set("size", 26)
            .set("color", 0xFFE5484D)
            .set("clickSize", 40));

        for (Object[] summit : SUMMITS) {
            map.addMarker(Spec.of("marker")
                .set("position", new double[] { (Double) summit[1], (Double) summit[2] })
                .set("style", pin.id()));
        }

        map.camera().moveTo(new Position(6.94, 45.87), 10.6f);

        // consumeClick, not onClick: a marker tap has to CLAIM the gesture, or the map's own
        // onClick below fires for the same tap and dismisses the popup as it opens.
        map.elements().consumeClick(new MapEvents.ConsumingHandler<MapEvents.ElementClick>() {
            @Override
            public boolean handle(MapEvents.ElementClick e) {
                show(map, e.position());
                return true;
            }
        });
        map.onClick(new MapEvents.Handler<MapEvents.Click>() {
            @Override
            public void handle(MapEvents.Click e) {
                dismiss(map);
            }
        });
        host.caption("Tap a pin. Everything here - marker, style, popup - is a JSON spec.");
    }

    /** A balloon at a position, built the same way a marker is. */
    private void show(MassifMap map, Position position) {
        dismiss(map);
        String name = "Summit";
        int metres = 0;
        for (Object[] summit : SUMMITS) {
            if (near(position, (Double) summit[1], (Double) summit[2])) {
                name = (String) summit[0];
                metres = (Integer) summit[3];
            }
        }
        popup = map.addPopup(Spec.of("balloon")
            .set("position", new double[] { position.lng, position.lat })
            .set("title", name)
            .set("description", metres + " m")
            .set("style", Spec.of("balloon")
                .set("cornerRadius", 6)
                .set("leftColor", 0xFFE5484D)
                .set("titleFontSize", 14)
                .set("descriptionFontSize", 12)));
    }

    private void dismiss(MassifMap map) {
        if (popup != null) {
            map.elements().remove(popup);
            popup.close();
            popup = null;
        }
    }

    private static boolean near(Position position, double lon, double lat) {
        return Math.abs(position.lng - lon) < 1e-4 && Math.abs(position.lat - lat) < 1e-4;
    }
}
