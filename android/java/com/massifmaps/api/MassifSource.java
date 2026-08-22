package com.massifmaps.api;

/**
 * A tile data source, usable with or without a map.
 *
 * The typed methods are the ones where the raw form costs an app real work: a tile comes back as
 * bytes rather than as a handle it has to reach into and then remember to close.
 */
public final class MassifSource extends MassifObject {

    MassifSource(int handle, String id) {
        super(handle, "source", id);
    }

    /**
     * Fetches one tile, on the calling thread.
     *
     * BLOCKS - an HTTP source does network I/O here. Use {@link #loadTileAsync} from anything with
     * a UI.
     *
     * @return The tile's bytes, or null when the source has no such tile.
     */
    public byte[] loadTile(int x, int y, int zoom) {
        MassifObject tile = null;
        try {
            tile = call("loadTile", new int[] { x, y, zoom });
            return tile != null ? tile.data("data") : null;
        } catch (MassifException e) {
            return null;
        } finally {
            if (tile != null) {
                tile.close();
            }
        }
    }

    /**
     * The same on a worker thread, with the bytes handed to the callback on the UI thread.
     * @return A call id for {@link #cancel}.
     */
    public int loadTileAsync(int x, int y, int zoom, final TileCallback callback) {
        return callAsync("loadTile", new Callback() {
            @Override
            public void onResult(MassifObject tile) {
                callback.onTile(tile != null ? tile.data("data") : null);
            }
        }, new int[] { x, y, zoom });
    }

    /**
     * Adds a named layer to a "geojson" source and returns its index.
     *
     * The index, not the name, is what the other calls take - it is what the SDK's own API uses,
     * and a lookup per feature update would be the wrong trade for a source that exists to be
     * updated often.
     */
    public int createLayer(String name) {
        MassifObject result = call("createLayer", name);
        try {
            return result != null ? (int) result.getLong("", -1) : -1;
        } finally {
            if (result != null) {
                result.close();
            }
        }
    }

    /**
     * Replaces a layer's whole content with a GeoJSON document - a FeatureCollection, or one
     * Feature. The source re-tiles it, so the features go through the same style and the same
     * renderer as a tile server's would.
     */
    public MassifSource setLayerGeoJSON(int layer, String geoJson) {
        MassifObject result = call("setLayerGeoJSON", layer, geoJson);
        if (result != null) {
            result.close();
        }
        return this;
    }

    /** Handed a tile's bytes, or null when the fetch failed or the tile does not exist. */
    public interface TileCallback {
        void onTile(byte[] data);
    }
}
