package com.massifmaps.ui;

import java.lang.ref.WeakReference;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

import android.content.Context;
import android.content.res.TypedArray;
import android.opengl.GLSurfaceView;
import android.util.AttributeSet;
import android.view.MotionEvent;

/**
 * TextureMapView is a MapView drawn into a TextureView: an ordinary view in the hierarchy, which
 * blends with the views around it. It is addressed through the facade like MapView.
 */
public class TextureMapView extends GLTextureView implements GLSurfaceView.Renderer, MapViewInterface {

    private final MapViewHandle host;

    /**
     * Creates a new TextureMapView object from a context object.
     * @param context The context object.
     */
    public TextureMapView(Context context) {
        this(context, null, null);
    }

    /**
     * Creates a new TextureMapView object from a context object and attributes.
     * @param context The context object.
     * @param attrs The attributes.
     */
    public TextureMapView(Context context, AttributeSet attrs) {
        this(context, attrs, null);
    }

    /**
     * Creates a new TextureMapView object under an id. The id names the map in the facade, so two
     * maps need two ids; without one the view takes a unique "mapview-N".
     * @param context The context object.
     * @param id The id of the map, or null.
     */
    public TextureMapView(Context context, String id) {
        this(context, null, id);
    }

    private TextureMapView(Context context, AttributeSet attrs, String id) {
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
            setPreserveEGLContextOnPause(true);
            // OpenGL ES 3.0 is required, no probe and no fallback (see MapView)
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
     * A TextureMapView is a TextureView, i.e. an ordinary view in the hierarchy, so unlike MapView
     * it blends with the views BEHIND it in the layout as well as with anything the window is
     * drawn over. That makes it the one to use for a map over other UI, and for a camera preview
     * held in a TextureView.
     * @param translucent True to make the view translucent.
     */
    public void setTranslucent(boolean translucent) {
        setOpaque(!translucent);
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
     * Deletes the resources associated with the view.
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
