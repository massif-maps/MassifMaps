package com.massifmaps.api;

/**
 * The map's own markers and popups.
 *
 * The mapbox-style half of the API - `addMarker`, `remove`, `clear` - with the SDK's style builders
 * kept out of it: an element and its style are BOTH described by a spec, so an app that wants a
 * bigger pin changes a number in JSON rather than reaching for MarkerStyleBuilder.
 *
 * <pre>
 * map.addMarker(Spec.of("marker")
 *     .set("position", new double[] { 6.865, 45.832 })
 *     .set("style", Spec.of("marker").set("size", 30).set("color", 0xFFE53935)));
 * </pre>
 *
 * The layer and the source behind this are created on first use and closed with the map. They are
 * ordinary registered objects - {@link #layer()} reaches the layer, so ordering, opacity and
 * visibility are the same properties as on any other.
 */
public final class MassifElements {

    /**
     * Where the elements' positions are read. lon/lat, matching the rest of this API - an app
     * whose map works in metres passes its own source spec to {@link MassifMap#elements(Spec)}.
     */
    static final String DEFAULT_PROJECTION = "EPSG:4326";

    private final MassifMap map;
    private final String id;
    private final Spec sourceSpec;
    private MassifObject source;
    private MassifLayer layer;
    private int counter;

    MassifElements(MassifMap map, String id, Spec sourceSpec) {
        this.map = map;
        this.id = id;
        this.sourceSpec = sourceSpec != null ? sourceSpec
            : Spec.of("local").set("projection", Spec.of(DEFAULT_PROJECTION));
    }

    /**
     * Adds an element - a "marker", a "balloon". The spec's "style" may name a style registered
     * earlier or carry one inline, which is what most apps write.
     *
     * @return The element, for {@link #remove}. Its properties are readable and writable by path.
     */
    public MassifObject add(Spec spec) {
        build();
        String elementId = id + ".el" + (++counter);
        MassifObject element = new MassifObject(Massif.create("element", elementId, spec),
                                                "element", elementId);
        map.own("element", elementId);
        MassifObject result = source.call("add", element.handle());
        if (result != null) {
            result.close();
        }
        return element;
    }

    /**
     * Registers a style under an id so many elements can share it - one style object rather than
     * one per marker, which is what matters once there are thousands.
     */
    public MassifObject style(String styleId, Spec spec) {
        String fullId = id + "." + styleId;
        map.own("elementstyle", fullId);
        return new MassifObject(Massif.create("elementstyle", fullId, spec), "elementstyle", fullId);
    }

    /** Removes one element. It stays registered until it is closed. */
    public boolean remove(MassifObject element) {
        if (source == null || element == null) {
            return false;
        }
        MassifObject result = source.call("remove", element.handle());
        try {
            return result != null && result.getBool("", false);
        } finally {
            if (result != null) {
                result.close();
            }
        }
    }

    /** Removes every element this has added. */
    public MassifElements clear() {
        if (source != null) {
            MassifObject result = source.call("clear");
            if (result != null) {
                result.close();
            }
        }
        return this;
    }

    /** Clicks on the elements themselves, with the element's position on the payload. */
    public Subscription onClick(MapEvents.Handler<MapEvents.ElementClick> handler) {
        build();
        return layer.onElementClick(handler);
    }

    /**
     * The same, claiming the tap so the map's own onClick does not also fire - which is what an
     * app wants whenever "tap a marker" and "tap the map" mean different things.
     */
    public Subscription consumeClick(MapEvents.ConsumingHandler<MapEvents.ElementClick> handler) {
        build();
        return layer.consumeElementClick(handler);
    }

    /** The layer they are drawn on, for opacity, visibility and ordering. */
    public MassifLayer layer() {
        build();
        return layer;
    }

    /** The source holding them, for anything the facade reaches on a local source. */
    public MassifObject source() {
        build();
        return source;
    }

    /** Built on first use, so a map that never adds one pays nothing. */
    private void build() {
        if (layer != null) {
            return;
        }
        source = new MassifObject(Massif.create("source", id, sourceSpec), "source", id);
        map.own("source", id);
        layer = map.addLayer(id, Spec.of("elements").set("source", id));
    }
}
