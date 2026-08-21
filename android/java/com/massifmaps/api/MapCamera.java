package com.massifmaps.api;

import com.massifmaps.core.MapPos;
import com.massifmaps.ui.MapView;

/**
 * The camera, fluent, and animated by default.
 *
 * A thin pass-through to {@link MapView} rather than a reimplementation - the object API already
 * has the flight code, and duplicating it here would be two things to keep in step. What this adds
 * is one call that moves everything at once, which is otherwise four calls with four separate
 * animations racing each other.
 */
public final class MapCamera {

    private final MapView view;
    private float duration;

    MapCamera(MapView view) {
        this.view = view;
    }

    /** Seconds for the moves that follow. 0 is immediate. Resets to 0 after each move. */
    public MapCamera animate(float seconds) {
        duration = seconds;
        return this;
    }

    public MapCamera position(MapPos pos) {
        view.setFocusPos(pos, take());
        return this;
    }

    public MapCamera zoom(float zoom) {
        view.setZoom(zoom, take());
        return this;
    }

    public MapCamera rotation(float degrees) {
        view.setMapRotation(degrees, take());
        return this;
    }

    public MapCamera tilt(float degrees) {
        view.setTilt(degrees, take());
        return this;
    }

    /**
     * Moves everything in ONE flight. Four separate setters animate independently and visibly
     * fight each other; this is the call an app actually wants.
     */
    public MapCamera moveTo(MapPos pos, float zoom, float rotation, float tilt) {
        view.flyTo(pos, zoom, rotation, tilt, take());
        return this;
    }

    public MapCamera moveTo(MapPos pos, float zoom) {
        view.flyTo(pos, zoom, take());
        return this;
    }

    public MapPos position() {
        return view.getFocusPos();
    }

    public float zoom() {
        return view.getZoom();
    }

    public float rotation() {
        return view.getMapRotation();
    }

    public float tilt() {
        return view.getTilt();
    }

    /** Whether a flight is still running, and how to stop it. */
    public boolean isMoving() {
        return view.isFlightActive();
    }

    public MapCamera stop() {
        view.stopFlight();
        return this;
    }

    private float take() {
        float seconds = duration;
        duration = 0;
        return seconds;
    }
}
