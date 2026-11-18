package com.massifmaps.ui;

import android.content.Context;
import android.content.res.AssetManager;

/**
 * The few native functions a map view needs, written by hand over api/MapViewHost.h. A view is
 * named by the handle {@link #create} returns and nothing else: no object-API proxy is involved.
 * It is not intended for public usage.
 * @hidden
 */
final class MapViewNative {

    private static Throwable libraryLoadingErrorCause;

    static {
        try {
            System.loadLibrary("massif");
        } catch (Throwable cause) {
            android.util.Log.e("massif", "Failed to initialize Massif Maps, native .so library failed to load?", cause);
            libraryLoadingErrorCause = cause;
        }
    }

    private MapViewNative() {
    }

    static Throwable getLibraryLoadingErrorCause() {
        return libraryLoadingErrorCause;
    }

    /** Connects the JVM, the context and the asset manager to the SDK. Repeating it is harmless. */
    static native void init(Context context, AssetManager assets);

    /**
     * @param redraw Run on whichever thread asks for a frame; it should call requestRender().
     * @return The view's handle, or 0 when the id is taken.
     */
    static native int create(String id, float dpi, Runnable redraw);

    static native void destroy(String id);

    static native void surfaceCreated(int view);

    static native void surfaceChanged(int view, int width, int height);

    static native void surfaceDestroyed(int view);

    static native void drawFrame(int view);

    static native void input(int view, int event, float x1, float y1, float x2, float y2);
}
