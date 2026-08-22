package com.massifmaps.api;

import com.massifmaps.core.MapPos;
import com.massifmaps.layers.Layer;
import com.massifmaps.layers.VectorElementEventListener;
import com.massifmaps.layers.VectorLayer;
import com.massifmaps.layers.VectorTileLayer;
import com.massifmaps.layers.VectorTileEventListener;

/**
 * A layer, with the things an app does to one.
 *
 * Ordering and clicks are here rather than on {@link MassifMap} because they are properties of the
 * layer: holding only its id has to be enough to reorder it, which was the point of the registry.
 */
public final class MassifLayer extends MassifObject {

    private final MassifMap map;

    MassifLayer(int handle, String id, MassifMap map) {
        super(handle, "layer", id);
        this.map = map;
    }

    public MassifLayer opacity(float opacity) {
        set("opacity", opacity);
        return this;
    }

    public MassifLayer visible(boolean visible) {
        set("visible", visible);
        return this;
    }

    public boolean isVisible() {
        return getBool("visible", true);
    }

    /** Moves the layer within the map's stack. 0 is the bottom. */
    public MassifLayer moveTo(int index) {
        requireMap().view().getLayers().insert(index, layer());
        return this;
    }

    /** Removes it from the map. The object stays registered until it is closed. */
    public MassifLayer detach() {
        requireMap().view().getLayers().remove(layer());
        return this;
    }

    /** The object-API layer, for anything the facade does not reach yet. */
    public Layer layer() {
        Layer target = id != null ? MassifApi.getLayer(id) : null;
        if (target == null) {
            throw new MassifException("Layer is no longer registered: " + this);
        }
        return target;
    }

    /**
     * Subscribes to feature clicks on this layer.
     *
     * Installs the bridge on first use and chains to whatever listener was already there, so an
     * app that also uses the object API keeps working.
     */
    public Subscription onFeatureClick(MapEvents.Handler<MapEvents.VectorTileClick> handler) {
        return featureClick(handler, null);
    }

    /**
     * The same, for a handler that can claim the click - return true and the SDK treats the
     * gesture as handled. It runs on the thread the click came from, because the SDK asks whether
     * the event was consumed NOW.
     *
     * A different NAME rather than an overload: Java cannot tell two functional interfaces apart
     * from an implicit lambda, so `onFeatureClick(e -> ...)` would be ambiguous and neither form
     * would compile.
     */
    public Subscription consumeFeatureClick(
            MapEvents.ConsumingHandler<MapEvents.VectorTileClick> handler) {
        return featureClick(null, handler);
    }

    /**
     * Subscribes to clicks on the elements of a vector layer - a marker, a popup an app added.
     *
     * Installs the bridge on first use and chains to whatever listener was already there, the same
     * way {@link #onFeatureClick} does for tile features.
     */
    public Subscription onElementClick(MapEvents.Handler<MapEvents.ElementClick> handler) {
        return elementClick(handler, null);
    }

    /**
     * The same, for a handler that can CLAIM the tap - return true and the SDK treats the gesture
     * as handled, so the map's own onClick does not also fire for it.
     *
     * Without this a marker tap runs both handlers, and the usual pair of "open a popup on the
     * marker" and "dismiss it when the map is tapped" cancel each other out. It runs on the thread
     * the click came from, because the SDK asks whether the event was consumed NOW.
     */
    public Subscription consumeElementClick(
            MapEvents.ConsumingHandler<MapEvents.ElementClick> handler) {
        return elementClick(null, handler);
    }

    private Subscription elementClick(MapEvents.Handler<MapEvents.ElementClick> handler,
                                      MapEvents.ConsumingHandler<MapEvents.ElementClick> consuming) {
        Layer target = layer();
        if (!(target instanceof VectorLayer)) {
            throw new MassifException("Not a vector element layer: " + this);
        }
        VectorLayer vector = (VectorLayer) target;
        if (!elementBridged) {
            VectorElementEventListener chained = vector.getVectorElementEventListener();
            vector.setVectorElementEventListener(
                MassifApi.createVectorElementEventBridge(handle, chained));
            elementBridged = true;
        }
        return subscribe(MapEvents.VECTOR_ELEMENT_CLICKED, handler, consuming,
                         consuming != null ? Delivery.ORIGIN : Delivery.UI, false,
                         map != null ? map.eventProjection() : "", EventKind.ELEMENT_CLICK);
    }

    /**
     * Elevations under a set of positions, as one flat array - a profile over a track is thousands
     * of numbers and neither JSON nor a per-element proxy is an acceptable way to move them.
     * Only a hillshade layer answers; anything else gives an empty array.
     */
    public double[] elevations(MapPos... positions) {
        double[][] argument = new double[positions.length][];
        for (int i = 0; i < positions.length; i++) {
            argument[i] = new double[] { positions[i].getX(), positions[i].getY() };
        }
        MassifObject result = null;
        try {
            result = call("getElevations", (Object) argument);
            return result != null ? result.doubles() : new double[0];
        } catch (MassifException e) {
            return new double[0];
        } finally {
            if (result != null) {
                result.close();
            }
        }
    }

    private Subscription featureClick(MapEvents.Handler<MapEvents.VectorTileClick> handler,
                                      MapEvents.ConsumingHandler<MapEvents.VectorTileClick> consuming) {
        Layer target = layer();
        if (!(target instanceof VectorTileLayer)) {
            throw new MassifException("Not a vector tile layer: " + this);
        }
        VectorTileLayer vector = (VectorTileLayer) target;
        if (!bridged) {
            VectorTileEventListener chained = vector.getVectorTileEventListener();
            vector.setVectorTileEventListener(
                MassifApi.createVectorTileEventBridge(handle, chained));
            bridged = true;
        }
        return subscribe(MapEvents.VECTOR_TILE_CLICKED, handler, consuming,
                         consuming != null ? Delivery.ORIGIN : Delivery.UI, false,
                         requireMap().eventProjection(), EventKind.TILE_CLICK);
    }

    private boolean bridged;
    private boolean elementBridged;

    private MassifMap requireMap() {
        if (map == null) {
            throw new MassifException("Layer is not attached to a map: " + this);
        }
        return map;
    }

}
