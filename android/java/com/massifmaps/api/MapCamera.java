package com.massifmaps.api;

import com.massifmaps.core.MapBounds;
import com.massifmaps.core.MapPos;
import com.massifmaps.core.ScreenBounds;
import com.massifmaps.core.ScreenPos;
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
     * Moves everything in ONE move. Four separate setters animate independently and visibly fight
     * each other; this is the call an app actually wants.
     *
     * With no {@link #animate} it is immediate, and works before the map has drawn - which is when
     * a screen usually points its camera.
     */
    public MapCamera moveTo(MapPos pos, float zoom, float rotation, float tilt) {
        float seconds = take();
        if (seconds > 0) {
            view.flyTo(pos, zoom, rotation, tilt, seconds);
        } else {
            view.moveTo(pos, zoom, rotation, tilt);
        }
        return this;
    }

    public MapCamera moveTo(MapPos pos, float zoom) {
        float seconds = take();
        if (seconds > 0) {
            view.flyTo(pos, zoom, seconds);
        } else {
            view.moveTo(pos, zoom);
        }
        return this;
    }

    /**
     * Frames a bounding box, which is what "zoom to this route" or "fit these markers" means.
     *
     * @param screenBounds The part of the view to fit it into - the whole view unless something
     *                     overlays it.
     * @param integerZoom Snap to a whole zoom level, which keeps raster tiles crisp.
     */
    public MapCamera fitBounds(MapBounds bounds, ScreenBounds screenBounds, boolean integerZoom) {
        view.moveToFitBounds(bounds, screenBounds, integerZoom, take());
        return this;
    }

    /** The same, over the whole view. */
    public MapCamera fitBounds(MapBounds bounds) {
        return fitBounds(bounds, new ScreenBounds(new ScreenPos(0, 0),
                                                  new ScreenPos(view.getWidth(), view.getHeight())),
                         false);
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
