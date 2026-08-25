package com.massifmaps.api;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * The JSON spec an object is built from, without writing JSON.
 *
 * Deliberately NOT one builder class per source or layer type. A named method per option would
 * have to grow every time the SDK gains one, which is the maintenance the facade exists to remove -
 * so `set` is the whole surface and a new option works the day it is declared, with no change here.
 *
 * <pre>
 * Massif.source("osm", Spec.of("http")
 *     .set("url", "https://tile.example.com/{z}/{x}/{y}.mvt")
 *     .set("maxZoom", 14));
 * </pre>
 */
public final class Spec {

    private final JSONObject json = new JSONObject();

    private Spec(String type) {
        if (type != null) {
            set("type", type);
        }
    }

    /**
     * A spec of the given type - "http", "mbtiles", "vector", "raster", "cartocss"...
     * The types a kind knows are the factories registered for it; an unknown one fails at create.
     */
    public static Spec of(String type) {
        return new Spec(type);
    }

    /**
     * A bare JSON object, with no "type".
     *
     * For a property whose value IS a map rather than an object to build - a source's
     * httpHeaders, a style parameter holding a table of colours.
     */
    public static Spec object() {
        return new Spec(null);
    }

    /**
     * Sets a key. Anything the factory does not consume is applied as a property afterwards, so a
     * spec can carry options as well as constructor arguments.
     *
     * @param value A string, number, boolean, another Spec, or an array of any of those.
     */
    public Spec set(String key, Object value) {
        try {
            json.put(key, unwrap(value));
        } catch (JSONException e) {
            throw new IllegalArgumentException("Cannot set '" + key + "'", e);
        }
        return this;
    }

    /** The keys written so far, in insertion order. For a group that has to prefix them. */
    java.util.List<String> keys() {
        java.util.List<String> names = new java.util.ArrayList<String>();
        java.util.Iterator<String> it = json.keys();
        while (it.hasNext()) {
            names.add(it.next());
        }
        return names;
    }

    /** The raw value under a key, as it was stored. */
    Object get(String key) {
        return json.opt(key);
    }

    /** The spec as JSON, which is what the facade actually takes. */
    public String toJson() {
        return json.toString();
    }

    @Override
    public String toString() {
        return toJson();
    }

    static Object unwrap(Object value) {
        if (value instanceof Spec) {
            return ((Spec) value).json;
        }
        if (value instanceof Object[]) {
            JSONArray array = new JSONArray();
            for (Object element : (Object[]) value) {
                array.put(unwrap(element));
            }
            return array;
        }
        if (value instanceof int[]) {
            JSONArray array = new JSONArray();
            for (int element : (int[]) value) {
                array.put(element);
            }
            return array;
        }
        if (value instanceof double[]) {
            JSONArray array = new JSONArray();
            for (double element : (double[]) value) {
                try {
                    array.put(element);
                } catch (JSONException e) {
                    throw new IllegalArgumentException("Not a finite number", e);
                }
            }
            return array;
        }
        return value;
    }
}
