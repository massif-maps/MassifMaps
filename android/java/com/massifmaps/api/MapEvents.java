package com.massifmaps.api;

import com.massifmaps.core.MapPos;

/**
 * Typed event payloads.
 *
 * The point of this file: a handler should read `e.featureId()`, not
 * `MassifApi.getInt(payload, "featureId", -1)`. Each class is a thin, typed view over the payload
 * handle - **nothing is read until it is asked for**, so a feature with a long geometry costs
 * nothing unless the handler wants the geometry.
 *
 * A payload is only valid for the duration of the handler. Copy out what you need; keeping the
 * event object and reading it later gives you nothing, because the handle has been freed.
 */
public final class MapEvents {

    /** Event names, for the generic {@link MassifObject#on} when a typed method does not exist. */
    public static final String CLICKED = "map.clicked";
    public static final String MOVED = "map.moved";
    public static final String IDLE = "map.idle";
    public static final String STABLE = "map.stable";
    public static final String INTERACTION = "map.interaction";
    public static final String VECTOR_TILE_CLICKED = "vectortile.clicked";
    public static final String VECTOR_ELEMENT_CLICKED = "vectorelement.clicked";

    /** Called on the thread the subscription asked for. */
    public interface Handler<E> {
        void handle(E event);
    }

    /**
     * The same, for a subscription that can claim the event. Returning true stops it reaching
     * later handlers and tells the SDK the gesture was handled.
     */
    public interface ConsumingHandler<E> {
        boolean handle(E event);
    }

    /** What every event carries: the object it fired on, and its payload if it has one. */
    public static class Event {
        final int target;
        final int payload;
        final String name;

        Event(int target, String name, int payload) {
            this.target = target;
            this.name = name;
            this.payload = payload;
        }

        /** The event name, for a handler subscribed to more than one. */
        public String name() {
            return name;
        }

        /** The object the event fired on. */
        public MassifObject source() {
            return MassifObject.wrap(target);
        }

        /** Whether there is a payload to read at all - map.idle and map.moved carry none. */
        public boolean hasPayload() {
            return payload != 0;
        }

        /**
         * Any payload property by path, for something a typed accessor does not cover yet. The
         * paths are the ones in the property table.
         */
        public String get(String path) {
            return MassifApi.getString(payload, path, null);
        }

        public double getDouble(String path, double defaultValue) {
            return MassifApi.getFloat(payload, path, defaultValue);
        }
    }

    /** A click on the map itself. */
    public static final class Click extends Event {
        Click(int target, String name, int payload) {
            super(target, name, payload);
        }

        /** Where it landed, in the map's projection or the one set with eventProjection. */
        public MapPos position() {
            return Values.toPos(MassifApi.getPos(payload, "clickPos", ""));
        }

        /** 0 single, 1 long, 2 double, 3 dual. */
        public int clickType() {
            return (int) MassifApi.getInt(payload, "clickType", -1);
        }
    }

    /** A pan, zoom, rotate or tilt the user drove. */
    public static final class Interaction extends Event {
        Interaction(int target, String name, int payload) {
            super(target, name, payload);
        }

        public boolean isPan() {
            return MassifApi.getBool(payload, "panning", false);
        }

        public boolean isZoom() {
            return MassifApi.getBool(payload, "zooming", false);
        }

        public boolean isRotate() {
            return MassifApi.getBool(payload, "rotating", false);
        }

        public boolean isTilt() {
            return MassifApi.getBool(payload, "tilting", false);
        }
    }

    /** A click on a feature of a vector tile layer. */
    public static final class VectorTileClick extends Event {
        VectorTileClick(int target, String name, int payload) {
            super(target, name, payload);
        }

        public long featureId() {
            return MassifApi.getInt(payload, "featureId", -1);
        }

        /** The style layer the feature came from - "roads", "buildings". */
        public String layerName() {
            return MassifApi.getString(payload, "featureLayerName", "");
        }

        /** The clicked point, MultiPoint-aware rather than the centre of the whole feature. */
        public MapPos position() {
            return Values.toPos(MassifApi.getPos(payload, "featurePos", ""));
        }

        /** Where the finger landed, which is not the same as where the feature is. */
        public MapPos clickPosition() {
            return Values.toPos(MassifApi.getPos(payload, "clickPos", ""));
        }

        /** POINT 0, LINE 1, POLYGON 2, MULTIPOINT 3, MULTILINE 4, MULTIPOLYGON 5, COLLECTION 6. */
        public int geometryType() {
            return (int) MassifApi.getInt(payload, "feature.geometry.type", -1);
        }

        /**
         * One property of the clicked feature, read on its own - the whole bag is never parsed.
         * @return null when the feature does not have it.
         */
        public String property(String key) {
            return MassifApi.getString(payload, "feature.properties." + key, null);
        }

        public double propertyDouble(String key, double defaultValue) {
            return MassifApi.getFloat(payload, "feature.properties." + key, defaultValue);
        }

        /** Every property, as a JSON object. Only pay for this when you really want them all. */
        public String properties() {
            return MassifApi.getString(payload, "feature.properties", "{}");
        }

        /** The geometry as GeoJSON, serialised by the SDK rather than by each binding. */
        public String geoJson() {
            return MassifApi.getString(payload, "feature.geometryGeoJSON", "");
        }
    }

    /** A click on a vector element - a marker, a line, a polygon an app added. */
    public static final class ElementClick extends Event {
        ElementClick(int target, String name, int payload) {
            super(target, name, payload);
        }

        public MapPos position() {
            return Values.toPos(MassifApi.getPos(payload, "elementClickPos", ""));
        }

        public MapPos clickPosition() {
            return Values.toPos(MassifApi.getPos(payload, "clickPos", ""));
        }

        public int clickType() {
            return (int) MassifApi.getInt(payload, "clickType", -1);
        }
    }

    private MapEvents() {
    }
}
