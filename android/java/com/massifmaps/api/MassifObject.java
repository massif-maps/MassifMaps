package com.massifmaps.api;

import com.massifmaps.core.BinaryData;
import com.massifmaps.core.MapBounds;
import com.massifmaps.core.MapPos;

import org.json.JSONArray;

/**
 * A registered object, addressed by handle.
 *
 * Properties, methods and events on one class, so an app never sees a handle or a result code.
 * Anything unsupported throws {@link MassifException} rather than returning a number to check.
 *
 * <pre>
 * MassifSource osm = Massif.source("osm", Spec.of("http").set("url", url));
 * osm.set("maxZoom", 14);
 * byte[] tile = osm.loadTile(8467, 5852, 14);
 * </pre>
 */
public class MassifObject implements AutoCloseable {

    final int handle;
    /** The kind and id it was registered under, when it came from the registry rather than a call. */
    final String kind;
    final String id;

    MassifObject(int handle) {
        this(handle, null, null);
    }

    MassifObject(int handle, String kind, String id) {
        this.handle = handle;
        this.kind = kind;
        this.id = id;
    }

    static MassifObject wrap(int handle) {
        return handle == 0 ? null : new MassifObject(handle);
    }

    /** The id it is registered under, or null for a call result, which has none an app chose. */
    public String id() {
        return id;
    }

    /** The raw handle, for the flat API underneath. Rarely needed. */
    public int handle() {
        return handle;
    }

    /** Whether the object is still registered. False after close, or after its id was destroyed. */
    public boolean isValid() {
        return handle != 0 && MassifApi.isValid(handle);
    }

    // --- properties ----------------------------------------------------------------------------

    /**
     * Sets a property. The path may walk object properties - "fogOptions.rangeStart".
     *
     * @param value A boolean, a number, a String, a MapPos or a MapBounds.
     * @throws MassifException When the path does not resolve, or the property is read-only.
     */
    public MassifObject set(String path, Object value) {
        int result;
        if (value instanceof Boolean) {
            result = MassifApi.setBool(handle, path, (Boolean) value);
        } else if (value instanceof MapPos) {
            result = MassifApi.setString(handle, path, Values.fromPos((MapPos) value));
        } else if (value instanceof MapBounds) {
            MapBounds bounds = (MapBounds) value;
            result = MassifApi.setString(handle, path,
                "[" + Values.fromPos(bounds.getMin()) + "," + Values.fromPos(bounds.getMax()) + "]");
        } else if (value instanceof Number) {
            Number number = (Number) value;
            result = number instanceof Double || number instanceof Float
                ? MassifApi.setFloat(handle, path, number.doubleValue())
                : MassifApi.setInt(handle, path, number.longValue());
        } else {
            result = MassifApi.setString(handle, path, String.valueOf(value));
        }
        MassifException.check(result, "set", path);
        return this;
    }

    public double getDouble(String path, double defaultValue) {
        return MassifApi.getFloat(handle, path, defaultValue);
    }

    public long getLong(String path, long defaultValue) {
        return MassifApi.getInt(handle, path, defaultValue);
    }

    public boolean getBool(String path, boolean defaultValue) {
        return MassifApi.getBool(handle, path, defaultValue);
    }

    /** @param defaultValue May be null, which the layer underneath cannot be handed directly. */
    public String getString(String path, String defaultValue) {
        return Values.getString(handle, path, defaultValue);
    }

    /** A position, in the object's own projection. */
    public MapPos getPos(String path) {
        return Values.toPos(MassifApi.getPos(handle, path, ""));
    }

    /** A position, converted - "EPSG:4326" for lon/lat. */
    public MapPos getPos(String path, String projection) {
        return Values.toPos(MassifApi.getPos(handle, path, projection));
    }

    public MapBounds getBounds(String path) {
        return Values.toBounds(MassifApi.getPos(handle, path, ""));
    }

    /**
     * A view scoped to a path prefix, so a group of options reads without repeating it:
     * <pre>map.fog().set("rangeStart", 2.5).set("rangeEnd", 8);</pre>
     */
    public PropertyGroup group(String prefix) {
        return new PropertyGroup(this, prefix);
    }

    // --- methods -------------------------------------------------------------------------------

    /**
     * Runs a method. Each argument is one positional parameter; an int[] or double[] becomes a
     * JSON array, which is how a tile or a list of positions is passed.
     *
     * @return The result, which the CALLER OWNS - close it, or use one of the typed helpers below
     *         which close it for you.
     * @throws MassifException When the method is unknown, the arguments do not fit, or it failed.
     */
    public MassifObject call(String method, Object... args) {
        try {
            return wrap(MassifApi.call(handle, method, toArgs(args)));
        } catch (RuntimeException e) {
            throw new MassifException("call '" + method + "' failed: " + e.getMessage(), e);
        }
    }

    /**
     * The same on a worker thread, with the result handed to the callback on the UI thread.
     *
     * The result is closed once the callback returns, so copy out what you need. A failure gives
     * a null result rather than throwing, since the call has already returned by then.
     *
     * @return A handle for {@link #cancel}, or 0.
     */
    public int callAsync(String method, final Callback callback, Object... args) {
        // One event name per call, so two calls to the same method cannot cross, and a holder for
        // the subscription so the handler can remove ITSELF - off(subscription) is what releases
        // the director, which offEvent cannot do because it does not know the ids it removed.
        final String event = method + ".done#" + (++asyncCounter);
        final Subscription[] self = new Subscription[1];
        self[0] = subscribe(event, new MapEvents.Handler<MapEvents.Event>() {
            @Override
            public void handle(MapEvents.Event e) {
                self[0].close();
                callback.onResult(e.hasPayload() ? wrap(e.payload) : null);
            }
        }, null, Delivery.UI, false, null, EventKind.PLAIN);
        try {
            return MassifApi.callAsync(handle, method, toArgs(args), event);
        } catch (RuntimeException e) {
            self[0].close();
            throw new MassifException("callAsync '" + method + "' failed: " + e.getMessage(), e);
        }
    }

    /**
     * Cancels a call started with {@link #callAsync}.
     *
     * Cancelling stops it being STARTED and stops its result being DELIVERED. It cannot abort one
     * already running, so a call in flight finishes and its result is dropped.
     */
    public boolean cancel(int call) {
        return MassifApi.cancelCall(call);
    }

    /** Cancels every pending call on this object. */
    public int cancelAll() {
        return MassifApi.cancelCalls(handle);
    }

    /** A binary property as bytes - "data" on a tile, or "" when this object IS the blob. */
    public byte[] data(String path) {
        BinaryData blob = MassifApi.getData(handle, path);
        return blob != null ? blob.getData() : null;
    }

    /** A bulk numeric result, flat. Empty when this object is not one. */
    public double[] doubles() {
        return MassifApi.getDoubles(handle);
    }

    /** This object's value as JSON, for a result that is a document rather than an object. */
    public String json() {
        return MassifApi.getString(handle, "", "");
    }

    // --- events --------------------------------------------------------------------------------

    /**
     * Subscribes to an event, delivered on the UI thread.
     *
     * @param handler Called with the raw event; the typed methods on {@link MassifMap} and
     *                {@link MassifLayer} are what an app usually wants instead.
     */
    public Subscription on(String event, final MapEvents.Handler<MapEvents.Event> handler) {
        return subscribe(event, handler, null, Delivery.UI, false, null, EventKind.PLAIN);
    }

    /** Removes every handler of one event on this object. */
    public int off(String event) {
        return MassifApi.offEvent(handle, event);
    }

    /** Removes every handler on this object. */
    public int offAll() {
        return MassifApi.offAll(handle);
    }

    // --- lifetime ------------------------------------------------------------------------------

    /** Drops the id and the context's reference. Handles held elsewhere go stale, not dangling. */
    @Override
    public void close() {
        MassifApi.destroy(handle);
    }

    @Override
    public boolean equals(Object other) {
        return other instanceof MassifObject && ((MassifObject) other).handle == handle;
    }

    @Override
    public int hashCode() {
        return handle;
    }

    @Override
    public String toString() {
        return getClass().getSimpleName() + "(" + handle + ")";
    }

    // --- internals -----------------------------------------------------------------------------

    /** Where a handler runs. UI is the default because that is where an app wants to be. */
    enum Delivery {
        ORIGIN(0), UI(1), BACKGROUND(2);

        final int value;

        Delivery(int value) {
            this.value = value;
        }
    }

    enum EventKind {
        PLAIN, CLICK, INTERACTION, TILE_CLICK, ELEMENT_CLICK
    }

    private static int asyncCounter;

    @SuppressWarnings("unchecked")
    <E> Subscription subscribe(final String event, final MapEvents.Handler<E> handler,
                               final MapEvents.ConsumingHandler<E> consuming, Delivery delivery,
                               boolean coalesce, String projection, final EventKind kind) {
        if (handler == null && consuming == null) {
            throw new IllegalArgumentException("Null handler");
        }
        EventListener listener = new EventListener() {
            @Override
            public boolean onEvent(int target, String name, int payload) {
                Object typed = build(kind, target, name, payload);
                if (consuming != null) {
                    return consuming.handle((E) typed);
                }
                handler.handle((E) typed);
                return false;
            }
        };
        int subscription = MassifApi.on(handle, event, listener, delivery.value, coalesce,
                                        projection == null ? "" : projection);
        if (subscription == 0) {
            throw new MassifException("Cannot subscribe to '" + event + "' on " + this);
        }
        return new Subscription(subscription);
    }

    static Object build(EventKind kind, int target, String name, int payload) {
        switch (kind) {
        case CLICK:         return new MapEvents.Click(target, name, payload);
        case INTERACTION:   return new MapEvents.Interaction(target, name, payload);
        case TILE_CLICK:    return new MapEvents.VectorTileClick(target, name, payload);
        case ELEMENT_CLICK: return new MapEvents.ElementClick(target, name, payload);
        default:            return new MapEvents.Event(target, name, payload);
        }
    }

    static String toArgs(Object[] args) {
        if (args == null || args.length == 0) {
            return "";
        }
        JSONArray array = new JSONArray();
        for (Object arg : args) {
            array.put(Spec.unwrap(arg instanceof MapPos ? posArray((MapPos) arg) : arg));
        }
        return array.toString();
    }

    private static double[] posArray(MapPos pos) {
        return new double[] { pos.getX(), pos.getY(), pos.getZ() };
    }

    /** Handed the result of an async call, on the UI thread. Null when the call failed. */
    public interface Callback {
        void onResult(MassifObject result);
    }
}
