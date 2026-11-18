package com.massifmaps.ui;

import java.lang.ref.WeakReference;
import java.util.concurrent.atomic.AtomicInteger;

import android.content.Context;
import android.content.res.AssetManager;
import android.view.MotionEvent;
import android.view.View;

import com.massifmaps.api.MassifApi;

/**
 * What MapView and TextureMapView share: the native view they own through its handle, and the
 * translation of a touch into the native input events. It is not intended for public usage.
 * @hidden
 */
final class MapViewHandle {

    private static final int NATIVE_ACTION_POINTER_1_DOWN = 0;
    private static final int NATIVE_ACTION_POINTER_2_DOWN = 1;
    private static final int NATIVE_ACTION_MOVE = 2;
    private static final int NATIVE_ACTION_CANCEL = 3;
    private static final int NATIVE_ACTION_POINTER_1_UP = 4;
    private static final int NATIVE_ACTION_POINTER_2_UP = 5;
    private static final int NATIVE_NO_COORDINATE = -1;

    private static final int INVALID_POINTER_ID = -1;

    private static final AtomicInteger counter = new AtomicInteger();

    private final String id;
    private volatile int handle;

    private int pointer1Id = INVALID_POINTER_ID;
    private int pointer2Id = INVALID_POINTER_ID;
    private boolean gestureAccepted;

    // A view only being edited in a layout editor has no native peer: handle 0.
    MapViewHandle(Context context, String id, boolean createNative, final WeakReference<View> view) {
        this.id = id != null ? id : "mapview-" + counter.incrementAndGet();
        if (!createNative) {
            return;
        }
        AssetManager assets = context.getApplicationContext().getAssets();
        MapViewNative.init(context, assets);
        handle = MapViewNative.create(this.id, context.getResources().getDisplayMetrics().densityDpi,
                new Runnable() {
                    @Override
                    public void run() {
                        View target = view.get();
                        if (target instanceof android.opengl.GLSurfaceView) {
                            ((android.opengl.GLSurfaceView) target).requestRender();
                        } else if (target instanceof GLTextureView) {
                            ((GLTextureView) target).requestRender();
                        }
                    }
                });
        if (handle == 0) {
            throw new IllegalStateException("A map view is already registered under the id '" + this.id + "'");
        }
    }

    String getId() {
        return id;
    }

    int getViewHandle() {
        return handle;
    }

    int getMapHandle() {
        return MassifApi.findObject("map", id);
    }

    int getLayersHandle() {
        return MassifApi.findObject("layers", id);
    }

    void release() {
        if (handle != 0) {
            handle = 0;
            MapViewNative.destroy(id);
        }
    }

    void surfaceCreated() {
        if (handle != 0) {
            MapViewNative.surfaceCreated(handle);
        }
    }

    void surfaceChanged(int width, int height) {
        if (handle != 0) {
            MapViewNative.surfaceChanged(handle, width, height);
        }
    }

    void drawFrame() {
        if (handle != 0) {
            MapViewNative.drawFrame(handle);
        }
    }

    /** One of the facade's view calls that takes no arguments, e.g. "clearAllCaches". */
    void call(String method) {
        if (handle != 0) {
            MassifApi.call(handle, method);
        }
    }

    // Not synchronized, unlike drawFrame: touches would wait for the frame in flight until input
    // dispatch times out ("isn't responding"). The native side has its own locking.
    boolean onTouchEvent(View view, MotionEvent event) {
        final int target = handle;
        if (target == 0) {
            return false;
        }

        // Latched per gesture: a host that flips clickable mid-touch would deliver the DOWN and
        // drop the UP, and the native pointer count never comes back down.
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
            gestureAccepted = view.isEnabled() && (view.isClickable() || view.isLongClickable());
        }
        if (!gestureAccepted) {
            return view.isClickable() || view.isLongClickable();
        }

        try {
            int pointer1Index;
            int pointer2Index;
            switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                pointer1Index = event.getActionIndex();
                pointer1Id = event.getPointerId(pointer1Index);
                MapViewNative.input(target, NATIVE_ACTION_POINTER_1_DOWN,
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
                    MapViewNative.input(target, NATIVE_ACTION_POINTER_2_DOWN,
                            event.getX(pointer1Index), event.getY(pointer1Index),
                            event.getX(pointer2Index), event.getY(pointer2Index));
                }
                break;
            case MotionEvent.ACTION_MOVE:
                if (pointer1Id != INVALID_POINTER_ID && pointer2Id == INVALID_POINTER_ID) {
                    pointer1Index = event.findPointerIndex(pointer1Id);
                    MapViewNative.input(target, NATIVE_ACTION_MOVE,
                            event.getX(pointer1Index), event.getY(pointer1Index),
                            NATIVE_NO_COORDINATE, NATIVE_NO_COORDINATE);
                } else if (pointer1Id != INVALID_POINTER_ID && pointer2Id != INVALID_POINTER_ID) {
                    pointer1Index = event.findPointerIndex(pointer1Id);
                    pointer2Index = event.findPointerIndex(pointer2Id);
                    MapViewNative.input(target, NATIVE_ACTION_MOVE,
                            event.getX(pointer1Index), event.getY(pointer1Index),
                            event.getX(pointer2Index), event.getY(pointer2Index));
                }
                break;
            case MotionEvent.ACTION_CANCEL:
                MapViewNative.input(target, NATIVE_ACTION_CANCEL,
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
                    MapViewNative.input(target, NATIVE_ACTION_POINTER_1_UP,
                            event.getX(pointer1Index), event.getY(pointer1Index),
                            NATIVE_NO_COORDINATE, NATIVE_NO_COORDINATE);
                    pointer1Id = INVALID_POINTER_ID;
                    // Dual pointer, first pointer up
                } else if (pointer1Id == pointerId) {
                    pointer1Index = event.findPointerIndex(pointer1Id);
                    pointer2Index = event.findPointerIndex(pointer2Id);
                    MapViewNative.input(target, NATIVE_ACTION_POINTER_1_UP,
                            event.getX(pointer1Index), event.getY(pointer1Index),
                            event.getX(pointer2Index), event.getY(pointer2Index));
                    pointer1Id = pointer2Id;
                    pointer2Id = INVALID_POINTER_ID;
                    // Dual pointer, second finger up
                } else if (pointer2Id == pointerId) {
                    pointer1Index = event.findPointerIndex(pointer1Id);
                    pointer2Index = event.findPointerIndex(pointer2Id);
                    MapViewNative.input(target, NATIVE_ACTION_POINTER_2_UP,
                            event.getX(pointer1Index), event.getY(pointer1Index),
                            event.getX(pointer2Index), event.getY(pointer2Index));
                    pointer2Id = INVALID_POINTER_ID;
                }
                break;
            }
        } catch (IllegalArgumentException e) {
            android.util.Log.e("massif", "MapView.onTouchEvent: " + e);
            // The event never reached native: cancel, or the pointer count stays up and onMapStable never fires.
            pointer1Id = INVALID_POINTER_ID;
            pointer2Id = INVALID_POINTER_ID;
            MapViewNative.input(target, NATIVE_ACTION_CANCEL,
                    NATIVE_NO_COORDINATE, NATIVE_NO_COORDINATE,
                    NATIVE_NO_COORDINATE, NATIVE_NO_COORDINATE);
        }
        return true;
    }
}
