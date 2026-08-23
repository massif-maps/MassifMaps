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
    private static final String VIEW_KIND = "view";

    private final MapView view;
    private final MassifObject options;
    private final MapCamera camera;
    /** Kind and id of everything this map built, so close() releases it. */
    private final java.util.List<String[]> owned = new java.util.ArrayList<>();
    private MassifElements elements;
    private String eventProjection = "";
    private boolean bridged;

    private MassifMap(MapView view, MassifObject options, MassifObject baseView) {
        this.view = view;
        this.options = options;
        this.camera = new MapCamera(baseView);
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
            handle = MassifInterop.adopt(KIND, id, view.getOptions());
            if (handle == 0) {
                throw new MassifException("Cannot attach map '" + id + "'");
            }
        }
        // The map view is adopted too, under its own kind: it is what carries the CAMERA, and
        // going through the facade is what gives moveTo the map's projection.
        int viewHandle = MassifApi.findObject(VIEW_KIND, id);
        if (viewHandle == 0) {
            viewHandle = MassifInterop.adopt(VIEW_KIND, id, view.getBaseMapView());
            if (viewHandle == 0) {
                throw new MassifException("Cannot attach the map view of '" + id + "'");
            }
        }
        installUiDispatcher();
        return new MassifMap(view, new MassifObject(handle, KIND, id),
                             new MassifObject(viewHandle, VIEW_KIND, id));
    }

    /**
     * Sends queued handlers to the main looper, once per process.
     *
     * Without it a subscription that asked for UI delivery runs INLINE on the thread that produced
     * the event - a GL or tile thread - and the facade warns once. Every handler in this API is
     * documented as main-thread, so the sugar owes the hop.
     */
    private static void installUiDispatcher() {
        if (dispatcher != null) {
            return;
        }
        final android.os.Handler main = new android.os.Handler(android.os.Looper.getMainLooper());
        dispatcher = new UiDispatcher() {
            @Override
            public void post() {
                main.post(new Runnable() {
                    @Override
                    public void run() {
                        MassifApi.drain();
                    }
                });
            }
        };
        MassifApi.setUiDispatcher(dispatcher);
    }

    /** Held for the life of the process: the C++ side keeps only a raw pointer to the director. */
    private static UiDispatcher dispatcher;

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

    /**
     * Turns 3D terrain on from an elevation source, and returns its options for tuning.
     *
     * <pre>
     * map.terrain(Spec.of("terrain").set("source", Spec.of("http")
     *         .set("url", "https://tiles.example.com/{z}/{x}/{y}.webp")
     *         .set("encoding", "terrarium")))
     *    .set("exaggeration", 1.2);
     * </pre>
     *
     * The elevation decoder comes from the source's own `encoding`, so nothing here names one.
     */
    public PropertyGroup terrain(Spec spec) {
        return optionGroup("terrainOptions", "terrain", spec);
    }

    /**
     * Fog, on the mapbox model. Independent of the terrain - it fogs a plain 2D map too.
     *
     * The range is in MULTIPLES of the camera-to-focus distance rather than metres, so one pair
     * of values holds at every zoom.
     */
    public PropertyGroup fog(Spec spec) {
        return optionGroup("fogOptions", "fog", spec);
    }

    /** The sky dome behind the map. */
    public PropertyGroup sky(Spec spec) {
        return optionGroup("skyOptions", "sky", spec);
    }

    /** Sun direction and colour, which the terrain and 3D buildings shade from. */
    public PropertyGroup light(Spec spec) {
        return optionGroup("lightOptions", "light", spec);
    }

    /**
     * Builds one of the Options sub-objects and hangs it on the map.
     *
     * The no-argument {@link #fog()} / {@link #sky()} accessors only work once something is there:
     * Options starts with those properties EMPTY, and writing through an empty one is an error
     * rather than a silent no-op.
     */
    private PropertyGroup optionGroup(String property, String type, Spec spec) {
        String id = (options.id == null ? "map" : options.id) + "." + type;
        options.set(property, object("options", id, spec == null ? Spec.of(type) : spec));
        return options.group(property);
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
        MassifLayer layer = add(Massif.layer(id, spec));
        owned.add(new String[] { "layer", id });
        return layer;
    }

    /** Builds an object of any kind, owned by this map. @see Massif#object */
    public MassifObject object(String kind, String id, Spec spec) {
        MassifObject object = Massif.object(kind, id, spec);
        owned.add(new String[] { kind, id });
        return object;
    }

    /**
     * Builds a source this map owns, so it is released with the map rather than living on under
     * its id. {@link Massif#source} is the one to use for a source shared between maps.
     */
    public MassifSource source(String id, Spec spec) {
        MassifSource source = Massif.source(id, spec);
        owned.add(new String[] { "source", id });
        return source;
    }

    /**
     * The same for a style. Worth an id whenever the app talks to it later - a style parameter, a
     * theme switch - because an object property cannot yet be read back as a handle.
     */
    public MassifObject style(String id, Spec spec) {
        MassifObject style = Massif.style(id, spec);
        owned.add(new String[] { "style", id });
        return style;
    }

    // --- markers and popups --------------------------------------------------------------------

    /**
     * The map's own markers and popups, on a layer created the first time this is called.
     *
     * @see MassifElements
     */
    public MassifElements elements() {
        return elements(null);
    }

    /**
     * The same, with the source spec chosen - which is how a map that does NOT work in lon/lat
     * places its markers correctly:
     *
     * <pre>map.elements(Spec.of("local").set("projection", Spec.of("EPSG:3857")));</pre>
     *
     * Only the first call builds; later ones return what it built.
     */
    public MassifElements elements(Spec sourceSpec) {
        if (elements == null) {
            elements = new MassifElements(this, (options.id == null ? "map" : options.id)
                                                + ".elements", sourceSpec);
        }
        return elements;
    }

    /** Registers an id this map is responsible for releasing. Used by {@link MassifElements}. */
    void own(String kind, String id) {
        owned.add(new String[] { kind, id });
    }

    /**
     * Adds a marker. The spec carries its position and its style, inline or by id:
     *
     * <pre>
     * map.addMarker(Spec.of("marker")
     *     .set("position", new double[] { 6.865, 45.832 })
     *     .set("style", Spec.of("marker").set("size", 30).set("color", 0xFFE53935)));
     * </pre>
     */
    public MassifObject addMarker(Spec spec) {
        return elements().add(spec);
    }

    /** The same for a balloon popup - a label anchored to a position, with a title and a body. */
    public MassifObject addPopup(Spec spec) {
        return elements().add(spec);
    }

    /**
     * Adopts the layer at a stack position, so a map built with the object API can be driven
     * through the facade without rebuilding it.
     *
     * @return The wrapper, or the existing one when the id is already registered.
     */
    public MassifLayer adoptLayer(String id, int index) {
        MassifLayer existing = layer(id);
        if (existing != null) {
            return existing;
        }
        Layer target = index >= 0 && index < layerCount() ? view.getLayers().get(index) : null;
        if (target == null) {
            return null;
        }
        int handle = MassifInterop.adopt("layer", id, target);
        return handle == 0 ? null : new MassifLayer(handle, id, this);
    }

    /**
     * Adopts the FIRST layer of a kind, which is what an app usually means by "the base map".
     * @param type A class from com.massifmaps.layers, e.g. VectorTileLayer.class.
     */
    public MassifLayer adoptFirst(String id, Class<? extends Layer> type) {
        for (int index = 0; index < layerCount(); index++) {
            if (type.isInstance(view.getLayers().get(index))) {
                return adoptLayer(id, index);
            }
        }
        return null;
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

    // --- screen and map ------------------------------------------------------------------------

    /** Where a touch point is on the map, in WGS84. */
    public Position screenToMap(float x, float y) {
        return camera.screenToMap(x, y);
    }

    /** And the other way, for placing a native view over a coordinate. */
    public ScreenPoint mapToScreen(Position pos) {
        return camera.mapToScreen(pos);
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

    /** Every camera change, whatever caused it. Fires well above frame rate during a drag. */
    public Subscription onMove(MapEvents.Handler<MapEvents.Move> handler) {
        return onMove(handler, 0);
    }

    /**
     * The same, delivering at most one event per {@code throttleMs}.
     *
     * A drag raises this 47 to 159 times a second, and a handler that repositions a view or
     * updates a readout does not need every one. Events inside the window are DROPPED, not
     * queued - the last one is not delivered late, because the payload does not outlive the emit.
     * For work that should happen once the movement ENDS, use {@link #onStable} instead.
     *
     * @param throttleMs The window in milliseconds; 0 delivers every event.
     */
    public Subscription onMove(MapEvents.Handler<MapEvents.Move> handler, int throttleMs) {
        bridge();
        return options.subscribe(MapEvents.MOVED, handler, null, MassifObject.Delivery.UI, false,
                                 eventProjection, MassifObject.EventKind.MOVE, throttleMs);
    }

    /**
     * Fires when the renderer has nothing left to draw. Tiles may still be loading - this is the
     * end of the frame queue, not of the data.
     */
    public Subscription onIdle(MapEvents.Handler<MapEvents.Event> handler) {
        return on(MapEvents.IDLE, handler, MassifObject.EventKind.PLAIN);
    }

    /**
     * Fires once when a movement ENDS - animations finished, fingers lifted, inertia died out.
     *
     * Once per movement, with the reason that caused it: a tap that did not move the camera does
     * not fire it at all. This is the one to hang "the map settled, refresh my data" on.
     */
    public Subscription onStable(MapEvents.Handler<MapEvents.Move> handler) {
        return on(MapEvents.STABLE, handler, MassifObject.EventKind.MOVE);
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
     * Detaches: removes every handler, takes off and drops every layer this map BUILT, and drops
     * the map's own id.
     *
     * Releasing what it built is what makes ids reusable - a screen that opens, builds "basemap"
     * and closes can be opened again with a different spec under the same name. Layers merely
     * added with {@link #add} are the caller's and are left alone. The MapView is untouched and
     * keeps working through the object API.
     */
    @Override
    public void close() {
        options.offAll();
        for (String[] object : owned) {
            if ("layer".equals(object[0])) {
                MassifLayer layer = layer(object[1]);
                if (layer != null) {
                    view.getLayers().remove(layer.layer());
                }
            }
            MassifApi.unregisterObject(object[0], object[1]);
        }
        owned.clear();
        elements = null;
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
        view.setMapEventListener(MassifInterop.createEventBridge(options.handle, chained));
        bridged = true;
    }

    private MassifLayer attached(MassifLayer layer) {
        return layer.id() != null ? new MassifLayer(layer.handle(), layer.id(), this) : layer;
    }
}
