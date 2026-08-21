package com.massifmaps.api;

import com.massifmaps.datasources.TileDataSource;

/**
 * The registry, for objects that do not belong to a map.
 *
 * This is the half of the object API worth keeping: a source driving an app's own tile server, or
 * one source shared by two maps, without either map owning it. Ids are per kind, so a source and a
 * layer may both be called "osm".
 *
 * <pre>
 * MassifSource osm = Massif.source("osm", Spec.of("http").set("url", url));
 * byte[] tile = osm.loadTile(8467, 5852, 14);
 * </pre>
 */
public final class Massif {

    /**
     * Builds a source, or returns the existing one when the spec is IDENTICAL - so two maps can
     * ask for the same source without coordinating. A different spec under the same id throws.
     */
    public static MassifSource source(String id, Spec spec) {
        return new MassifSource(create("source", id, spec), id);
    }

    /** The source registered under an id, or null. */
    public static MassifSource source(String id) {
        int handle = MassifApi.findObject("source", id);
        return handle == 0 ? null : new MassifSource(handle, id);
    }

    /** Builds a style - a CartoCSS project, an inline sheet. */
    public static MassifObject style(String id, Spec spec) {
        return new MassifObject(create("style", id, spec), "style", id);
    }

    public static MassifObject style(String id) {
        int handle = MassifApi.findObject("style", id);
        return handle == 0 ? null : new MassifObject(handle, "style", id);
    }

    /**
     * Builds a layer. It is NOT added to a map - use {@link MassifMap#add} for that, or hand
     * {@link MassifLayer#layer()} to the object API.
     */
    public static MassifLayer layer(String id, Spec spec) {
        return new MassifLayer(create("layer", id, spec), id, null);
    }

    public static MassifLayer layer(String id) {
        int handle = MassifApi.findObject("layer", id);
        return handle == 0 ? null : new MassifLayer(handle, id, null);
    }

    /**
     * Adopts a source built with the object API, so it can be addressed by id like any other.
     * The escape hatch in the other direction is {@link MassifSource#handle}.
     */
    public static boolean has(String kind, String id) {
        return MassifApi.findObject(kind, id) != 0;
    }

    /** Drops an id. Handles held elsewhere go stale rather than dangling. */
    public static boolean destroy(String kind, String id) {
        return MassifApi.unregisterObject(kind, id);
    }

    /** The object-API source behind an id, for anything the facade does not reach yet. */
    public static TileDataSource rawSource(String id) {
        return MassifApi.getSource(id);
    }

    static int create(String kind, String id, Spec spec) {
        if (spec == null) {
            throw new IllegalArgumentException("Null spec for " + kind + " '" + id + "'");
        }
        try {
            return MassifApi.create(kind, id, spec.toJson());
        } catch (RuntimeException e) {
            throw new MassifException("Cannot create " + kind + " '" + id + "': " + e.getMessage(), e);
        }
    }

    private Massif() {
    }
}
