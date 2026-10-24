package com.massifmaps.api;

import com.massifmaps.components.Layers;
import com.massifmaps.datasources.TileDataSource;
import com.massifmaps.layers.Layer;
import com.massifmaps.utils.AssetPackage;

/**
 * The bridge between the object API and the facade, for apps still built on the object API. It exists
 * only in the objectapi variant of the SDK: the default one has no object API to bridge.
 *
 * Adopting gives an object built with {@code new} an id, and with it properties, methods and events, so
 * an app moves to the facade a piece at a time.
 */
public final class MassifLegacy {

    private MassifLegacy() {
    }

    /**
     * Adopts a layer. The CONCRETE class is recovered, so an adopted VectorTileLayer answers to a vector
     * tile layer's properties rather than only to Layer's.
     * @return The wrapper, or null when the id is taken.
     */
    public static MassifLayer adopt(String id, Layer layer) {
        int handle = MassifInterop.adopt("layer", id, layer);
        return handle == 0 ? null : new MassifLayer(handle, id, null);
    }

    /** The same for a source. */
    public static MassifSource adopt(String id, TileDataSource source) {
        int handle = MassifInterop.adopt("source", id, source);
        return handle == 0 ? null : new MassifSource(handle, id);
    }

    /**
     * The same for an asset package, including an app's OWN subclass of it: the one thing a spec cannot
     * build. Every spec taking an "assets" key then resolves the id.
     */
    public static MassifObject adopt(String id, AssetPackage assets) {
        int handle = MassifInterop.adopt("assets", id, assets);
        return handle == 0 ? null : new MassifObject(handle, "assets", id);
    }

    /** Adopts the layer at a stack position of the map. @return The wrapper, or null. */
    public static MassifLayer adoptLayer(MassifMap map, String id, int index) {
        MassifLayer existing = map.layer(id);
        if (existing != null) {
            return existing;
        }
        Layer target = index >= 0 && index < map.layerCount() ? rawLayer(map, index) : null;
        if (target == null) {
            return null;
        }
        int handle = MassifInterop.adopt("layer", id, target);
        return handle == 0 ? null : new MassifLayer(handle, id, map);
    }

    /** Adopts the FIRST layer of a kind, which is what an app usually means by "the base map". */
    public static MassifLayer adoptFirst(MassifMap map, String id, Class<? extends Layer> type) {
        for (int index = 0; index < map.layerCount(); index++) {
            if (type.isInstance(rawLayer(map, index))) {
                return adoptLayer(map, id, index);
            }
        }
        return null;
    }

    /** A layer by stack position, for the ones the app did not build through the registry. */
    public static Layer rawLayer(MassifMap map, int index) {
        MassifObject layer = map.layersObject().call("get", index);
        return MassifInterop.getLayerByHandle(layer.handle());
    }

    /** The object-API layer behind a wrapper, for anything the facade does not reach yet. */
    public static Layer layer(MassifLayer layer) {
        Layer target = layer.id() != null ? MassifInterop.getLayer(layer.id()) : null;
        if (target == null) {
            throw new MassifException("Layer is no longer registered: " + layer);
        }
        return target;
    }

    /** The object-API source behind an id, for anything the facade does not reach yet. */
    public static TileDataSource rawSource(String id) {
        return MassifInterop.getSource(id);
    }

    /** The map's layer list as an object, for code that still adds layers built with {@code new}. */
    public static Layers layers(MassifMap map) {
        return MassifInterop.getLayerListByHandle(map.layersObject().handle());
    }
}
