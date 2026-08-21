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
