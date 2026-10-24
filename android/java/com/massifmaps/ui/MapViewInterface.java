package com.massifmaps.ui;

/**
 * MapViewInterface is a common interface for map rendering and interaction implementations.
 *
 * A map view owns a native map and is addressed through the facade: everything else about it,
 * the camera, the options, the layers and the events, goes through the handles below (see
 * com.massifmaps.api.MassifMap).
 */
public interface MapViewInterface {

    /**
     * The id the map is registered under in the facade, shared by its view, map and layers.
     * Not View.getId(), which is the layout resource id.
     * @return The map id.
     */
    public String getMapId();

    /**
     * The facade handle of the native view, which carries the camera. 0 once deleted.
     * @return The handle.
     */
    public int getViewHandle();

    /**
     * The facade handle of the map's options (fog, sky, terrain, projection...), also the target of
     * the map's events. 0 once deleted.
     * @return The handle.
     */
    public int getMapHandle();

    /**
     * The facade handle of the map's layer list, which layers are added to and removed from.
     * @return The handle.
     */
    public int getLayersHandle();

    /**
     * Cancels all qued tasks such as tile and vector data fetches. Tasks that have already started
     * may continue until they finish. Tasks that are added after this method call are not affected.
     */
    public void cancelAllTasks();

    /**
     * Releases the memory occupied by the preloading area. Calling this method releases some
     * memory if preloading is enabled, but means that the area right outside the visible area has to be
     * fetched again.
     */
    public void clearPreloadingCaches();

    /**
     * Releases memory occupied by all caches. Calling this means that everything has to be fetched again,
     * including the visible area.
     */
    public void clearAllCaches();

    /**
     * Deletes the native map. The view can be used no more.
     */
    public void delete();
}
