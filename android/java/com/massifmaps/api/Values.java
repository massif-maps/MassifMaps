package com.massifmaps.api;

import org.json.JSONArray;

/**
 * The JSON the facade uses for the small by-value structs, as the facade's own types.
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
    static Position toPos(String json) {
        if (json == null || json.isEmpty()) {
            return null;
        }
        try {
            JSONArray array = new JSONArray(json);
            if (array.length() < 2) {
                return null;
            }
            return new Position(array.getDouble(0), array.getDouble(1),
                                array.length() > 2 ? array.getDouble(2) : 0);
        } catch (Exception e) {
            return null;
        }
    }

    static Bounds toBounds(String json) {
        if (json == null || json.isEmpty()) {
            return null;
        }
        try {
            JSONArray array = new JSONArray(json);
            if (array.length() != 2) {
                return null;
            }
            Position min = toPos(array.getJSONArray(0).toString());
            Position max = toPos(array.getJSONArray(1).toString());
            return min != null && max != null ? new Bounds(min, max) : null;
        } catch (Exception e) {
            return null;
        }
    }

    static String fromPos(Position pos) {
        return "[" + pos.lng + "," + pos.lat + "," + pos.alt + "]";
    }

    static String fromBounds(Bounds bounds) {
        return "[" + fromPos(bounds.min) + "," + fromPos(bounds.max) + "]";
    }

    static String fromRect(ScreenRect rect) {
        return "[[" + rect.min.x + "," + rect.min.y + "],[" + rect.max.x + "," + rect.max.y + "]]";
    }

    /** @return null when the value is missing or not a screen point. */
    static ScreenPoint toScreenPoint(String json) {
        if (json == null || json.isEmpty()) {
            return null;
        }
        try {
            JSONArray array = new JSONArray(json);
            return array.length() >= 2
                ? new ScreenPoint((float) array.getDouble(0), (float) array.getDouble(1))
                : null;
        } catch (Exception e) {
            return null;
        }
    }

    private Values() {
    }
}
