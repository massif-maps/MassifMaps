package com.massifmaps.ui;

import java.lang.ref.WeakReference;
import java.lang.reflect.Method;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

import android.content.Context;
import android.content.res.TypedArray;
import android.opengl.GLSurfaceView;
import android.util.AttributeSet;
import android.view.MotionEvent;

/**
 * MapView is a view class supporting map rendering and interaction.
 *
 * The view owns a native map and is addressed through the facade: {@link #getViewHandle()},
 * {@link #getMapHandle()} and {@link #getLayersHandle()} are facade handles, and the camera,
 * the options, the layers and the events are all reached with them (see MassifMap).
 */
public class MapView extends GLSurfaceView implements GLSurfaceView.Renderer, MapViewInterface {

    private final MapViewHandle host;

    /**
     * Creates a new MapView object from a context object.
     * @param context The context object.
     */
    public MapView(Context context) {
        this(context, null, null);
    }

    /**
     * Creates a new MapView object from a context object and attributes.
     * @param context The context object.
     * @param attrs The attributes.
     */
    public MapView(Context context, AttributeSet attrs) {
        this(context, attrs, null);
    }

    /**
     * Creates a new MapView object under an id. The id names the map in the facade, so two maps
     * need two ids; without one the view takes a unique "mapview-N".
     * @param context The context object.
     * @param id The id of the map, or null.
     */
    public MapView(Context context, String id) {
        this(context, null, id);
    }

    private MapView(Context context, AttributeSet attrs, String id) {
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
            android.util.Log.w("massif", "MapView: Failed to read attributes");
        }
        setClickable(clickable);
        setLongClickable(longClickable);

        host = new MapViewHandle(context, id, !isInEditMode(), new WeakReference<android.view.View>(this));

        if (!isInEditMode()) {
            // Set up relevant EGL state
            try {
                Method m = GLSurfaceView.class.getMethod("setPreserveEGLContextOnPause", Boolean.TYPE);
                m.invoke(this, true);
            } catch (Exception e) {
                android.util.Log.i("massif", "MapView: Preserving EGL context on pause is not possible: " + e);
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
     * does not paint. Combine it with a transparent clear color (the map options' clearColor),
     * which is what leaves the frame empty; the SDK renders with premultiplied alpha, so the
     * result composites correctly.
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

    @Override
    public String getMapId() {
        return host.getId();
    }

    @Override
    public int getViewHandle() {
        return host.getViewHandle();
    }

    @Override
    public int getMapHandle() {
        return host.getMapHandle();
    }

    @Override
    public int getLayersHandle() {
        return host.getLayersHandle();
    }

    @Override
    public void cancelAllTasks() {
        host.call("cancelAllTasks");
    }

    @Override
    public void clearPreloadingCaches() {
        host.call("clearPreloadingCaches");
    }

    @Override
    public void clearAllCaches() {
        host.call("clearAllCaches");
    }

    /**
     * Deletes the resources associated with the MapView.
     * The method can be used to dispose native objects immediately,
     * without waiting for next GC cycle.
     */
    @Override
    public synchronized void delete() {
        host.release();
    }

    @Override
    public synchronized void onSurfaceCreated(GL10 gl, EGLConfig config) {
        host.surfaceCreated();
    }

    @Override
    public synchronized void onSurfaceChanged(GL10 gl, int width, int height) {
        host.surfaceChanged(width, height);
    }

    @Override
    public synchronized void onDrawFrame(GL10 gl) {
        host.drawFrame();
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        return host.onTouchEvent(this, event);
    }
}
