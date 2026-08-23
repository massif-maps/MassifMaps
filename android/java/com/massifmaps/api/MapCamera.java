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

    MapCamera(MassifObject view) {
        this.view = view;
    }

    /** Seconds for the moves that follow. 0 is immediate. Resets to 0 after each move. */
    public MapCamera animate(float seconds) {
        duration = seconds;
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
     * Moves everything in ONE move. Four separate setters animate independently and visibly fight
     * each other; this is the call an app actually wants, and the reason the camera is methods
     * rather than writable properties.
     *
     * With no {@link #animate} it is immediate, and works before the map has drawn - which is when
     * a screen usually points its camera.
     */
    public MapCamera moveTo(Position pos, float zoom, float rotation, float tilt) {
        float seconds = take();
        if (seconds > 0) {
            view.call("flyTo", pos, zoom, rotation, tilt, seconds).close();
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
        view.call("fitBounds", bounds, screenRect, integerZoom, take()).close();
        return this;
    }

    /** The same, over the whole view. */
    public MapCamera fitBounds(Bounds bounds, float width, float height) {
        return fitBounds(bounds, new ScreenRect(0, 0, width, height), false);
    }

    public Position position() {
        return view.getPos("focusPos");
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
