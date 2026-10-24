package com.massifmaps.MassifDemo.demo;

import com.massifmaps.api.MassifInterop;
import com.massifmaps.components.Layers;
import com.massifmaps.components.Options;
import com.massifmaps.renderers.MapRenderer;
import com.massifmaps.ui.BaseMapView;
import com.massifmaps.ui.MapEventListener;
import com.massifmaps.ui.MapViewInterface;

/**
 * A MapView no longer carries the object API: the demo, which still builds with it, reaches the
 * native map's Options, Layers and view through MassifInterop with the view's handles.
 */
public final class ObjectApi {

    private ObjectApi() {
    }

    public static Options options(MapViewInterface view) {
        return MassifInterop.getOptionsByHandle(view.getMapHandle());
    }

    public static Layers layers(MapViewInterface view) {
        return MassifInterop.getLayerListByHandle(view.getLayersHandle());
    }

    /** The view, which carries the camera: focus, zoom, tilt, rotation and the flights. */
    public static BaseMapView view(MapViewInterface view) {
        return MassifInterop.getViewByHandle(view.getViewHandle());
    }

    public static MapRenderer renderer(MapViewInterface view) {
        return view(view).getMapRenderer();
    }

    /**
     * Installs a listener beside the facade's: the view already emits the map's events as facade
     * events, so the bridge is rebuilt around the app's listener instead of being replaced by it.
     */
    public static void setMapEventListener(MapViewInterface view, MapEventListener listener) {
        view(view).setMapEventListener(MassifInterop.createEventBridge(view.getMapHandle(), listener));
    }
}
