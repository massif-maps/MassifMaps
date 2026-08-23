/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_OPTIONS_H_
#define _MASSIF_OPTIONS_H_

#include "core/MapBounds.h"
#include "core/MapRange.h"
#include "core/ScreenPos.h"
#include "components/TerrainOptions.h"
#include "components/SkyOptions.h"
#include "components/FogOptions.h"
#include "components/LightOptions.h"
#include "graphics/Color.h"

#include <memory>
#include <mutex>
#include <vector>

namespace massif {
    class Bitmap;
    class CancelableThreadPool;
    class Projection;
    class ProjectionSurface;
    
    namespace RenderProjectionMode {
        /**
         *  Possible render projection modes.
         */
        enum RenderProjectionMode {
            /**
             * Planar projection.
             */
            RENDER_PROJECTION_MODE_PLANAR,
            /**
             * Spherical projection.
             */
            RENDER_PROJECTION_MODE_SPHERICAL
        };
    }
    
    namespace PanningMode {
        /**
         *  Possible panning modes for dual touch user input.
         */
        enum PanningMode {
            /**
             * Free panning means that the map panning is unrestricted, user is able to zoom, rotate and 
             * pan the map at the same time without any artificial limits.
             */
            PANNING_MODE_FREE,
            /**
             * Sticky panning means that the map panning is restricted, user is able to freely pan the map,
             * but zooming and rotating gestures can't be performed at the same time. User is still able to 
             * switch between zooming and rotating the map but it takes a bit more effort compared to FREE panning.
             * gesture is performed and
             */
            PANNING_MODE_STICKY,
            /**
             * Final sticky panning means that the map panning is restricted, user is able to freely pan the map,
             * but zooming and rotating gestures can't be performed at the same time. Once the gesture type is 
             * determined the user is stuck with either zooming or rotating. To switch the gesture the user has to lift
             * at least one the two fingers off the screen.
             */
            PANNING_MODE_STICKY_FINAL
        };
    }
    
    namespace FreeRoamMode {
        /**
         * Possible free roam modes: what a one-finger drag does, and which camera model the
         * tilt and the rotation follow.
         */
        enum FreeRoamMode {
            /**
             * Off: the standard map gestures. A one-finger drag pans the map.
             */
            FREE_ROAM_MODE_OFF,
            /**
             * Look: a one-finger drag looks around instead of panning - sideways turns the heading
             * about the camera, up and down tilts the map the way the two-finger tilt does, so the
             * camera still orbits its focus point. Panning moves to a two-finger drag; pinch and
             * two-finger rotation are unchanged.
             */
            FREE_ROAM_MODE_LOOK,
            /**
             * First person: the camera stops orbiting anything. A one-finger drag turns the view
             * about the CAMERA on both axes, like a mouse in a first person game - the position
             * never changes - and a two-finger drag moves, forward/back and strafing, the way the
             * keys would. Pinch and two-finger rotation are off, since neither belongs to that
             * control scheme.
             *
             * The camera model applies to every source, not just to touch: setTilt and
             * setMapRotation turn the view in place too, so a camera driven by the device's
             * orientation behaves exactly like the drag.
             */
            FREE_ROAM_MODE_FIRST_PERSON
        };
    }

    namespace PanningSpeedMode {
        /**
         * How fast a one-finger pan moves the map on a TILTED view, where a touch near the horizon
         * corresponds to a point far away and a touch at the bottom of the screen to a near one.
         */
        enum PanningSpeedMode {
            /**
             * The map point under the finger follows it exactly, which is what a flat map does.
             * On a tilted view the speed then changes DURING the gesture: a drag that starts near
             * the camera and travels up the screen accelerates as the finger reaches parts of the
             * screen that are further away.
             */
            PANNING_SPEED_MODE_MAP,
            /**
             * The scale is measured where the pan STARTS and stays fixed for the whole gesture:
             * starting far away still pans fast and starting close still pans slowly, but the
             * speed never changes while the finger is down. The default.
             */
            PANNING_SPEED_MODE_ANCHORED,
            /**
             * The scale is measured at the centre of the screen, so it depends neither on where
             * the finger started nor on where it goes - every pan moves the map at the same rate.
             */
            PANNING_SPEED_MODE_CONSTANT
        };
    }

    namespace PivotMode {
        /**
         *  Possible pivot modes.
         */
        enum PivotMode {
            /**
             * The touch point (or middle point between 2 finger touches) is used as the pivot point.
             */
            PIVOT_MODE_TOUCHPOINT,
            /**
             * Screen center is always used for pivot point.
             */
            PIVOT_MODE_CENTERPOINT
        };
    }
    
    /**
     * A class containing various options for rendering and map manipulation.
     */
    class Options {
    public:
        /**
         * Interface for monitoring options change events.
         */
        struct OnChangeListener {
            virtual ~OnChangeListener() { }
            
            /**
             * Listener method that gets called when an option has changed.
             * @param optionName The name of the option that has changed.
             */
            virtual void onOptionChanged(const std::string& optionName) = 0;
        };

        /**
         * Constructs an Options object with all parameters set to defaults.
         * @param envelopeThreadPool The thread pool used for envelope tasks.
         * @param tileThreadPool The thread pool used for tile tasks.
         */
        Options(const std::shared_ptr<CancelableThreadPool>& envelopeThreadPool, const std::shared_ptr<CancelableThreadPool>& tileThreadPool);
        virtual ~Options();
        
        /**
         * Returns the color of the ambient light.
         * @return The color of the ambient light.
         */
        Color getAmbientLightColor() const;
        /**
         * Sets the ambient light color.
         * Ambient light affects all lighting enabled models in the scene equally, it has no direction or location.
         * @param color The new color for the ambient light.
         */
        void setAmbientLightColor(const Color& color);
        
        /**
         * Returns the color of the main light.
         * @return The color of the main light.
         */
        Color getMainLightColor() const;
        /**
         * Sets the color of the main light. The main light affects all lighting enabled models
         * in the scene equally from a certain direction. This light can be used to simulate sun or moon light.
         * @param color The new color for the main light.
         */
        void setMainLightColor(const Color& color);

        /**
         * Returns the direction of the main light.
         * @return The direction of the main light.
         */
        MapVec getMainLightDirection() const;
        /**
         * Sets the direction of the main light. The main light affects all lighting enabled models
         * in the scene equally from a certain direction. This light can be used to simulate sun or moon light.
         * The direction is always measured based on the local tangent frame of the focus point. 
         * @param direction The new direction vector for the main light. (0,0,-1) means straight down, (-0.707,0,-0.707) means
         *        from east with a 45 degree angle. The direction vector will be normalized.
         */
        void setMainLightDirection(const MapVec& direction);
    
        /**
         * Returns the render projection mode.
         * @return The render projection mode.
         */
        RenderProjectionMode::RenderProjectionMode getRenderProjectionMode() const;
        /**
         * Sets the render projection mode. The default is RenderProjectionMode::PLANAR.
         * @param renderProjectionMode The new render projection mode.
         */
        void setRenderProjectionMode(RenderProjectionMode::RenderProjectionMode renderProjectionMode);
    
        /**
         * Returns the state of the tile border debug overlay.
         * @return True if every tile layer outlines the tiles it draws.
         */
        bool isDebugTileBorders() const;
        /**
         * Sets the state of the tile border debug overlay. Every tile layer then outlines the
         * tiles it actually draws, in 3D following the terrain, with a colour per zoom level and
         * alternating brightness between neighbours - so a layer using a coarser tile set than
         * the one under it, an overzoomed stand-in, or a tile that owns pixels it should not, is
         * visible on screen. The default is false.
         * @param enabled The new state of the tile border debug overlay.
         */
        void setDebugTileBorders(bool enabled);

        /**
         * Returns the click type detection state.
         * @return True if click type detection is enabled.
         */
        bool isClickTypeDetection() const;
        /**
         * Sets the state of the click type detection flag. If set to true clicks are categorized as normal clicks, double clicks,
         * long clicks and dual clicks. The default is true.
         * @param enabled The new state of the click type detection flag.
         */
        void setClickTypeDetection(bool enabled);
    
        /**
         * Returns the double click detection state.
         * @return True if double click detection is enabled.
         */
        bool isDoubleClickDetection() const;
        /**
         * Sets the state of the double click detection flag. If set to true clicks, then double clicks are detected separately from normal clicks. are categorized as normal clicks, double clicks,
         * The click type resolving takes about 400ms (configurable via setDoubleClickMaxDuration), so for applications that do not 
         * require this functionality, it can be turned off. The default is true.
         * @param enabled The new state of the double click detection flag.
         */
        void setDoubleClickDetection(bool enabled);

        /**
         * Returns the long click duration in seconds.
         * @return The long click duration in seconds.
         */
        float getLongClickDuration() const;
        /**
         * Sets the long click duration in seconds. The default is value is 0.4 (400ms).
         * @param duration The new duration for the long click in seconds.
         */
        void setLongClickDuration(float duration);

        /**
         * Returns the double click max duration in seconds.
         * @return The double click max duration in seconds.
         */
        float getDoubleClickMaxDuration() const;
        /**
         * Sets the double click max in seconds. The default is value is 0.4 (400ms).
         * @param duration The new value for the double click max duration detection in seconds.
         */
        void setDoubleClickMaxDuration(float duration);
    
        /**
         * Returns the tile size used for drawing map tiles.
         * @return The tile size in density-independent pixels (dp).
         */
        int getTileDrawSize() const;
        /**
         * Sets the tile size for drawing map tiles. Different datasources may have different tile sizes, this option
         * can be used to compensate for bigger or smaller tiles than default. The default is 256.
         * @param tileDrawSize The new tile size in density-independent pixels (dp).
         */
        void setTileDrawSize(int tileDrawSize);

        /**
         * Returns the factor on the screen size a tile may cover before it is refined.
         * @return The tile LOD factor. The default is 0.5; 1 is exactly tangram's rule.
         */
        float getTileLODFactor() const;
        /**
         * Sets how big a tile may get on screen before the next zoom level is used, as a factor on
         * tangram's rule (core/src/tile/tileManager.cpp): refine while the tile's projected screen
         * area is at least that of a 2x2 block of nominal tiles. A factor of 1 is that rule
         * verbatim, larger keeps tiles coarser (fewer tiles, fewer labels, less detail), smaller
         * refines further.
         * Where it matters is a tilted view: a tile near the horizon collapses to a few pixels of
         * screen while its distance barely grows, so this - not the view distance - is what decides
         * how much of the horizon band is drawn at full detail.
         * @param factor The new tile LOD factor. The default is 0.5.
         */
        void setTileLODFactor(float factor);

        /**
         * Returns how many zoom levels a tile may lose to foreshortening.
         * @return The limit in zoom levels, or 0 when there is no limit. The default is 0.
         */
        float getTileLODForeshorteningLimit() const;
        /**
         * Sets how many zoom levels a tile may be coarsened by the grazing angle alone.
         * The LOD rule (see setTileLODFactor) compares a tile's projected screen AREA, which falls
         * both with the distance and with the cosine of the angle the view ray makes with the
         * ground. The second term is what keeps a mountain 10 km out coarse while ground at the same
         * distance under a steeper angle is refined: at a low tilt every tile in the frame sits at
         * 79-89 degrees of incidence and loses 1.2 to 3 levels to it.
         * This bounds that second term only, so the distance term is untouched and genuinely far
         * ground stays coarse. Lower values refine more of the tilted view and cost tiles roughly
         * 2x per level; 0 leaves the area rule as tangram wrote it.
         * @param levels The limit in zoom levels, or 0 for no limit. The default is 0.
         */
        void setTileLODForeshorteningLimit(float levels);
    
        /**
         * Returns the dots per inch value.
         * @return The dots per inch value.
         */
        float getDPI() const;
        /**
         * Sets the dots per inch value. This is calculated automatically by the SDK when the MapView is created using 
         * the device screen parameters. The purpose of this value is to compensate for very high or low resolution devices,
         * so that the map remains readable.
         * @param dpi The new dots per inch value.
         */
        void setDPI(float dpi);
    
        /**
         * Returns the draw distance value.
         * @return The draw distance value.
         */
        float getDrawDistance() const;
        /**
         * Sets a new draw distance value. The higher the draw distance the more tiles can be seen, if the map is tilted.
         * Changing the draw distance will cause the horizon to move, which means that the if the sky bitmap is used,
         * the horizon may not match up anymore. Increasing this value will decrease performance and increase network traffic, 
         * if online map is used. The default is 16.
         * @param drawDistance The new draw distance value.
         */
        void setDrawDistance(float drawDistance);
    
        /**
         * Returns the vertial field of view angle.
         * @return The vertical field of view angle in degrees.
         */
        int getFieldOfViewY() const;
        /**
         * Sets the vertial field of view angle. Larger values increase the viewable area, at the cost of performance and
         * additional perspective distortion. The default is 70.
         * @param fovY The new vertical field of view angle in degrees.
         */
        void setFieldOfViewY(int fovY);
    
        /**
         * Returns the panning mode.
         * @return The panning mode.
         */
        PanningMode::PanningMode getPanningMode() const;
        /**
         * Sets the panning mode. The default is PanningMode::FREE.
         * @param panningMode The new panning mode.
         */
        void setPanningMode(PanningMode::PanningMode panningMode);
        
        /**
         * Returns the pivot mode.
         * @return The pivot mode.
         */
        PivotMode::PivotMode getPivotMode() const;
        /**
         * Sets the pivot mode. The default is PIVOT_MODE_TOUCHPOINT
         * @param pivotMode The new pivot mode.
         */
        void setPivotMode(PivotMode::PivotMode pivotMode);
    
        /**
         * Returns the state of seamless horizontal panning flag.
         * @return True if seamless horizontal panning is enabled.
         */
        bool isSeamlessPanning() const;
        /**
         * Sets the state of seamless horizontal panning flag. If set to true, the user can scroll seamlessly from
         * the left side of the map to the right, and the other way around. The default is true.
         * @param enabled The new state of seamless horizontal panning flag.
         */
        void setSeamlessPanning(bool enabled);

        /**
         * Returns the state of the restricted panning flag.
         * @return True if restricted panning is enabled.
         */
        bool isRestrictedPanning() const;
        /**
         * Sets the restricted panning flag. If set to true, then focus point coordinates and zoom level of the map view
         * will be adjusted to display as little empty background as possible. The default is false.
         */
        void setRestrictedPanning(bool enabled);

        /**
         * Returns true if tilting gesture direction is reversed (and same as with Google Maps).
         * @return True if tilting gesture direction is reversed (and same as with Google Maps). Otherwise returns false.
         */
        bool isTiltGestureReversed() const;
        /**
         * Sets the tilting gesture direction. By default, the gesture is not reversed.
         * @param reversed True if Google Maps compatible mode should be used. False otherwise (default).
         */
        void setTiltGestureReversed(bool reversed);

        /**
         * Returns the state of zoom gestures. 
         * @return True if zoom gestures are enabled. False otherwise.
         */
        bool isZoomGestures() const;
        /**
         * Sets the zoom gestures flag. Zoom gestures allow to use double click and dual click to zoom in/out of the map.
         * Enabled by default. Note that zoom gestures require that click detection mode is enabled and also that double click detection is enabled.
         * @param enabled True if zoom gestured should be enabled, false otherwise.
         */
        void setZoomGestures(bool enabled);

        /**
         * Returns the state of rotation gestures. 
         * @return True if rotation gestures are enabled. False otherwise.
         */
        bool isRotationGestures() const;
        /**
         * Sets the rotation gestures flag. Rotation gestures allow to use pinch to rotate the map.
         * By default, rotation gestures are not enabled.
         * @param enabled True if rotation gestured should be enabled, false otherwise.
         */
        void setRotationGestures(bool enabled);
    
        /**
         * Returns the number of threads used by the envelope task pool.
         * @return The envelope task thread pool size.
         */
        int getEnvelopeThreadPoolSize() const;
        /**
         * Sets the number of threads used by the envelope task pool. More threads means more envelope tasks 
         * are executed in parallel. This might speed up the data query, but may cause performance drops. Default is 1.
         * @param poolSize The new envelope task thread pool size.
         */
        void setEnvelopeThreadPoolSize(int poolSize);
    
        /**
         * Returns the number of threads used by the tile task pool.
         * @return The tile task thread pool size.
         */
        int getTileThreadPoolSize() const;
        /**
         * Sets the number of threads used by the tile task pool. More threads means more tile tasks
         * are executed in parallel. This might speed up the data query, but may cause performance drops. Default is 1.
         * @param poolSize The new tile task thread pool size.
         */
        void setTileThreadPoolSize(int poolSize);
    
        /**
         * Returns the clear color used by the renderer before drawing anything else.
         * By default, this is white. It should be set to (0, 0, 0, 0) if transparent MapView is needed.
         * @return The clear color.
         */
        Color getClearColor() const;
        /**
         * Sets the clear color of the renderer.
         * @param color The new clear color.
         */
        void setClearColor(const Color& color);
        
        /**
         * Returns the sky color.
         * @return The sky color.
         */
        Color getSkyColor() const;
        /**
         * Sets the sky color. The purpose of the sky bitmap is to fill out the empty space visible at low tilt angles.
         * @param color The new sky color. If the color is transparent, sky is not rendered.
         */
        void setSkyColor(const Color& color);
        /**
         * Returns the sky bitmap. May be null.
         * @return The sky bitmap.
         */
        std::shared_ptr<Bitmap> getSkyBitmap() const;

        /**
         * Returns the background bitmap. May be null.
         * @return The background bitmap.
         */
        std::shared_ptr<Bitmap> getBackgroundBitmap() const;
        /**
         * Sets the background bitmap. The purpose of the background bitmap is to fill out the empty space when there's
         * no map data visible. The bitmap will get scaled and repeated to cover the hole visible area. If a null pointer is passed, 
         * the background won't be drawn.
         * The width and height of the bitmap must be power of two (for example: 256 * 256 or 128 * 512). 
         * It's also preferred if the bitmap was square (width == height), but this is not a requirement. 
         * The default is "default_background.png".
         * @param backgroundBitmap The new background bitmap.
         */
        void setBackgroundBitmap(const std::shared_ptr<Bitmap>& backgroundBitmap);

        /**
         * Returns the state of the user input flag.
         * @return True if user input is allowed.
         */
        bool isUserInput() const;
        /**
         * Sets the state of the user input flag. If set to false the user won't be able to pan the map using touch controls,
         * programmatic map panning using MapView methods is still possible. The default is false.
         * @param enabled The new state of the user input flag.
         */
        void setUserInput(bool enabled);
    
        /**
         * Returns the panning speed mode.
         * @return The panning speed mode.
         */
        PanningSpeedMode::PanningSpeedMode getPanningSpeedMode() const;
        /**
         * Sets how fast a one-finger pan moves the map on a tilted view. The default is
         * PANNING_SPEED_MODE_ANCHORED, which keeps the speed a gesture starts with for as long as
         * it lasts; PANNING_SPEED_MODE_MAP is the exact grab-the-world pan, which changes speed as
         * the finger moves between near and far parts of the screen.
         * @param mode The new panning speed mode.
         */
        void setPanningSpeedMode(PanningSpeedMode::PanningSpeedMode mode);

        /**
         * Returns the free roam mode.
         * @return The free roam mode.
         */
        FreeRoamMode::FreeRoamMode getFreeRoamMode() const;
        /**
         * Sets the free roam mode: what a one-finger drag does, and which camera model the tilt
         * and the rotation follow. Free roam is what makes content placed in the sky
         * (CelestialLayer) reachable, since it is normally off the top of the screen. To look
         * ABOVE the horizon the tilt range has to allow a negative tilt - e.g.
         * setTiltRange(MapRange(-90, 90)) - as it stops at the horizon by default.
         * The default is FREE_ROAM_MODE_OFF.
         * @param mode The new free roam mode.
         */
        void setFreeRoamMode(FreeRoamMode::FreeRoamMode mode);

        /**
         * Returns how fast a free roam drag turns the view.
         * @return The turn in degrees per inch of drag.
         */
        float getFreeRoamLookSensitivity() const;
        /**
         * Sets how fast a free roam drag turns the view, in degrees per inch of drag. The default
         * is 90, i.e. an inch of drag turns a quarter turn.
         * @param degreesPerInch The turn in degrees per inch of drag.
         */
        void setFreeRoamLookSensitivity(float degreesPerInch);

        /**
         * Returns how far a first person move drag travels.
         * @return The distance per inch of drag, as a fraction of the camera to focus distance.
         */
        float getFreeRoamMoveSpeed() const;
        /**
         * Sets how far a two-finger move travels in FREE_ROAM_MODE_FIRST_PERSON, per inch of drag,
         * as a fraction of the distance from the camera to its focus point - so a move covers the
         * same part of the view at any zoom. The default is 0.5.
         * @param distancePerInch The distance per inch of drag.
         */
        void setFreeRoamMoveSpeed(float distancePerInch);

        /**
         * Returns the state of the kinetic panning flag.
         * @return True if kinetic panning is enabled.
         */
        bool isKineticPan();
        /**
         * Sets the state of the kinetic panning flag. Kinetic panning allows the map to pan automatically using
         * the inertia of the last swipe, after the user has finished interacting with the touch screen.
         * Default is true.
         * @param enabled The new state of the kinetic panning flag.
         */
        void setKineticPan(bool enabled);
    
        /**
         * Returns the state of the kinetic rotation flag.
         * @return True if kinetic rotation is enabled.
         */
        bool isKineticRotation();
        /**
         * Sets the state of the kinetic rotation flag. Kinetic rotation allows the map to rotate automatically using
         * the inertia of the last swipe, after the user has finished interacting with the touch screen.
         * Default is true.
         * @param enabled The new state of the kinetic rotation flag.
         */
        void setKineticRotation(bool enabled);
        
        /**
         * Returns the state of kinetic zoom flag.
         * @return True if kinetic zooming is enabled.
         */
        bool isKineticZoom();
        /**
         * Sets the state of the kinetic zooming flag. Kinetic zooming allows the map to zoom automatically using
         * the inertia of the last swipe, after the user has finished interacting with the touch screen.
         * Default is true.
         * @param enabled The new state of the kinetic zooming flag.
         */
        void setKineticZoom(bool enabled);
    
        /**
         * Returns the state of the map rotatability flag.
         * @return True if map rotating is enabled.
         */
        bool isRotatable() const;
        /**
         * Sets the state of the map rotatability flag. If set to false the map can't be rotated by any means. The default is true.
         * @param enabled The new state of the map rotatability flag.
         */
        void setRotatable(bool enabled);
    
        /**
         * Returns the tilt range constraint.
         * @return The tilt range constraint in degrees.
         */
        MapRange getTiltRange() const;
        /**
         * Sets the tilt range constraint. This will limit the tilt angle of the camera to the specified range.
         * The current tilt angle will remain unaffected, until the next time the tilt angle changes.
         * The minimum tilt angle is -90 degrees and the maximum is 90 degrees. Values that are out of range will be clamped.
         * The default value is MapRange(0, 90).
         * A NEGATIVE tilt looks above the horizon: the camera stays where it is and the view pitches
         * up, which is what an application showing the sky needs. It is opt-in, as the default range
         * stops at the horizon.
         * @param tiltRange The new tilt range constraint in degrees.
         */
        void setTiltRange(const MapRange& tiltRange);
    
        /**
         * Returns the zoom range constraint.
         * @return The zoom range constraint.
         */
        MapRange getZoomRange() const;
        /**
         * Sets the zoom range constraint. This will limit the zoom level of the camera to the specified range.
         * The current zoom level will remain unaffected, until the next time the zoom level changes.
         * The minimum zoom value is 0 and the maximum is 24. Values that are out of range will be clamped.
         * The default value is MapRange(0, 24).
         * @param zoomRange The new zoom range constraint.
         */
        void setZoomRange(const MapRange& zoomRange);
        
        /**
         * Returns the map panning bounds constraints. Map bounds minimum and maximum points are in the base
         * projection's coordinate system.
         * @return The map bounds constraints.
         */
        MapBounds getPanBounds() const;
        /**
         * Sets the map panning bounds constraints. This will limit camera movement to the specified bounds.
         * The current camera position will remain unaffected, until the next time camera position changes.
         * Map bounds minimum and maximum points are expected to be in the base projection's coordinate system.
         * If the bounds are larger than the world size, they will be clamped to world bounds. The default value covers the 
         * whole world.
         * @param panBounds The new map bounds constraints.
         */
        void setPanBounds(const MapBounds& panBounds);
        /**
         * Returns the adjusted internal pan bounds. This takes also account of render projection mode.
         * @param clamp True if the coordinates should be clamped.
         * @return The adjusted internal pan bounds.
         */
        MapBounds getAdjustedInternalPanBounds(bool clamp) const;
    
        /**
         * Returns the focus point offset (from screen center) in pixels.
         * @return The focus point offset in pixels.
         */
        ScreenPos getFocusPointOffset() const;
        /**
         * Sets the focus point offset (from screen center) in pixels.
         * @param offset The new focus point offset in pixels.
         */
        void setFocusPointOffset(const ScreenPos& offset);
        
        /**
         * Returns the base projection.
         * @return The base projection.
         */
        std::shared_ptr<Projection> getBaseProjection() const;
        /**
         * Sets the base projection. All MapView, MapEventListener and Options methods use the coordinate system of this projection.
         * For example, if base projection is set to EPSG3857 then MapView::getFocusPos returns the coordinates in the EPSG3857 coordinate system,
         * The same applies to setter methods like MapView::setFocusPos which expect the input coordinates to be in the base projection's coordinate system.
         * The default is EPSG3857.
         * @param baseProjection The new base projection.
         */
        void setBaseProjection(const std::shared_ptr<Projection>& baseProjection);
        
        /**
         * Returns the projection surface.
         * @return The projection surface.
         */
        std::shared_ptr<ProjectionSurface> getProjectionSurface() const;

        /**
         * Returns the terrain options. May be null if no terrain is configured.
         * @return The terrain options.
         */
        std::shared_ptr<TerrainOptions> getTerrainOptions() const;
        /**
         * Sets the terrain options. When set, 3D terrain is rendered using the elevation
         * data source of the terrain options. Note that terrain is currently only supported
         * with the PLANAR render projection mode. Setting null disables terrain.
         * Note: this feature is experimental and may change in future SDK versions.
         * @param terrainOptions The new terrain options. Can be null.
         */
        void setTerrainOptions(const std::shared_ptr<TerrainOptions>& terrainOptions);

        /**
         * Returns the sky options. May be null.
         * @return The sky options.
         */
        std::shared_ptr<SkyOptions> getSkyOptions() const;
        /**
         * Sets the sky options. Attaching a SkyOptions object replaces the legacy sky bitmap
         * band with a full-screen shader sky. Pass a null pointer to go back to the legacy sky.
         * @param skyOptions The new sky options.
         */
        void setSkyOptions(const std::shared_ptr<SkyOptions>& skyOptions);

        /**
         * Returns the fog (atmosphere) options. May be null.
         * @return The fog options.
         */
        std::shared_ptr<FogOptions> getFogOptions() const;
        /**
         * Sets the fog options - the haze distant ground fades into and the colours it carries
         * into the sky. Setting null, or leaving the fog color transparent, means no fog.
         * @param fogOptions The new fog options. Can be null.
         */
        void setFogOptions(const std::shared_ptr<FogOptions>& fogOptions);

        /**
         * Returns the light (sun) options. May be null.
         * @return The light options.
         */
        std::shared_ptr<LightOptions> getLightOptions() const;
        /**
         * Sets the light (sun) options. The sun direction drives the shader sky, terrain
         * surface lighting and shadows.
         * @param lightOptions The new light options.
         */
        void setLightOptions(const std::shared_ptr<LightOptions>& lightOptions);

        /**
         * Sets wether layers are processed in reversed order to process labels.
         * The default is true.
         * @param enabled wether to process layers in reversed order.
         */
        void setLayersLabelsProcessedInReverseOrder(bool enabled);
    
        /**
         * Returns wether layers are processed in reversed order to process labels.
         * @return True if layers are processed in reversed order.
         */
        bool isLayersLabelsProcessedInReverseOrder() const;

        /**
         * Registers listener for options change events.
         * @param listener The listener for change events.
         */
        void registerOnChangeListener(const std::shared_ptr<OnChangeListener>& listener);
        
        /**
         * Unregisters listener from options change events.
         * @param listener The previously added listener.
         */
        void unregisterOnChangeListener(const std::shared_ptr<OnChangeListener>& listener);

        static std::shared_ptr<Bitmap> GetDefaultBackgroundBitmap();

    private:
        static const float DEFAULT_LONG_CLICK_DURATION;
        static const float DEFAULT_DOUBLE_CLICK_MAX_DURATION;
        static const Color DEFAULT_CLEAR_COLOR;
        static const Color DEFAULT_SKY_COLOR;
        static const Color DEFAULT_BACKGROUND_COLOR;
        static const Color DEFAULT_AMBIENT_LIGHT_COLOR;
        static const Color DEFAULT_MAIN_LIGHT_COLOR;
        static const MapVec DEFAULT_MAIN_LIGHT_DIR;
        
        void notifyOptionChanged(const std::string& optionName);
        
        Color _ambientLightColor;
        Color _mainLightColor;
        MapVec _mainLightDir;
    
        RenderProjectionMode::RenderProjectionMode _renderProjectionMode;
    
        bool _debugTileBorders;
        bool _clickTypeDetection;
        bool _doubleClickDetection;
        float _longClickDuration;
        float _doubleClickMaxDuration;
    
        int _tileDrawSize;
        float _tileLODFactor;
        float _tileLODForeshorteningLimit;
    
        float _dpi;
    
        float _drawDistance;
    
        int _fovY;
    
        PanningMode::PanningMode _panningMode;
        
        PivotMode::PivotMode _pivotMode;
    
        bool _seamlessPanning;
        bool _restrictedPanning;

        bool _tiltGestureReversed;

        bool _zoomGestures;
        bool _rotationGestures;

        Color _clearColor;
        Color _skyColor;
        
        mutable Color _skyBitmapColor;
        mutable std::shared_ptr<Bitmap> _skyBitmap;

        std::shared_ptr<Bitmap> _backgroundBitmap;
        
        bool _userInput;
        PanningSpeedMode::PanningSpeedMode _panningSpeedMode;
        FreeRoamMode::FreeRoamMode _freeRoamMode;
        float _freeRoamLookSensitivity;
        float _freeRoamMoveSpeed;
    
        bool _kineticPan;
        bool _kineticRotation;
        bool _kineticZoom;
    
        bool _rotatable;
        MapRange _tiltRange;
        MapRange _zoomRange;
        MapBounds _panBounds;
        ScreenPos _focusPointOffset;

        bool _layersLabelsProcessedInReverseOrder;

    
        std::shared_ptr<Projection> _baseProjection;

        std::shared_ptr<Projection> _renderProjection;

        std::shared_ptr<ProjectionSurface> _projectionSurface;

        std::shared_ptr<TerrainOptions> _terrainOptions;
        std::shared_ptr<TerrainOptions::OnChangeListener> _terrainOptionsListener;

        std::shared_ptr<SkyOptions> _skyOptions;
        std::shared_ptr<SkyOptions::OnChangeListener> _skyOptionsListener;

        std::shared_ptr<FogOptions> _fogOptions;
        std::shared_ptr<FogOptions::OnChangeListener> _fogOptionsListener;

        std::shared_ptr<LightOptions> _lightOptions;
        std::shared_ptr<LightOptions::OnChangeListener> _lightOptionsListener;
    
        std::shared_ptr<CancelableThreadPool> _envelopeThreadPool;
        std::shared_ptr<CancelableThreadPool> _tileThreadPool;
    
        mutable std::mutex _mutex;

        std::vector<std::shared_ptr<OnChangeListener> > _onChangeListeners;
        mutable std::mutex _onChangeListenersMutex;

        static std::shared_ptr<Bitmap> _DefaultBackgroundBitmap;
        
        static std::mutex _Mutex;
    };
    
}

#endif
