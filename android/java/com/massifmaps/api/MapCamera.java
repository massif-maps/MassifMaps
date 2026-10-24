package com.massifmaps.api;

/**
 * The camera, fluent, and animated by default.
 *
 * Every call goes through the FACADE - the map view is adopted, and this is `set`, `get` and
 * `call` on its handle. Not a pass-through to MapView any more: that was the one part of the sugar
 * a binding could not reproduce from the C ABI, and going through the facade is also what applies
 * the projection, so a position from a click event can be handed straight back here (#159).
 *
 * Positions are WGS84 - longitude, latitude - unless the map was told otherwise.
 */
public final class MapCamera {

    private final MassifObject view;
    private float duration;
    private float climb;
    private String easing = "";

    MapCamera(MassifObject view) {
        this.view = view;
    }

    /** Seconds for the moves that follow. 0 is immediate. Resets to 0 after each move. */
    public MapCamera animate(float seconds) {
        duration = seconds;
        return this;
    }

    /**
     * Arches the next flight - highest halfway, nothing at either end, which is what clears the
     * ridge between two valleys. In the base projection's units. Resets after the move.
     */
    public MapCamera climb(float height) {
        climb = height;
        return this;
    }

    /**
     * The timing curve of the next flight, by its CSS name: "ease" (the default), "linear", "ease-in",
     * "ease-out" or "ease-in-out". Resets after the move.
     */
    public MapCamera easing(String curve) {
        easing = curve;
        return this;
    }

    public MapCamera position(Position pos) {
        return moveTo(pos, zoom(), rotation(), tilt());
    }

    public MapCamera zoom(float zoom) {
        return moveTo(position(), zoom, rotation(), tilt());
    }

    public MapCamera rotation(float degrees) {
        return moveTo(position(), zoom(), degrees, tilt());
    }

    public MapCamera tilt(float degrees) {
        return moveTo(position(), zoom(), rotation(), degrees);
    }

    /**
     * The moves the old MapView had as pan/zoom/rotate/tilt: relative to where the camera is now, and
     * one move like {@link #moveTo}. {@link #animate} applies to them as to every other move.
     */
    public MapCamera panBy(double deltaLng, double deltaLat) {
        Position now = position();
        return position(new Position(now.lng + deltaLng, now.lat + deltaLat, now.alt));
    }

    public MapCamera zoomBy(float delta) {
        return zoom(zoom() + delta);
    }

    /** Positive turns clockwise, as the old MapView.rotate did. */
    public MapCamera rotateBy(float degrees) {
        return rotation(rotation() + degrees);
    }

    public MapCamera tiltBy(float degrees) {
        return tilt(tilt() + degrees);
    }

    /**
     * Moves everything in ONE move. Four separate setters animate independently and visibly fight
     * each other; this is the call an app actually wants, and the reason the camera is methods
     * rather than writable properties.
     *
     * With no {@link #animate} it is immediate, and works before the map has drawn - which is when
     * a screen usually points its camera.
     */
    public MapCamera moveTo(Position pos, float zoom, float rotation, float tilt) {
        float seconds = take();
        float height = climb;
        climb = 0;
        if (seconds > 0) {
            String curve = easing;
            easing = "";
            view.call("flyTo", pos, zoom, rotation, tilt, height, seconds, curve).close();
        } else {
            view.call("moveTo", pos, zoom, rotation, tilt).close();
        }
        return this;
    }

    public MapCamera moveTo(Position pos, float zoom) {
        return moveTo(pos, zoom, rotation(), tilt());
    }

    /**
     * Frames a bounding box, which is what "zoom to this route" or "fit these markers" means.
     *
     * @param screenRect The part of the view to fit it into - the whole view unless something
     *                   overlays it.
     * @param integerZoom Snap to a whole zoom level, which keeps raster tiles crisp.
     */
    public MapCamera fitBounds(Bounds bounds, ScreenRect screenRect, boolean integerZoom) {
        return fitBounds(bounds, screenRect, integerZoom, false, false);
    }

    /** The same, also straightening the camera on the way there. */
    public MapCamera fitBounds(Bounds bounds, ScreenRect screenRect, boolean integerZoom,
                               boolean resetRotation, boolean resetTilt) {
        view.call("fitBounds", bounds, screenRect, integerZoom, resetRotation, resetTilt,
                  take()).close();
        return this;
    }

    /** The same, over the whole view. */
    public MapCamera fitBounds(Bounds bounds, float width, float height) {
        return fitBounds(bounds, new ScreenRect(0, 0, width, height), false);
    }

    public Position position() {
        return view.getPos("focusPos");
    }

    /**
     * Where the camera IS, not what it looks at. At a low tilt the focus is kilometres out in front
     * of the viewpoint, so this is what a top-down view centres on to come back to the same place.
     */
    public Position eyePosition() {
        return view.getPos("cameraPos");
    }

    public float zoom() {
        return (float) view.getDouble("zoom", 0);
    }

    public float rotation() {
        return (float) view.getDouble("rotation", 0);
    }

    public float tilt() {
        return (float) view.getDouble("tilt", 0);
    }

    /** The map's renderer, which holds the post-process effect. */
    public MassifObject renderer() {
        return view.child("mapRenderer");
    }

    /** Where a touch point is on the map. */
    public Position screenToMap(float x, float y) {
        MassifObject result = view.call("screenToMap", x, y);
        try {
            return Values.toPos(result.json());
        } finally {
            result.close();
        }
    }

    /** And the other way, for placing a native view over a coordinate. */
    public ScreenPoint mapToScreen(Position pos) {
        MassifObject result = view.call("mapToScreen", pos);
        try {
            return Values.toScreenPoint(result.json());
        } finally {
            result.close();
        }
    }

    /** Whether a flight is still running, and how to stop it. */
    public boolean isMoving() {
        return view.getBool("flightActive", false);
    }

    /** 0 to 1 through the current flight, so a UI can follow it. */
    public float progress() {
        return (float) view.getDouble("flightProgress", 0);
    }

    public MapCamera stop() {
        view.call("stopFlight").close();
        return this;
    }

    private float take() {
        float seconds = duration;
        duration = 0;
        return seconds;
    }
}
