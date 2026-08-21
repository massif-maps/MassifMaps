package com.massifmaps.api;

import com.massifmaps.core.MapBounds;
import com.massifmaps.core.MapPos;

import org.json.JSONArray;

/**
 * The JSON the facade uses for the small by-value structs, as the SDK's own types.
 *
 * One place, so no binding grows its own position parser - the shape is `[x, y]` or `[x, y, z]`,
 * and bounds are a pair of those.
 */
final class Values {

    /**
     * Stands in for "no default" when reading a string.
     *
     * Swig's std::string typemap rejects a null Java String with a NullPointerException, so null
     * cannot be passed through as a default - which is what a nullable getter wants. This is
     * passed instead and mapped back on the way out. It contains a NUL, so no real value equals it.
     */
    private static final String ABSENT = "\u0000massif:absent";

    /** A string property, or null when the path does not resolve. */
    static String getStringOrNull(int handle, String path) {
        String value = MassifApi.getString(handle, path, ABSENT);
        return ABSENT.equals(value) ? null : value;
    }

    /** The same, with the caller's default - which may itself be null. */
    static String getString(int handle, String path, String defaultValue) {
        String value = getStringOrNull(handle, path);
        return value != null ? value : defaultValue;
    }

    /** @return null when the value is missing or not a position. */
    static MapPos toPos(String json) {
        if (json == null || json.isEmpty()) {
            return null;
        }
        try {
            JSONArray array = new JSONArray(json);
            if (array.length() < 2) {
                return null;
            }
            return new MapPos(array.getDouble(0), array.getDouble(1),
                              array.length() > 2 ? array.getDouble(2) : 0);
        } catch (Exception e) {
            return null;
        }
    }

    static MapBounds toBounds(String json) {
        if (json == null || json.isEmpty()) {
            return null;
        }
        try {
            JSONArray array = new JSONArray(json);
            if (array.length() != 2) {
                return null;
            }
            MapPos min = toPos(array.getJSONArray(0).toString());
            MapPos max = toPos(array.getJSONArray(1).toString());
            return min != null && max != null ? new MapBounds(min, max) : null;
        } catch (Exception e) {
            return null;
        }
    }

    static String fromPos(MapPos pos) {
        return "[" + pos.getX() + "," + pos.getY() + "," + pos.getZ() + "]";
    }

    private Values() {
    }
}
