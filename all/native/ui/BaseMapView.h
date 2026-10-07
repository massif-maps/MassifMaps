/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_BASEMAPVIEW_H_
#define _MASSIF_BASEMAPVIEW_H_

#include "ui/FlightEasing.h"

#include <memory>
#include <mutex>
#include <thread>

namespace massif {
    class CancelableThreadPool;
    class Layers;
    class MapBounds;
    class MapPos;
    class MapVec;
    class MapRenderer;
    class ScreenBounds;
    class ScreenPos;
    class Options;
    class MapEventListener;
    class RedrawRequestListener;
    class TouchHandler;
    
    /**
     * A platform independent main view class for all mapping operations.
     * Allows the user to manipulate the map and access various related components.
     */
    class BaseMapView {
    public:
        /**
         * Returns the SDK version and build info. The result should be used only for reporting purposes.
         * @return The SDK version and build info.
         */
        static std::string GetSDKVersion();
        
        BaseMapView();
        virtual ~BaseMapView();
    
        /**
         * Prepares renderers for drawing. Has to be called again if the graphics context was lost.
         */
        void onSurfaceCreated();
        /**
         * Changes the screen size of the map view. Calling this method before
         * onSurfaceCreated is called results in undefined behaviour.
         * @param width The new width of the map view.
         * @param height The new height of the map view.
         */
        void onSurfaceChanged(int width, int height);
        /**
         * Draws a single frame to the current graphics context. Calling this method before
         * onSurfaceCreated and onSurfaceChanged are called results in undefined behaviour.
         */
        void onDrawFrame();
        /**
         * Stops renderer. Rendering may resume only after onSurfaceCreated is called again.
         */
        void onSurfaceDestroyed();

        /**
         * Finish all rendering (wait until all rendering commands have finished executing).
         */
        void finishRendering();

        /**
         * Handles a user input event.
         * @param event The event type. First pointer down = 0, second pointer down = 1, either pointer moved = 2, 
         *              gesture canceled = 3, first pointer up = 4, second pointer up = 5.
         * @param x1 The x coordinate of the first pointer. -1 if there are no coordinates.
         * @param y1 The y coordinate of the first pointer. -1 if there are no coordinates.
         * @param x2 The x coordinate of the second pointer. -1 if there are no coordinates.
         * @param y2 The y coordinate of the second pointer. -1 if there are no coordinates.
         */
        void onInputEvent(int event, float x1, float y1, float x2, float y2);

        /**
         * Handles a wheel-rotation event.
         * @param delta The number of ticks wheel changed with sign showing the direction of change.
         * @param x The x coordinate of the pointer.
         * @param y The y coordinate of the pointer.
         */
        void onWheelEvent(int delta, float x, float y);

        /**
         * Returns the Layers object, that can be used for adding and removing map layers.
         * @return The Layer object.
         */
        const std::shared_ptr<Layers>& getLayers() const;
        /**
         * Returns the Options object, that can be used for modifying various map options.
         * @return the Option object.
         */
        const std::shared_ptr<Options>& getOptions() const;
        /**
         * Returns the MapRenderer object, that can be used for controlling rendering options.
         * @return the MapRenderer object.
         */
        const std::shared_ptr<MapRenderer>& getMapRenderer() const;
    
        /**
         * Returns the position that the camera is currently looking at.
         * @return The current focus position in the coordinate system of the base projection.
         */
        MapPos getFocusPos() const;
        /**
         * Returns the position the camera itself is above (the viewpoint), which at a low tilt is far from the
         * focus it looks at. Where a top-down view has to be centred to come back to the same place.
         * @return The camera's ground position in the coordinate system of the base projection.
         */
        MapPos getCameraPos() const;
        /**
         * Returns the map rotation in degrees. 0 means looking north, 90 means west, -90 means east and 180 means south.
         * @return The map rotation in degrees in range of (-180 .. 180].
         */
        float getRotation() const;
        
        /**
         * Returns the tilt angle in degrees. 0 means looking directly at the horizon, 90 means looking directly down.
         * @return The tilt angle in degrees.
         */
        float getTilt() const;
        /**
         * Returns the zoom level. The value returned is never negative, 0 means absolutely zoomed out and all other
         * values describe some level of zoom.
         * @return The zoom level.
         */
        float getZoom() const;
    
        /**
         * Pans the view relative to the current focus position, deltaPos in the base projection's coordinate system.
         * The new focus is clamped to the world bounds and to Options::setPanBounds.
         * If durationSeconds > 0 the pan is animated; a previous pan animation still running is stopped.
         * @param deltaPos The relative coordinate shift.
         * @param durationSeconds The duration in which the panning operation will be completed in seconds.
         */
        void pan(const MapVec& deltaPos, float durationSeconds);
        /**
         * Sets the new absolute focus position, in the base projection's coordinate system, clamped to the world
         * bounds and to Options::setPanBounds.
         * If durationSeconds > 0 the pan is animated; a previous pan animation still running is stopped.
         * @param pos The new absolute focus position.
         * @param durationSeconds The duration in which the panning operation will be completed in seconds.
         */
        void setFocusPos(const MapPos& pos, float durationSeconds);

        /**
         * Points the camera at a position and a zoom level immediately, with no animation, and before the first
         * frame too. Prefer it over setFocusPos + setZoom: with restricted panning, a focus set at a world view
         * is clamped to the middle of the pan bounds and the zoom that follows does not undo it.
         * @param pos The target position in base projection coordinate system.
         * @param zoom The target zoom level.
         */
        void moveTo(const MapPos& pos, float zoom);
        /**
         * The same, also setting rotation and tilt. See moveTo.
         * @param pos The target position in base projection coordinate system.
         * @param zoom The target zoom level.
         * @param rotation The rotation in degrees.
         * @param tilt The tilt in degrees.
         */
        void moveTo(const MapPos& pos, float zoom, float rotation, float tilt);

        /**
         * Puts the camera itself at a position, where moveTo places the focus it looks at: for a
         * first-person or panorama view. Rotation and tilt are applied first. Exact on a planar
         * projection; on the globe a very long move lands close rather than exact.
         * @param pos The target camera position in base projection coordinate system.
         * @param zoom The target zoom level.
         * @param rotation The rotation in degrees.
         * @param tilt The tilt in degrees.
         */
        void moveCameraTo(const MapPos& pos, float zoom, float rotation, float tilt);
        /**
         * The same, keeping the current rotation and tilt. See moveCameraTo.
         * @param pos The target camera position in base projection coordinate system.
         * @param zoom The target zoom level.
         */
        void moveCameraTo(const MapPos& pos, float zoom);

        /**
         * Moves the camera to a position and a zoom level in one animation, pulling back over a long move and
         * coming down at the target (Van Wijk & Nuij's optimal path). Asked for before the first frame, the
         * flight runs from that frame.
         * @param pos The target position in base projection coordinate system.
         * @param zoom The target zoom level.
         * @param durationSeconds The duration in seconds, or 0 to derive it from the length of the path.
         *                        0 is not "immediate"; for that use moveTo.
         */
        void flyTo(const MapPos& pos, float zoom, float durationSeconds);
        /**
         * Moves the camera to a position, zoom, rotation and tilt in one animation. See flyTo.
         * @param pos The target position in base projection coordinate system.
         * @param zoom The target zoom level.
         * @param rotation The target rotation in degrees.
         * @param tilt The target tilt in degrees.
         * @param durationSeconds The duration in seconds, or 0 to derive it from the path.
         */
        void flyTo(const MapPos& pos, float zoom, float rotation, float tilt, float durationSeconds);
        /**
         * Moves the camera to a position, zoom, rotation and tilt in one animation, climbing on the way: the
         * climb is added to the target Z (the final viewpoint height) as a parabola, highest halfway.
         * @param pos The target position in base projection coordinate system; its Z is the target height.
         * @param zoom The target zoom level.
         * @param rotation The target rotation in degrees.
         * @param tilt The target tilt in degrees.
         * @param climbHeight The extra height at the middle of the path, in the base projection's units.
         * @param durationSeconds The duration in seconds, or 0 to derive it from the path.
         */
        void flyTo(const MapPos& pos, float zoom, float rotation, float tilt, float climbHeight, float durationSeconds);
        /**
         * Moves the camera as the overload above, on a chosen timing curve. The others fly on
         * FLIGHT_EASING_EASE.
         * @param pos The target position in base projection coordinate system; its Z is the target height.
         * @param zoom The target zoom level.
         * @param rotation The target rotation in degrees.
         * @param tilt The target tilt in degrees.
         * @param climbHeight The extra height at the middle of the path, in the base projection's units.
         * @param durationSeconds The duration in seconds, or 0 to derive it from the path.
         * @param easing The timing curve the whole move runs on.
         */
        void flyTo(const MapPos& pos, float zoom, float rotation, float tilt, float climbHeight, float durationSeconds, FlightEasing::FlightEasing easing);
        /**
         * Stops a flight started with flyTo, leaving the camera where it is.
         */
        void stopFlight();
        /**
         * Returns true while a flyTo animation is running.
         * @return True if the camera is in flight.
         */
        bool isFlightActive() const;
        /**
         * How far along a flyTo animation is, from 0 to 1, or -1 when none is running. The value the camera is
         * actually at, so an app animating its own state alongside the move reads it rather than its own clock.
         * @return The flight progress, or -1.
         */
        float getFlightProgress() const;
        
        /**
         * Rotates the view relative to the current rotation, positive clockwise, wrapped to (-180 .. 180].
         * Ignored if Options::setRotatable is false.
         * If durationSeconds > 0 the rotation is animated; a previous rotation animation still running is stopped.
         * @param deltaAngle The delta rotation value in degrees.
         * @param durationSeconds The duration in which the rotation operation will be completed in seconds.
         */
        void rotate(float deltaAngle, float durationSeconds);
        /**
         * Rotates the view relative to the current rotation, positive clockwise, wrapped to (-180 .. 180], around
         * targetPos, which keeps its screen location. Ignored if Options::setRotatable is false.
         * If durationSeconds > 0 the rotation is animated; a previous rotation animation still running is stopped.
         * @param deltaAngle The delta angle value in degrees.
         * @param targetPos The zooming target position in the coordinate system of the base projection.
         * @param durationSeconds The duration in which the rotation operation will be completed in seconds.
         */
        void rotate(float deltaAngle, const MapPos& targetPos, float durationSeconds);
        /**
         * Sets the new absolute rotation: 0 looks north, 90 west, -90 east, 180 south; wrapped to (-180 .. 180].
         * Ignored if Options::setRotatable is false.
         * If durationSeconds > 0 the rotation is animated; a previous rotation animation still running is stopped.
         * @param angle The new absolute angle value in degrees.
         * @param durationSeconds The duration in which the rotation operation will be completed in seconds.
         */
        void setRotation(float angle, float durationSeconds);
        /**
         * Sets the new absolute rotation (0 north, 90 west, -90 east, 180 south; wrapped to (-180 .. 180]) around
         * targetPos, which keeps its screen location. Ignored if Options::setRotatable is false.
         * If durationSeconds > 0 the rotation is animated; a previous rotation animation still running is stopped.
         * @param angle The new absolute angle value in degrees.
         * @param targetPos The zooming target position in the coordinate system of the base projection.
         * @param durationSeconds The duration in which the rotation operation will be completed in seconds.
         */
        void setRotation(float angle, const MapPos& targetPos, float durationSeconds);
        
        /**
         * Tilts the view relative to the current tilt: positive down towards the map, negative up towards the horizon.
         * The result is clamped to the range set by Options::setTiltRange.
         * If durationSeconds > 0 the tilt is animated; a previous tilt animation still running is stopped.
         * @param deltaTilt The number of degrees the camera should be tilted by.
         * @param durationSeconds The duration in which the tilting operation will be completed in seconds.
         */
        void tilt(float deltaTilt, float durationSeconds);
        /**
         * Sets the new absolute tilt: 0 looks at the horizon, 90 straight down. Clamped to the range set by
         * Options::setTiltRange.
         * If durationSeconds > 0 the tilt is animated; a previous tilt animation still running is stopped.
         * @param tilt The new absolute tilt value in degrees.
         * @param durationSeconds The duration in which the tilting operation will be completed in seconds.
         */
        void setTilt(float tilt, float durationSeconds);
        
        /**
         * Zooms the view relative to the current zoom, positive in, negative out; clamped to [0 .. 24] and to
         * Options::setZoomRange.
         * If durationSeconds > 0 the zoom is animated; a previous zoom animation still running is stopped.
         * @param deltaZoom The delta zoom value.
         * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
         */
        void zoom(float deltaZoom, float durationSeconds);
        /**
         * Zooms the view relative to the current zoom (positive in, negative out; clamped to [0 .. 24] and to
         * Options::setZoomRange) towards targetPos, which keeps its screen location.
         * If durationSeconds > 0 the zoom is animated; a previous zoom animation still running is stopped.
         * @param deltaZoom The delta zoom value.
         * @param targetPos The zooming target position in the coordinate system of the base projection.
         * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
         */
        void zoom(float deltaZoom, const MapPos& targetPos, float durationSeconds);
        /**
         * Sets the new absolute zoom, clamped to [0 .. 24] (0 is fully zoomed out) and to Options::setZoomRange.
         * If durationSeconds > 0 the zoom is animated; a previous zoom animation still running is stopped.
         * @param zoom The new absolute zoom value.
         * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
         */
        void setZoom(float zoom, float durationSeconds);
        /**
         * Sets the new absolute zoom, clamped to [0 .. 24] and to Options::setZoomRange, towards targetPos, which
         * keeps its screen location.
         * If durationSeconds > 0 the zoom is animated; a previous zoom animation still running is stopped.
         * @param zoom The new absolute zoom value.
         * @param targetPos The zooming target position in the coordinate system of the base projection.
         * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
         */
        void setZoom(float zoom, const MapPos& targetPos, float durationSeconds);
        
        /**
         * Animate the view parameters (focus position, tilt, rotation, zoom) so that the specified bounding box becomes fully visible.
         * This method does not work before the screen size is set.
         * @param mapBounds The bounding box on the map to be made visible in the base projection's coordinate system.
         * @param screenBounds The screen bounding box where to fit the map bounding box.
         * @param integerZoom If true, then closest integer zoom level will be used. If false, exact fractional zoom level will be used.
         * @param durationSeconds The duration in which the operation will be completed in seconds.
         */
        void moveToFitBounds(const MapBounds& mapBounds, const ScreenBounds& screenBounds, bool integerZoom, float durationSeconds);
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
        void moveToFitBounds(const MapBounds& mapBounds, const ScreenBounds& screenBounds, bool integerZoom, bool resetRotation, bool resetTilt, float durationSeconds);
        
        /**
         * Returns the map event listener. May be null.
         * @return The map event listener.
         */
        std::shared_ptr<MapEventListener> getMapEventListener() const;
        /**
         * Sets the map event listener. If a null pointer is passed no map events will be generated. The default is null.
         * @param mapEventListener The new map event listener.
         */
        void setMapEventListener(const std::shared_ptr<MapEventListener>& mapEventListener);
        
        /**
         * Returns the redraw request listener.
         * @return The redraw request listener.
         */
        std::shared_ptr<RedrawRequestListener> getRedrawRequestListener() const;
        /**
         * Sets the listener which will notified when the map needs to be redrawn
         * @param listener The redraw listener.
         */
        void setRedrawRequestListener(const std::shared_ptr<RedrawRequestListener>& listener);

        /**
         * Calculates the map position corresponding to a screen position, using the current view parameters.
         * @param screenPos The screen position.
         * @return The calculated map position in base projection coordinate system. If the given screen position is not on the map, NaNs are returned.
         */
        MapPos screenToMap(const ScreenPos& screenPos);
        /**
         * Calculates the screen position corresponding to a map position, using the current view parameters.
         * @param mapPos The map position in base projection coordinate system.
         * @return The calculated screen position. Can be off-screen.
         */
        ScreenPos mapToScreen(const MapPos& mapPos);
        
        /**
         * Cancels all qued tasks such as tile and vector data fetches. Tasks that have already started
         * may continue until they finish. Tasks that are added after this method call are not affected.
         */
        void cancelAllTasks();
    
        /**
         * Releases the memory occupied by the preloading area. Calling this method releases some
         * memory if preloading is enabled, but means that the area right outside the visible area has to be
         * fetched again.
         */
        void clearPreloadingCaches();
    
        /**
         * Releases memory occupied by all caches. Calling this means that everything has to be fetched again,
         * including the visible area.
         */
        void clearAllCaches();
    
    protected:
        void stopCameraAnimations();
        // A continuous zoom delta for a host whose wheel is not in ticks; pivots on the ground under the pointer.
        void onWheelZoom(float zoomDelta, float x, float y);
        // The ground under a screen point, internal: what a mouse orbit turns about (Options::setOrbitAroundPivot).
        MapPos calculateOrbitPivot(float x, float y);
        void orbit(float rotationDelta, float tiltDelta, const MapPos& pivotPos);

    private:
        // Rotation and tilt optional, so both public moveTo overloads are the same code.
        void moveTo(const MapPos& pos, float zoom, const float* rotation, const float* tilt);
        /** Null rotation/tilt keeps the current one, as with moveTo. */
        void moveCameraTo(const MapPos& pos, float zoom, const float* rotation, const float* tilt);

        std::shared_ptr<CancelableThreadPool> _envelopeThreadPool;
        std::shared_ptr<CancelableThreadPool> _tileThreadPool;
        std::shared_ptr<Options> _options;
        std::shared_ptr<Layers> _layers;
        std::shared_ptr<MapRenderer> _mapRenderer;
        
        std::shared_ptr<TouchHandler> _touchHandler;
        
        mutable std::mutex _mutex;
    };
    
}

#endif
