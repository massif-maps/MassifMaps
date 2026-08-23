package com.massifmaps.ui;

import java.lang.reflect.Method;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.AssetManager;
import android.content.res.TypedArray;
import android.opengl.GLSurfaceView;
import android.util.AttributeSet;
import android.view.MotionEvent;

import com.massifmaps.components.Options;
import com.massifmaps.components.Layers;
import com.massifmaps.core.MapBounds;
import com.massifmaps.core.MapPos;
import com.massifmaps.core.ScreenPos;
import com.massifmaps.core.ScreenBounds;
import com.massifmaps.core.MapVec;
import com.massifmaps.renderers.MapRenderer;
import com.massifmaps.renderers.RedrawRequestListener;
import com.massifmaps.utils.AndroidUtils;
import com.massifmaps.utils.AssetUtils;

/**
 * MapView is a view class supporting map rendering and interaction.
 */
public class MapView extends GLSurfaceView implements GLSurfaceView.Renderer, MapViewInterface {
    
    private static final int NATIVE_ACTION_POINTER_1_DOWN = 0;
    private static final int NATIVE_ACTION_POINTER_2_DOWN = 1;
    private static final int NATIVE_ACTION_MOVE = 2;
    private static final int NATIVE_ACTION_CANCEL = 3;
    private static final int NATIVE_ACTION_POINTER_1_UP = 4;
    private static final int NATIVE_ACTION_POINTER_2_UP = 5;
    private static final int NATIVE_NO_COORDINATE = -1;
    
    private static final int INVALID_POINTER_ID = -1;

    private static Throwable libraryLoadingErrorCause;

    private static AssetManager assetManager;
    
    static {
        try {
            System.loadLibrary("massif");
            AndroidUtils.attachJVM(MapView.class);
        } catch (Throwable cause) {
            android.util.Log.e("massif", "Failed to initialize Massif Maps, native .so library failed to load?", cause);
            libraryLoadingErrorCause = cause;
        }
    }

    private BaseMapView baseMapView;
    
    private int pointer1Id = INVALID_POINTER_ID;
    private int pointer2Id = INVALID_POINTER_ID;
    
    /**
     * Creates a new MapView object from a context object.
     * @param context The context object.
     */
    public MapView(Context context) {
        this(context, null);
    }

    /**
     * Creates a new MapView object from a context object and attributes.
     * @param context The context object.
     * @param attrs The attributes.
     */
    public MapView(Context context, AttributeSet attrs) {
        super(context, attrs);

        // Unless explictly not clickable, make clickable by default
        boolean clickable = true;
        boolean longClickable = true;
        try {
            TypedArray ta = context.obtainStyledAttributes(attrs, new int[]{ android.R.attr.clickable, android.R.attr.longClickable });
            clickable = ta.getBoolean(0, true);
            longClickable = ta.getBoolean(1, true);
            ta.recycle();
        } catch (Exception e) {
            com.massifmaps.utils.Log.warn("MapView: Failed to read attributes");
        }
        setClickable(clickable);
        setLongClickable(longClickable);

        if (!isInEditMode()) {
            // Connect context info and asset manager to native part
            AndroidUtils.setContext(context);
            if (assetManager == null) {
                assetManager = context.getApplicationContext().getAssets();
                AssetUtils.setAssetManagerPointer(assetManager);
            }

            // Initialize native BaseMapView instance
            baseMapView = new BaseMapView();
            baseMapView.getOptions().setDPI(getResources().getDisplayMetrics().densityDpi);
            baseMapView.setRedrawRequestListener(new MapRedrawRequestListener(this));

            // Set up relevant EGL state
            try {
                Method m = GLSurfaceView.class.getMethod("setPreserveEGLContextOnPause", Boolean.TYPE);
                m.invoke(this, true);
            } catch (Exception e) {
                com.massifmaps.utils.Log.info("MapView: Preserving EGL context on pause is not possible: " + e);
            }
            // OpenGL ES 3.0 is required, so it is asked for unconditionally - no probe and no
            // fallback. The manifest declares glEsVersion 0x00030000, which keeps the SDK off
            // devices that cannot provide one.
            setEGLContextClientVersion(3);
            setEGLConfigChooser(new ConfigChooser());
            setRenderer(this);
            setRenderMode(RENDERMODE_WHEN_DIRTY);
        }
    }

    /**
     * Makes the view translucent, so that whatever is behind it shows through wherever the map
     * does not paint. Combine it with a transparent clear color - Options.setClearColor(new
     * Color(0, 0, 0, 0)) - which is what leaves the frame empty; the SDK renders with premultiplied
     * alpha, so the result composites correctly.
     *
     * IMPORTANT, and the usual trap: a MapView is a SurfaceView. Its surface is composited BELOW
     * the window, so it can only reveal another surface below it - typically a camera preview -
     * and NOT other views of the same layout, which are drawn above it. This method also raises the
     * surface above other media surfaces (setZOrderMediaOverlay) so a preview placed behind it is
     * what shows through.
     *
     * To blend with ordinary views instead, use TextureMapView, which is a real view in the
     * hierarchy.
     *
     * Changing this after the view is attached recreates the GL surface.
     * @param translucent True to make the view translucent.
     */
    public void setTranslucent(boolean translucent) {
        setZOrderMediaOverlay(translucent);
        getHolder().setFormat(translucent ? android.graphics.PixelFormat.TRANSLUCENT : android.graphics.PixelFormat.OPAQUE);
    }

    /**
     * Deletes the resources associated with the MapView.
     * The method can be used to dispose native objects immediately,
     * without waiting for next GC cycle.
     */
    public synchronized void delete() {
        if (baseMapView != null) {
            baseMapView.setRedrawRequestListener(null);
            baseMapView.delete();
            baseMapView = null;
        }
    }
    
    @Override
    public synchronized void onSurfaceCreated(GL10 gl, EGLConfig config) {
        if (baseMapView != null) {
            baseMapView.onSurfaceCreated();
        }
    }
    
    @Override
    public synchronized void onSurfaceChanged(GL10 gl, int width, int height) {
        if (baseMapView != null) {
            baseMapView.onSurfaceChanged(width, height);
        }
    }
    
    @Override
    public synchronized void onDrawFrame(GL10 gl) {
        if (baseMapView != null) {
            baseMapView.onDrawFrame();
        }
    }
    
    // NOT synchronized, unlike onDrawFrame: the two run on different threads (UI and GL) and
    // sharing the instance monitor makes every touch event wait for the frame in flight. A
    // frame takes as long as the scene makes it take - hundreds of milliseconds on a heavy
    // 3D terrain scene on a mid-range device - so a gesture's event stream queues up behind
    // the renderer until input dispatch times out and Android shows "isn't responding".
    // The native side has its own locking, and every other MapView method (setFocusPos,
    // getOptions, ...) already calls into baseMapView without this monitor - so it never
    // guarded against delete() racing a caller in the first place.
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (baseMapView == null) {
            return false;
        }

        boolean clickable = isClickable() || isLongClickable();
        if (!isEnabled() || !clickable) {
            return clickable;
        }

        try {
            int pointer1Index;
            int pointer2Index;
            switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                pointer1Index = event.getActionIndex();
                pointer1Id = event.getPointerId(pointer1Index);
                baseMapView.onInputEvent(NATIVE_ACTION_POINTER_1_DOWN, 
                        event.getX(pointer1Index), event.getY(pointer1Index), 
                        NATIVE_NO_COORDINATE, NATIVE_NO_COORDINATE);
                break;
            case MotionEvent.ACTION_POINTER_DOWN:
                if (event.getPointerCount() == 2) {
                    // Check which pointer to use
                    if (pointer1Id != INVALID_POINTER_ID) {
                        pointer1Index = event.findPointerIndex(pointer1Id);
                        pointer2Index = event.getActionIndex();
                        pointer2Id = event.getPointerId(event.getActionIndex());
                    } else if (pointer2Id != INVALID_POINTER_ID) {
                        pointer2Index = event.findPointerIndex(pointer2Id);
                        pointer1Index = event.getActionIndex();
                        pointer1Id = event.getPointerId(event.getActionIndex());
                    } else {
                        break;
                    }
                    baseMapView.onInputEvent(NATIVE_ACTION_POINTER_2_DOWN, 
                            event.getX(pointer1Index), event.getY(pointer1Index),
                            event.getX(pointer2Index), event.getY(pointer2Index));
                }
                break;
            case MotionEvent.ACTION_MOVE:
                if (pointer1Id != INVALID_POINTER_ID && pointer2Id == INVALID_POINTER_ID) {
                    pointer1Index = event.findPointerIndex(pointer1Id);
                    baseMapView.onInputEvent(NATIVE_ACTION_MOVE, 
                            event.getX(pointer1Index), event.getY(pointer1Index), 
                            NATIVE_NO_COORDINATE, NATIVE_NO_COORDINATE);
                } else if (pointer1Id != INVALID_POINTER_ID && pointer2Id != INVALID_POINTER_ID) {
                    pointer1Index = event.findPointerIndex(pointer1Id);
                    pointer2Index = event.findPointerIndex(pointer2Id);
                    baseMapView.onInputEvent(NATIVE_ACTION_MOVE, 
                            event.getX(pointer1Index), event.getY(pointer1Index), 
                            event.getX(pointer2Index), event.getY(pointer2Index));
                }
                break;
            case MotionEvent.ACTION_CANCEL:
                baseMapView.onInputEvent(NATIVE_ACTION_CANCEL, 
                        NATIVE_NO_COORDINATE, NATIVE_NO_COORDINATE, 
                        NATIVE_NO_COORDINATE, NATIVE_NO_COORDINATE);
                pointer1Id = INVALID_POINTER_ID;
                pointer2Id = INVALID_POINTER_ID;
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                int pointerIndex = event.getActionIndex();
                int pointerId = event.getPointerId(pointerIndex);
                // Single pointer
                if (pointer1Id == pointerId && pointer2Id == INVALID_POINTER_ID) {
                    pointer1Index = event.findPointerIndex(pointer1Id);
                    baseMapView.onInputEvent(NATIVE_ACTION_POINTER_1_UP, 
                            event.getX(pointer1Index), event.getY(pointer1Index), 
                            NATIVE_NO_COORDINATE, NATIVE_NO_COORDINATE);
                    pointer1Id = INVALID_POINTER_ID;
                    // Dual pointer, first pointer up
                } else if (pointer1Id == pointerId) {
                    pointer1Index = event.findPointerIndex(pointer1Id);
                    pointer2Index = event.findPointerIndex(pointer2Id);
                    baseMapView.onInputEvent(NATIVE_ACTION_POINTER_1_UP, 
                            event.getX(pointer1Index), event.getY(pointer1Index), 
                            event.getX(pointer2Index), event.getY(pointer2Index));
                    pointer1Id = pointer2Id;
                    pointer2Id = INVALID_POINTER_ID;
                    // Dual pointer, second finger up
                } else if (pointer2Id == pointerId) {
                    pointer1Index = event.findPointerIndex(pointer1Id);
                    pointer2Index = event.findPointerIndex(pointer2Id);
                    baseMapView.onInputEvent(NATIVE_ACTION_POINTER_2_UP, 
                            event.getX(pointer1Index), event.getY(pointer1Index), 
                            event.getX(pointer2Index), event.getY(pointer2Index));
                    pointer2Id = INVALID_POINTER_ID;
                }
                break;
            }
        }
        catch (IllegalArgumentException e) {
            com.massifmaps.utils.Log.error("MapView.onTouchEvent: " + e);
        }
        return true;
    }
    
    /**
     * Returns the Layers object, that can be used for adding and removing map layers.
     * @return The Layer object.
     */
    public Layers getLayers() {
        return baseMapView.getLayers();
    }

    /**
     * Returns the underlying BaseMapView, which is what carries the camera.
     *
     * For the facade: MassifApi.adopt("map", id, view.getBaseMapView()) makes moveTo, flyTo,
     * fitBounds, screenToMap, mapToScreen and stopFlight ordinary facade calls. An app using the
     * object API has no reason to reach for it - every one of those is on this class already.
     * @return the BaseMapView object.
     */
    public BaseMapView getBaseMapView() {
        return baseMapView;
    }

    /**
     * Returns the Options object, that can be used for modifying various map options.
     * @return the Option object.
     */
    public Options getOptions() {
        return baseMapView.getOptions();
    }

    /**
     * Returns the MapRenderer object, that can be used for controlling rendering related options.
     * @return the MapRenderer object.
     */
    public MapRenderer getMapRenderer() {
        return baseMapView.getMapRenderer();
    }

    /**
     * Returns the position that the camera is currently looking at.
     * @return The current focus position in the coordinate system of the base projection.
     */
    public MapPos getFocusPos() {
        return baseMapView.getFocusPos();
    }

    /**
     * Returns the map rotation in degrees. 0 means looking north, 90 means west, -90 means east and 180 means south.
     * @return The map rotation in degrees in range of (-180 .. 180].
     */
    public float getMapRotation() {
        return baseMapView.getRotation();
    }

    /**
     * Returns the tilt angle in degrees. 0 means looking directly at the horizon, 90 means looking directly down.
     * @return The tilt angle in degrees.
     */
    public float getTilt() {
        return baseMapView.getTilt();
    }

    /**
     * Returns the zoom level. The value returned is never negative, 0 means absolutely zoomed out and all other
     * values describe some level of zoom.
     * @return The zoom level.
     */
    public float getZoom() {
        return baseMapView.getZoom();
    }

    /**
     * Pans the view relative to the current focus position. The deltaPos vector is expected to be in
     * the coordinate system of the base projection. The new calculated focus position will be clamped to
     * the world bounds and to the bounds set by Options::setPanBounds.
     * 
     * If durationSeconds &gt; 0 the panning operation will be animated over time. If the previous panning animation has not
     * finished by the time this method is called, it will be stopped.
     * 
     * @param deltaPos The coordinate difference the map should be moved by.
     * @param durationSeconds The duration in which the tilting operation will be completed in seconds.
     */
    public void pan(MapVec deltaPos, float durationSeconds) {
        baseMapView.pan(deltaPos, durationSeconds);
    }

    /**
     * Sets the new absolute focus position. The new focus position is expected to be in
     * the coordinate system of the base projection. The new focus position will be clamped to
     * the world bounds and to the bounds set by Options::setPanBounds.
     * 
     * If durationSeconds &gt; 0 the panning operation will be animated over time. If the previous panning animation has not
     * finished by the time this method is called, it will be stopped.
     * 
     * @param pos The new focus point position in base coordinate system.
     * @param durationSeconds The duration in which the tilting operation will be completed in seconds.
     */
    public void setFocusPos(MapPos pos, float durationSeconds) {
        baseMapView.setFocusPos(pos, durationSeconds);
    }

    /**
     * Rotates the view relative to the current rotation value. Positive values rotate clockwise, negative values counterclockwise.
     * The new calculated rotation value will be wrapped to the range of (-180 .. 180]. Rotations are ignored if Options::setRotatable 
     * is set to false.
     * 
     * If durationSeconds &gt; 0 the rotating operation will be animated over time. If the previous rotating animation has not
     * finished by the time this method is called, it will be stopped.
     * 
     * @param deltaAngle The delta angle value in degrees.
     * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
     */
    public void rotate(float deltaAngle, float durationSeconds) {
        baseMapView.rotate(deltaAngle, durationSeconds);
    }

    /**
     * Points the camera at a position and a zoom level immediately, with no animation.
     *
     * Prefer it over setFocusPos + setZoom, which are clamped differently depending on which is
     * applied first, and unlike flyTo it needs no frame - so it also works before the map has
     * drawn. See BaseMapView.moveTo.
     * @param pos The target position in base projection coordinate system.
     * @param zoom The target zoom level.
     */
    public void moveTo(MapPos pos, float zoom) {
        baseMapView.moveTo(pos, zoom);
    }

    /**
     * The same, also setting rotation and tilt. See BaseMapView.moveTo.
     * @param pos The target position in base projection coordinate system.
     * @param zoom The target zoom level.
     * @param rotation The rotation in degrees.
     * @param tilt The tilt in degrees.
     */
    public void moveTo(MapPos pos, float zoom, float rotation, float tilt) {
        baseMapView.moveTo(pos, zoom, rotation, tilt);
    }

    /**
     * Moves the camera to a position and a zoom level in one animation, pulling back over a long
     * move and coming down at the target. See BaseMapView.flyTo.
     *
     * A duration of 0 is NOT immediate - it derives the duration from the path. For an immediate
     * move use moveTo.
     * @param pos The target position in base projection coordinate system.
     * @param zoom The target zoom level.
     * @param durationSeconds The duration in seconds, 0 to derive it from the length of the path.
     */
    public void flyTo(MapPos pos, float zoom, float durationSeconds) {
        baseMapView.flyTo(pos, zoom, durationSeconds);
    }

    /**
     * Moves the camera to a position, zoom, rotation and tilt in one animation.
     * @param pos The target position in base projection coordinate system.
     * @param zoom The target zoom level.
     * @param rotation The target rotation in degrees.
     * @param tilt The target tilt in degrees.
     * @param durationSeconds The duration in seconds, 0 to derive it from the length of the path.
     */
    public void flyTo(MapPos pos, float zoom, float rotation, float tilt, float durationSeconds) {
        baseMapView.flyTo(pos, zoom, rotation, tilt, durationSeconds);
    }

    /**
     * Moves the camera to a position, zoom, rotation and tilt in one animation, climbing over the
     * way there: the target position's Z is the height it ends at, and the climb is added as a
     * parabola, highest halfway and back to nothing at both ends.
     * @param pos The target position in base projection coordinate system; its Z is the target height.
     * @param zoom The target zoom level.
     * @param rotation The target rotation in degrees.
     * @param tilt The target tilt in degrees.
     * @param climbHeight The extra height at the middle of the path.
     * @param durationSeconds The duration in seconds, 0 to derive it from the length of the path.
     */
    public void flyTo(MapPos pos, float zoom, float rotation, float tilt, float climbHeight, float durationSeconds) {
        baseMapView.flyTo(pos, zoom, rotation, tilt, climbHeight, durationSeconds);
    }

    /**
     * How far along a flyTo animation is, from 0 to 1, or -1 when none is running.
     * @return The flight progress, or -1.
     */
    public float getFlightProgress() {
        return baseMapView.getFlightProgress();
    }

    /**
     * Stops a flyTo animation, leaving the camera where it is.
     */
    public void stopFlight() {
        baseMapView.stopFlight();
    }

    /**
     * Returns true while a flyTo animation is running.
     * @return True if the camera is in flight.
     */
    public boolean isFlightActive() {
        return baseMapView.isFlightActive();
    }


    /**
     * Rotates the view relative to the current rotation value. Positive values rotate clockwise, negative values counterclockwise.
     * The new calculated rotation value will be wrapped to the range of (-180 .. 180]. Rotations are ignored if Options::setRotatable
     * is set to false.
     * 
     * Rotating is done around the specified target position, keeping it at the same location on the screen.
     * 
     * If durationSeconds &gt; 0 the rotating operation will be animated over time. If the previous rotating animation has not
     * finished by the time this method is called, it will be stopped.
     * 
     * @param deltaAngle The delta angle value in degrees.
     * @param targetPos The zooming target position in the coordinate system of the base projection.
     * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
     */
    public void rotate(float deltaAngle, MapPos targetPos, float durationSeconds) {
        baseMapView.rotate(deltaAngle, targetPos, durationSeconds);
    }

    /**
     * Sets the new absolute rotation value. 0 means look north, 90 means west, -90 means east and 180 means south.
     * The rotation value will be wrapped to the range of (-180 .. 180]. Rotations are ignored if Options::setRotatable
     * is set to false.
     * 
     * If durationSeconds &gt; 0 the rotating operation will be animated over time. If the previous rotating animation has not
     * finished by the time this method is called, it will be stopped.
     * 
     * @param angle The new absolute rotation angle value in degrees.
     * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
     */
    public void setMapRotation(float angle, float durationSeconds) {
        baseMapView.setRotation(angle, durationSeconds);
    }

    /**
     * Sets the new absolute rotation value. 0 means look north, 90 means west, -90 means east and 180 means south.
     * The rotation value will be wrapped to the range of (-180 .. 180]. Rotations are ignored if Options::setRotatable 
     * is set to false.
     * 
     * Rotating is done around the specified target position, keeping it at the same location on the screen.
     * 
     * If durationSeconds &gt; 0 the rotating operation will be animated over time. If the previous rotating animation has not
     * finished by the time this method is called, it will be stopped.
     * 
     * @param angle The new absolute rotation angle value in degrees.
     * @param targetPos The zooming target position in the coordinate system of the base projection.
     * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
     */
    public void setMapRotation(float angle, MapPos targetPos, float durationSeconds) {
        baseMapView.setRotation(angle, targetPos, durationSeconds);
    }

    /**
     * Tilts the view relative to the current tilt value. Positive values tilt the view down towards the map, 
     * negative values tilt the view up towards the horizon. The new calculated tilt value will be clamped to
     * the range of [30 .. 90] and to the range set by Options::setZoomRange.
     * 
     * If durationSeconds &gt; 0 the tilting operation will be animated over time. If the previous tilting animation has not
     * finished by the time this method is called, it will be stopped.
     * @param deltaTilt The number of degrees the camera should be tilted by.
     * @param durationSeconds The duration in which the tilting operation will be completed in seconds.
     */
    public void tilt(float deltaTilt, float durationSeconds) {
        baseMapView.tilt(deltaTilt, durationSeconds);
    }

    /**
     * Sets the new absolute tilt value. 0 means look directly at the horizon, 90 means look directly down. The
     * minimum tilt angle is 30 degrees and the maximum is 90 degrees. The tilt value can be further constrained
     * by the Options::setTiltRange method. Values exceeding these ranges will be clamped.
     * 
     * If durationSeconds &gt; 0 the tilting operation will be animated over time. If the previous tilting animation has not
     * finished by the time this method is called, it will be stopped.
     * @param tilt The new absolute tilt value in degrees.
     * @param durationSeconds The duration in which the tilting operation will be completed in seconds.
     */
    public void setTilt(float tilt, float durationSeconds) {
        baseMapView.setTilt(tilt, durationSeconds);
    }

    /**
     * Zooms the view relative to the current zoom value. Positive values zoom in, negative values zoom out.
     * The new calculated zoom value will be clamped to the range of [0 .. 24] and to the range set by Options::setZoomRange.
     * 
     * If durationSeconds &gt; 0 the zooming operation will be animated over time. If the previous zooming animation has not
     * finished by the time this method is called, it will be stopped.
     * @param deltaZoom The delta zoom value.
     * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
     */
    public void zoom(float deltaZoom, float durationSeconds) {
        baseMapView.zoom(deltaZoom, durationSeconds);
    }

    /**
     * Zooms the view relative to the current zoom value. Positive values zoom in, negative values zoom out.
     * The new calculated zoom value will be clamped to the range of [0 .. 24] and to the range set by Options::setZoomRange.
     * 
     * Zooming is done towards the specified target position, keeping it at the same location on the screen.
     * 
     * If durationSeconds &gt; 0 the zooming operation will be animated over time. If the previous zooming animation has not
     * finished by the time this method is called, it will be stopped.
     * @param deltaZoom The delta zoom value.
     * @param targetPos The zooming target position in the coordinate system of the base projection.
     * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
     */
    public void zoom(float deltaZoom, MapPos targetPos, float durationSeconds) {
        baseMapView.zoom(deltaZoom, targetPos, durationSeconds);
    }

    /**
     * Sets the new absolute zoom value. The minimum zoom value is 0, which means absolutely zoomed out and the maximum
     * zoom value is 24. The zoom value can be further constrained by the Options::setZoomRange method. Values
     * exceeding these ranges will be clamped. 
     * 
     * If durationSeconds &gt; 0 the zooming operation will be animated over time. If the previous zooming animation has not
     * finished by the time this method is called, it will be stopped.
     * @param zoom The new absolute zoom value.
     * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
     */
    public void setZoom(float zoom, float durationSeconds) {
        baseMapView.setZoom(zoom, durationSeconds);
    }

    /**
     * Sets the new absolute zoom value. The minimum zoom value is 0, which means absolutely zoomed out and the maximum 
     * zoom value is 24. The zoom value can be further constrained by the Options::setZoomRange method. Values 
     * exceeding these ranges will be clamped.
     * 
     * Zooming is done towards the specified target position, keeping it at the same location on the screen.
     * 
     * If durationSeconds &gt; 0, the zooming operation will be animated over time. If the previous zooming animation has not
     * finished by the time this method is called, it will be stopped.
     * @param zoom The new absolute zoom value.
     * @param targetPos The zooming target position in the coordinate system of the base projection.
     * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
     */
    public void setZoom(float zoom, MapPos targetPos, float durationSeconds) {
        baseMapView.setZoom(zoom, targetPos, durationSeconds);
    }

    /**
     * Animate the view parameters (focus position, tilt, rotation, zoom) so that the specified bounding box becomes fully visible.
     * This method does not work before the screen size is set.
     * @param mapBounds The bounding box on the map to be made visible in the base projection's coordinate system.
     * @param screenBounds The screen bounding box where to fit the map bounding box.
     * @param integerZoom If true, then closest integer zoom level will be used. If false, exact fractional zoom level will be used.
     * @param durationSeconds The duration in which the operation will be completed in seconds.
     */
    public void moveToFitBounds(MapBounds mapBounds, ScreenBounds screenBounds, boolean integerZoom, float durationSeconds) {
        baseMapView.moveToFitBounds(mapBounds, screenBounds, integerZoom, durationSeconds);
    }

    /**
     * Animate the view parameters (focus position, tilt, rotation, zoom) so that the specified bounding box becomes fully visible.
     * Also supports resetting the tilt and rotation angles over the course of the animation.
     * This method does not work before the screen size is set.
     * @param mapBounds The bounding box on the map to be made visible in the base projection's coordinate system.
     * @param screenBounds The screen bounding box where to fit the map bounding box.
     * @param integerZoom If true, then closest integer zoom level will be used. If false, exact fractional zoom level will be used.
     * @param resetTilt If true, view will be untilted. If false, current tilt will be kept.
     * @param resetRotation If true, rotation will be reset. If false, current rotation will be kept.
     * @param durationSeconds The duration in which the operation will be completed in seconds.
     */
    public void moveToFitBounds(MapBounds mapBounds, ScreenBounds screenBounds, boolean integerZoom, boolean resetRotation, 
            boolean resetTilt, float durationSeconds) {
        baseMapView.moveToFitBounds(mapBounds, screenBounds, integerZoom, resetRotation, resetTilt, durationSeconds);
    }
    
    /**
     * Returns the map event listener. May be null.
     * @return The map event listener.
     */
    public MapEventListener getMapEventListener() {
        return baseMapView.getMapEventListener();
    }

    /**
     * Sets the map event listener. If a null pointer is passed no map events will be generated. The default is null.
     * @param mapEventListener The new map event listener.
     */
    public void setMapEventListener(MapEventListener mapEventListener) {
        baseMapView.setMapEventListener(mapEventListener);
    }

    /**
     * Calculates the map position corresponding to a screen position, using the current view parameters.
     * @param screenPos The screen position.
     * @return The calculated map position in base projection coordinate system. If the given screen position is not on the map, then NaNs are returned.
     */
    public MapPos screenToMap(ScreenPos screenPos) {
        return baseMapView.screenToMap(screenPos);
    }

    /**
     * Calculates the screen position corresponding to a map position, using the current view parameters.
     * @param mapPos The map position in base projection coordinate system.
     * @return The calculated screen position. Can be off-screen.
     */
    public ScreenPos mapToScreen(MapPos mapPos) {
        return baseMapView.mapToScreen(mapPos);
    }

    /**
     * Cancels all qued tasks such as tile and vector data fetches. Tasks that have already started
     * may continue until they finish. Tasks that are added after this method call are not affected.
     */
    public void cancelAllTasks() {
        baseMapView.cancelAllTasks();
    }

    /**
     * Releases the memory occupied by the preloading area. Calling this method releases some
     * memory if preloading is enabled, but means that the area right outside the visible area has to be
     * fetched again.
     */
    public void clearPreloadingCaches() {
        baseMapView.clearPreloadingCaches();
    }

    /**
     * Releases memory occupied by all caches. Calling this means that everything has to be fetched again,
     * including the visible area.
     */
    public void clearAllCaches() {
        baseMapView.clearAllCaches();	
    }
    
}
