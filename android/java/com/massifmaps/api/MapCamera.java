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
    /** The last target commanded, for {@link #reapply}. */
    private MapPos lastPos;
    private float lastZoom, lastRotation, lastTilt;
    private boolean commanded;

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
     *
     * A duration of 0 is applied DIRECTLY rather than as a flight: a flight is set up on the
     * render thread's next frame, so one asked for before the map has drawn - the usual case when
     * a screen opens - is simply never run, and the camera stays wherever it started.
     * See https://github.com/massif-maps/MassifMaps/issues (flyTo before the first frame).
     */
    public MapCamera moveTo(MapPos pos, float zoom, float rotation, float tilt) {
        float seconds = take();
        if (seconds > 0) {
            view.flyTo(pos, zoom, rotation, tilt, seconds);
        } else {
            // ZOOM FIRST. With restricted panning on, the focus is clamped so the viewport stays
            // inside the world, so setting it while the map is still zoomed out to the whole globe
            // pins the latitude to 0 - and the later zoom does not undo it.
            view.setZoom(zoom, 0);
            view.setMapRotation(rotation, 0);
            view.setTilt(tilt, 0);
            view.setFocusPos(pos, 0);
        }
        remember(pos, zoom, rotation, tilt);
        return this;
    }

    public MapCamera moveTo(MapPos pos, float zoom) {
        float seconds = take();
        if (seconds > 0) {
            view.flyTo(pos, zoom, seconds);
        } else {
            view.setZoom(zoom, 0);
            view.setFocusPos(pos, 0);
        }
        remember(pos, zoom, rotation(), tilt());
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

    /**
     * Re-issues the last {@link #moveTo}.
     *
     * Needed because restricted panning CLAMPS a focus set before the GL surface has a size - the
     * latitude comes back as 0 and the map opens on the equator. Android layout is not the gate;
     * the surface is, and it arrives on the render thread. Call this once the map has drawn.
     *
     * A workaround for an SDK bug, not a feature: see MapCamera#moveTo.
     */
    public MapCamera reapply() {
        if (commanded) {
            view.setZoom(lastZoom, 0);
            view.setMapRotation(lastRotation, 0);
            view.setTilt(lastTilt, 0);
            view.setFocusPos(lastPos, 0);
        }
        return this;
    }

    private void remember(MapPos pos, float zoom, float rotation, float tilt) {
        lastPos = pos;
        lastZoom = zoom;
        lastRotation = rotation;
        lastTilt = tilt;
        commanded = true;
    }

    private float take() {
        float seconds = duration;
        duration = 0;
        return seconds;
    }
}
