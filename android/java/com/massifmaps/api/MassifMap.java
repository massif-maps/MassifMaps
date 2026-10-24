package com.massifmaps.api;

import com.massifmaps.ui.MapViewInterface;

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
 * The view owns the native map and registers it under its id (see MapView.getMapId), so there
 * is nothing to adopt: this wraps the handles the view already has. Attaching twice gives the
 * same map back.
 */
public final class MassifMap implements AutoCloseable {

    private static final String KIND = "map";
    private static final String VIEW_KIND = "view";
    private static final String LAYERS_KIND = "layers";

    private final MapViewInterface view;
    private final MassifObject options;
    private final MassifObject layers;
    private final MapCamera camera;
    /** Kind and id of everything this map built, so close() releases it. */
    private final java.util.List<String[]> owned = new java.util.ArrayList<>();
    private MassifElements elements;
    private String eventProjection = "";

    private MassifMap(MapViewInterface view, MassifObject options, MassifObject layers, MassifObject baseView) {
        this.view = view;
        this.options = options;
        this.layers = layers;
        this.camera = new MapCamera(baseView);
    }

    /**
     * Attaches to a map view, under the id the view was created with.
     * @throws MassifException when the view has been deleted.
     */
    public static MassifMap attach(MapViewInterface view) {
        if (view == null) {
            throw new IllegalArgumentException("Null map view");
        }
        int viewHandle = view.getViewHandle();
        int mapHandle = view.getMapHandle();
        int layersHandle = view.getLayersHandle();
        if (viewHandle == 0 || mapHandle == 0 || layersHandle == 0) {
            throw new MassifException("The map view '" + view.getMapId() + "' has been deleted");
        }
        installUiDispatcher();
        String id = view.getMapId();
        return new MassifMap(view, new MassifObject(mapHandle, KIND, id), new MassifObject(layersHandle, LAYERS_KIND, id),
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

    /** The Android view this is attached to: every MapViewInterface is a View. */
    public android.view.View view() {
        return (android.view.View) view;
    }

    /** The map's layer list as a facade object. */
    MassifObject layersObject() {
        return layers;
    }

    /** The map's Options, as a facade object - every option the SDK has, by path. */
    public MassifObject options() {
        return options;
    }

    public MapCamera camera() {
        return camera;
    }

    // --- options, by path ----------------------------------------------------------------------

    /*
     * The map IS its Options as far as the facade is concerned, so a path reads off it directly -
     * the same shape a layer and a source already had. The readable spellings come from the alias
     * table: "fog.rangeStart" is "fogOptions.rangeStart".
     */

    /** <pre>map.set("fog.rangeStart", 2.5).set("zoomRange", new double[] { 3, 17 });</pre> */
    public MassifMap set(String path, Object value) {
        options.set(path, value);
        return this;
    }

    /** Several at once, in one crossing. @see MassifObject#apply */
    public MassifMap apply(Spec values) {
        options.apply(values);
        return this;
    }

    public interface CaptureCallback {
        /** @param png The PNG bytes, or null when the capture failed. Called on the UI thread. */
        void onCapture(byte[] png);
    }

    /**
     * Captures the next rendered frame as PNG, through the facade. @param waitWhileUpdating holds it
     * until the tiles have settled.
     * @return A call id for {@link MassifObject#cancel(int)}.
     */
    public int capture(boolean waitWhileUpdating, final CaptureCallback callback) {
        return camera().renderer().callAsync("capturePNG", new MassifObject.Callback() {
            @Override
            public void onResult(MassifObject result) {
                callback.onCapture(result != null ? result.data("") : null);
            }
        }, waitWhileUpdating);
    }

    /**
     * A method on the map or on an object under it, by path: <pre>map.call("light.setSunPositionFromTime", 2026, 3, 20, 12, 0, 45.0, 5.7).close();</pre>
     * The result is the caller's to close.
     * @see MassifObject#call
     */
    public MassifObject call(String method, Object... args) {
        return options.call(method, args);
    }

    public double getDouble(String path, double defaultValue) {
        return options.getDouble(path, defaultValue);
    }

    public long getLong(String path, long defaultValue) {
        return options.getLong(path, defaultValue);
    }

    public boolean getBool(String path, boolean defaultValue) {
        return options.getBool(path, defaultValue);
    }

    /** @param defaultValue May be null. */
    public String getString(String path, String defaultValue) {
        return options.getString(path, defaultValue);
    }

    /** A view scoped to a path prefix - what {@link #fog()} and its siblings are made of. */
    public PropertyGroup group(String prefix) {
        return options.group(prefix);
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
        layers.call("add", layer.handle());
        return attached(layer);
    }

    /** Takes every layer off the map, the ones this map did not build included. Their ids stay registered. */
    public MassifMap clearLayers() {
        layers.call("clear").close();
        return this;
    }

    /** Adds it at a given position. 0 is the bottom. */
    public MassifLayer add(MassifLayer layer, int index) {
        layers.call("insert", index, layer.handle());
        return attached(layer);
    }

    /** Builds and adds in one step, which is what an app writes most of the time. */
    public MassifLayer addLayer(String id, Spec spec) {
        MassifLayer layer = add(Massif.layer(id, spec));
        owned.add(new String[] { "layer", id });
        return layer;
    }

    /** Builds a layer this map owns without putting it on the stack: for {@link #add(MassifLayer, int)} or a cached layer. */
    public MassifLayer buildLayer(String id, Spec spec) {
        MassifLayer layer = new MassifLayer(Massif.create("layer", id, spec), id, this);
        owned.add(new String[] { "layer", id });
        return layer;
    }

    /**
     * Takes a layer this map built off the map and RELEASES its id, so the same id can be built
     * again. `remove(MassifLayer)` only takes it off the stack: the id stays registered, and
     * building it a second time fails with RESULT_DUPLICATE_ID - which is what an app swapping a
     * basemap between two styles hits first.
     * @param id The layer id, as given to addLayer.
     * @return True if a layer of that id was on this map.
     */
    public boolean removeLayer(String id) {
        boolean owns = false;
        for (java.util.Iterator<String[]> it = owned.iterator(); it.hasNext(); ) {
            String[] object = it.next();
            if ("layer".equals(object[0]) && object[1].equals(id)) {
                it.remove();
                owns = true;
            }
        }
        MassifLayer layer = layer(id);
        if (layer != null) {
            layers.call("remove", layer.handle());
        }
        if (owns || layer != null) {
            MassifApi.unregisterObject("layer", id);
        }
        return layer != null;
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

    /** A layer already on this map, by id, or null. */
    public MassifLayer layer(String id) {
        int handle = MassifApi.findObject("layer", id);
        return handle == 0 ? null : new MassifLayer(handle, id, this);
    }

    public MassifMap remove(MassifLayer layer) {
        layers.call("remove", layer.handle());
        return this;
    }

    /** How many layers the map is showing. */
    public int layerCount() {
        return (int) layers.getLong("count", 0);
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
        return options.subscribe(MapEvents.INTERACTION, handler, null, MassifObject.Delivery.UI,
                                 true, eventProjection, MassifObject.EventKind.INTERACTION);
    }

    /** Every handler on this map, gone. Layer subscriptions are the layer's own. */
    public MassifMap offAll() {
        options.offAll();
        return this;
    }

    /**
     * Detaches: removes every handler and takes off and drops every layer this map BUILT.
     *
     * Releasing what it built is what makes ids reusable - a screen that opens, builds "basemap"
     * and closes can be opened again with a different spec under the same name. Layers merely
     * added with {@link #add} are the caller's and are left alone. The MapView is untouched, and so
     * are the ids it registered: they go with {@link MapViewInterface#delete()}.
     */
    @Override
    public void close() {
        options.offAll();
        for (String[] object : owned) {
            if ("layer".equals(object[0])) {
                MassifLayer layer = layer(object[1]);
                if (layer != null) {
                    layers.call("remove", layer.handle());
                }
            }
            MassifApi.unregisterObject(object[0], object[1]);
        }
        owned.clear();
        elements = null;
    }

    // --- internals -----------------------------------------------------------------------------

    private <E> Subscription on(String event, MapEvents.Handler<E> handler,
                                MassifObject.EventKind kind) {
        return options.subscribe(event, handler, null, MassifObject.Delivery.UI, false,
                                 eventProjection, kind);
    }

    private MassifLayer attached(MassifLayer layer) {
        return layer.id() != null ? new MassifLayer(layer.handle(), layer.id(), this) : layer;
    }
}
