/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_BASEMAPVIEW_H_
#define _MASSIF_BASEMAPVIEW_H_

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
         * Returns the position the camera itself is above, which at a low tilt is nowhere near the
         * focus - the focus is what the camera looks AT, kilometres out in front of it. This is the
         * viewpoint: where a first-person camera stands, and where a top-down view has to be centred
         * to come back to the same place.
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
         * Pans the view relative to the current focus position. The deltaPos vector is expected to be in 
         * the coordinate system of the base projection. The new calculated focus position will be clamped to
         * the world bounds and to the bounds set by Options::setPanBounds.
         *
         * If durationSeconds > 0 the panning operation will be animated over time. If the previous panning animation has not
         * finished by the time this method is called, it will be stopped.
         * @param deltaPos The relative coordinate shift.
         * @param durationSeconds The duration in which the panning operation will be completed in seconds.
         */
        void pan(const MapVec& deltaPos, float durationSeconds);
        /**
         * Sets the new absolute focus position. The new focus position is expected to be in
         * the coordinate system of the base projection. The new focus position will be clamped to
         * the world bounds and to the bounds set by Options::setPanBounds.
         *
         * If durationSeconds > 0 the panning operation will be animated over time. If the previous panning animation has not
         * finished by the time this method is called, it will be stopped.
         * @param pos The new absolute focus position.
         * @param durationSeconds The duration in which the panning operation will be completed in seconds.
         */
        void setFocusPos(const MapPos& pos, float durationSeconds);

        /**
         * Points the camera at a position and a zoom level IMMEDIATELY, with no animation.
         *
         * Prefer it over setFocusPos + setZoom: with restricted panning on, the focus is clamped so
         * the viewport stays inside the pan bounds, so the same target is clamped hard at a world
         * view and not at all up close. Setting the focus first therefore pins it to the middle of
         * the bounds - the equator, on an opening map - and the zoom that follows does not undo it.
         * This applies the two in whichever order avoids that.
         *
         * Unlike flyTo, it needs no frame, so it is also the call for pointing the camera before
         * the map has drawn.
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
         * Moves the camera to a position and a zoom level in ONE animation, pulling back over a
         * long move and coming down at the target (Van Wijk & Nuij's optimal path). Unlike
         * setFocusPos + setZoom, which run on their own clocks and cross the map at the final
         * zoom, this keeps the whole path in view.
         * @param pos The target position in base projection coordinate system.
         * @param zoom The target zoom level.
         * @param durationSeconds The duration in seconds, or 0 to derive it from the length of
         *                        the path - a move twice as far then does not take twice as long.
         *                        0 is NOT "immediate"; for that use moveTo.
         *
         * A flight asked for before the map has drawn its first frame RUNS FROM THAT FIRST FRAME:
         * the path is set up against the view it actually starts from, which is not known until
         * there is one. It is not dropped and it does not snap.
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
         * Moves the camera to a position, zoom, rotation and tilt in one animation, climbing over
         * the way there. The target position's Z is the height the viewpoint ends at, and the
         * climb is added to it as a parabola: highest halfway, back to nothing at both ends - a
         * plane's flight, which is also how you clear what stands between the two ends.
         * @param pos The target position in base projection coordinate system; its Z is the target height.
         * @param zoom The target zoom level.
         * @param rotation The target rotation in degrees.
         * @param tilt The target tilt in degrees.
         * @param climbHeight The extra height at the middle of the path, in the base projection's units.
         * @param durationSeconds The duration in seconds, or 0 to derive it from the path.
         */
        void flyTo(const MapPos& pos, float zoom, float rotation, float tilt, float climbHeight, float durationSeconds);
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
         * How far along a flyTo animation is, from 0 to 1, or -1 when none is running. It is the
         * value the camera is actually at, so an app animating its own state alongside the move
         * (a layer fading in, a mode switching over) reads it rather than running its own clock.
         * @return The flight progress, or -1.
         */
        float getFlightProgress() const;
        
        /**
         * Rotates the view relative to the current rotation value. Positive values rotate clockwise, negative values counterclockwise.
         * The new calculated rotation value will be wrapped to the range of (-180 .. 180]. Rotations are ignored if Options::setRotatable 
         * is set to false.
         *
         * If durationSeconds > 0 the rotating operation will be animated over time. If the previous rotating animation has not
         * finished by the time this method is called, it will be stopped.
         * @param deltaAngle The delta rotation value in degrees.
         * @param durationSeconds The duration in which the rotation operation will be completed in seconds.
         */
        void rotate(float deltaAngle, float durationSeconds);
        /**
         * Rotates the view relative to the current rotation value. Positive values rotate clockwise, negative values counterclockwise.
         * The new calculated rotation value will be wrapped to the range of (-180 .. 180]. Rotations are ignored if Options::setRotatable
         * is set to false.
         *
         * Rotating is done around the specified target position, keeping it at the same location on the screen.
         *
         * If durationSeconds > 0 the rotating operation will be animated over time. If the previous rotating animation has not
         * finished by the time this method is called, it will be stopped.
         * @param deltaAngle The delta angle value in degrees.
         * @param targetPos The zooming target position in the coordinate system of the base projection.
         * @param durationSeconds The duration in which the rotation operation will be completed in seconds.
         */
        void rotate(float deltaAngle, const MapPos& targetPos, float durationSeconds);
        /**
         * Sets the new absolute rotation value. 0 means look north, 90 means west, -90 means east and 180 means south.
         * The rotation value will be wrapped to the range of (-180 .. 180]. Rotations are ignored if Options::setRotatable
         * is set to false.
         *
         * If durationSeconds > 0 the rotating operation will be animated over time. If the previous rotating animation has not
         * finished by the time this method is called, it will be stopped.
         * @param angle The new absolute angle value in degrees.
         * @param durationSeconds The duration in which the rotation operation will be completed in seconds.
         */
        void setRotation(float angle, float durationSeconds);
        /**
         * Sets the new absolute rotation value. 0 means look north, 90 means west, -90 means east and 180 means south.
         * The rotation value will be wrapped to the range of (-180 .. 180]. Rotations are ignored if Options::setRotatable 
         * is set to false.
         *
         * Rotating is done around the specified target position, keeping it at the same location on the screen.
         *
         * If durationSeconds > 0 the rotating operation will be animated over time. If the previous rotating animation has not
         * finished by the time this method is called, it will be stopped.
         * @param angle The new absolute angle value in degrees.
         * @param targetPos The zooming target position in the coordinate system of the base projection.
         * @param durationSeconds The duration in which the rotation operation will be completed in seconds.
         */
        void setRotation(float angle, const MapPos& targetPos, float durationSeconds);
        
        /**
         * Tilts the view relative to the current tilt value. Positive values tilt the view down towards the map, 
         * negative values tilt the view up towards the horizon. The new calculated tilt value will be clamped to
         * the range of [30 .. 90] and to the range set by Options::setZoomRange.
         *
         * If durationSeconds > 0 the tilting operation will be animated over time. If the previous tilting animation has not
         * finished by the time this method is called, it will be stopped.
         * @param deltaTilt The number of degrees the camera should be tilted by.
         * @param durationSeconds The duration in which the tilting operation will be completed in seconds.
         */
        void tilt(float deltaTilt, float durationSeconds);
        /**
         * Sets the new absolute tilt value. 0 means look directly at the horizon, 90 means look directly down. The
         * minimum tilt angle is 30 degrees and the maximum is 90 degrees. The tilt value can be further constrained
         * by the Options::setTiltRange method. Values exceeding these ranges will be clamped.
         *
         * If durationSeconds > 0 the tilting operation will be animated over time. If the previous tilting animation has not
         * finished by the time this method is called, it will be stopped.
         * @param tilt The new absolute tilt value in degrees.
         * @param durationSeconds The duration in which the tilting operation will be completed in seconds.
         */
        void setTilt(float tilt, float durationSeconds);
        
        /**
         * Zooms the view relative to the current zoom value. Positive values zoom in, negative values zoom out.
         * The new calculated zoom value will be clamped to the range of [0 .. 24] and to the range set by Options::setZoomRange.
         *
         * If durationSeconds > 0 the zooming operation will be animated over time. If the previous zooming animation has not
         * finished by the time this method is called, it will be stopped.
         * @param deltaZoom The delta zoom value.
         * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
         */
        void zoom(float deltaZoom, float durationSeconds);
        /**
         * Zooms the view relative to the current zoom value. Positive values zoom in, negative values zoom out.
         * The new calculated zoom value will be clamped to the range of [0 .. 24] and to the range set by Options::setZoomRange.
         *
         * Zooming is done towards the specified target position, keeping it at the same location on the screen.
         *
         * If durationSeconds > 0 the zooming operation will be animated over time. If the previous zooming animation has not
         * finished by the time this method is called, it will be stopped.
         * @param deltaZoom The delta zoom value.
         * @param targetPos The zooming target position in the coordinate system of the base projection.
         * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
         */
        void zoom(float deltaZoom, const MapPos& targetPos, float durationSeconds);
        /**
         * Sets the new absolute zoom value. The minimum zoom value is 0, which means absolutely zoomed out and the maximum
         * zoom value is 24. The zoom value can be further constrained by the Options::setZoomRange method. Values
         * exceeding these ranges will be clamped. 
         *
         * If durationSeconds > 0 the zooming operation will be animated over time. If the previous zooming animation has not
         * finished by the time this method is called, it will be stopped.
         * @param zoom The new absolute zoom value.
         * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
         */
        void setZoom(float zoom, float durationSeconds);
        /**
         * Sets the new absolute zoom value. The minimum zoom value is 0, which means absolutely zoomed out and the maximum 
         * zoom value is 24. The zoom value can be further constrained by the Options::setZoomRange method. Values 
         * exceeding these ranges will be clamped.
         *
         * Zooming is done towards the specified target position, keeping it at the same location on the screen.
         *
         * If durationSeconds > 0, the zooming operation will be animated over time. If the previous zooming animation has not
         * finished by the time this method is called, it will be stopped.
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

    private:
        // Rotation and tilt optional, so both public moveTo overloads are the same code.
        void moveTo(const MapPos& pos, float zoom, const float* rotation, const float* tilt);

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
