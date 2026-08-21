package com.massifmaps.api;

import com.massifmaps.layers.Layer;
import com.massifmaps.ui.MapEventListener;
import com.massifmaps.ui.MapView;

/**
 * A map, as an app talks to it.
 *
 * <pre>
 * MassifMap map = MassifMap.attach(mapView).eventProjection("EPSG:4326");
 * map.fog().set("rangeStart", 2.5).set("rangeEnd", 8);
 * map.add(Massif.layer("base", Spec.of("vector").set("source", "osm")));
 * map.onClick(e -&gt; Log.i(TAG, "clicked " + e.position()));
 * </pre>
 *
 * The MapView stays the app's: this attaches to one rather than replacing it, and the object API
 * keeps working alongside. Attaching twice with the same id gives the same map back.
 */
public final class MassifMap implements AutoCloseable {

    private static final String KIND = "map";

    private final MapView view;
    private final MassifObject options;
    private final MapCamera camera;
    private String eventProjection = "";
    private boolean bridged;

    private MassifMap(MapView view, MassifObject options) {
        this.view = view;
        this.options = options;
        this.camera = new MapCamera(view);
    }

    /** Attaches to a map view under the id "main". */
    public static MassifMap attach(MapView view) {
        return attach(view, "main");
    }

    /**
     * Attaches under a given id, which is what makes more than one map work: every handle, event
     * and layer belongs to the map it was registered under.
     */
    public static MassifMap attach(MapView view, String id) {
        if (view == null) {
            throw new IllegalArgumentException("Null map view");
        }
        int handle = MassifApi.findObject(KIND, id);
        if (handle == 0) {
            handle = MassifApi.registerOptions(KIND, id, view.getOptions());
            if (handle == 0) {
                throw new MassifException("Cannot attach map '" + id + "'");
            }
        }
        return new MassifMap(view, new MassifObject(handle, KIND, id));
    }

    /** The view this is attached to. Nothing here replaces it. */
    public MapView view() {
        return view;
    }

    /** The map's Options, as a facade object - every option the SDK has, by path. */
    public MassifObject options() {
        return options;
    }

    public MapCamera camera() {
        return camera;
    }

    // --- option groups -------------------------------------------------------------------------

    /*
     * One accessor per options class, not per option. The names below are the whole list and it
     * does not grow with the SDK; the properties inside them are data.
     */

    public PropertyGroup fog() {
        return options.group("fogOptions");
    }

    public PropertyGroup sky() {
        return options.group("skyOptions");
    }

    public PropertyGroup terrain() {
        return options.group("terrainOptions");
    }

    public PropertyGroup light() {
        return options.group("lightOptions");
    }

    // --- layers --------------------------------------------------------------------------------

    /** Adds a layer built with {@link Massif#layer} to the top of the stack. */
    public MassifLayer add(MassifLayer layer) {
        view.getLayers().add(layer.layer());
        return attached(layer);
    }

    /** Adds it at a given position. 0 is the bottom. */
    public MassifLayer add(MassifLayer layer, int index) {
        view.getLayers().insert(index, layer.layer());
        return attached(layer);
    }

    /** Builds and adds in one step, which is what an app writes most of the time. */
    public MassifLayer addLayer(String id, Spec spec) {
        return add(Massif.layer(id, spec));
    }

    /** A layer already on this map, by id, or null. */
    public MassifLayer layer(String id) {
        int handle = MassifApi.findObject("layer", id);
        return handle == 0 ? null : new MassifLayer(handle, id, this);
    }

    public MassifMap remove(MassifLayer layer) {
        view.getLayers().remove(layer.layer());
        return this;
    }

    /** How many layers the map is showing. */
    public int layerCount() {
        return view.getLayers().count();
    }

    /** A layer by stack position, for the ones the app did not build through the registry. */
    public Layer rawLayer(int index) {
        return view.getLayers().get(index);
    }

    // --- events --------------------------------------------------------------------------------

    /**
     * The projection this map's event positions are read in - "EPSG:4326" for lon/lat.
     *
     * Set once, and every handler subscribed afterwards gets positions already converted, instead
     * of each one repeating the same toWgs84 chain. Empty leaves them in the map's own projection.
     */
    public MassifMap eventProjection(String name) {
        eventProjection = name == null ? "" : name;
        return this;
    }

    String eventProjection() {
        return eventProjection;
    }

    public Subscription onClick(MapEvents.Handler<MapEvents.Click> handler) {
        return on(MapEvents.CLICKED, handler, MassifObject.EventKind.CLICK);
    }

    public Subscription onMove(MapEvents.Handler<MapEvents.Event> handler) {
        return on(MapEvents.MOVED, handler, MassifObject.EventKind.PLAIN);
    }

    /** Fires once the map has stopped moving and every visible tile has settled. */
    public Subscription onIdle(MapEvents.Handler<MapEvents.Event> handler) {
        return on(MapEvents.IDLE, handler, MassifObject.EventKind.PLAIN);
    }

    public Subscription onStable(MapEvents.Handler<MapEvents.Event> handler) {
        return on(MapEvents.STABLE, handler, MassifObject.EventKind.PLAIN);
    }

    /**
     * Pans, zooms, rotations and tilts the user drove.
     *
     * Coalesced: this fires per frame while a finger is down, so a pending one is replaced rather
     * than queued, and a slow handler cannot flood the loop.
     */
    public Subscription onInteraction(MapEvents.Handler<MapEvents.Interaction> handler) {
        bridge();
        return options.subscribe(MapEvents.INTERACTION, handler, null, MassifObject.Delivery.UI,
                                 true, eventProjection, MassifObject.EventKind.INTERACTION);
    }

    /** Every handler on this map, gone. Layer subscriptions are the layer's own. */
    public MassifMap offAll() {
        options.offAll();
        return this;
    }

    /**
     * Detaches: removes every handler and drops the map's id. The MapView is untouched and keeps
     * working through the object API.
     */
    @Override
    public void close() {
        options.offAll();
        if (options.id != null) {
            MassifApi.unregisterObject(KIND, options.id);
        }
    }

    // --- internals -----------------------------------------------------------------------------

    private <E> Subscription on(String event, MapEvents.Handler<E> handler,
                                MassifObject.EventKind kind) {
        bridge();
        return options.subscribe(event, handler, null, MassifObject.Delivery.UI, false,
                                 eventProjection, kind);
    }

    /**
     * Installs the map's event bridge on first subscription, chaining to whatever listener the app
     * already had - a single listener slot means adopting the facade would otherwise silently
     * disconnect its existing handlers.
     */
    private void bridge() {
        if (bridged) {
            return;
        }
        MapEventListener chained = view.getMapEventListener();
        view.setMapEventListener(MassifApi.createEventBridge(options.handle, chained));
        bridged = true;
    }

    private MassifLayer attached(MassifLayer layer) {
        return layer.id() != null ? new MassifLayer(layer.handle(), layer.id(), this) : layer;
    }
}
