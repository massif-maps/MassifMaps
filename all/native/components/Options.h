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
    namespace vt { class TileTransformer; }
    
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
             */
            PANNING_MODE_STICKY,
            /**
             * Final sticky panning: like sticky panning, but once the gesture type is determined the user is
             * stuck with either zooming or rotating until at least one of the two fingers is lifted.
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
             * Look: a one-finger drag turns the heading (sideways) and tilts (up/down) instead of panning;
             * the camera still orbits its focus point. Panning moves to a two-finger drag; pinch and
             * two-finger rotation are unchanged.
             */
            FREE_ROAM_MODE_LOOK,
            /**
             * First person: a one-finger drag turns the view about the camera, whose position never changes;
             * a two-finger drag moves forward/back and strafes. Pinch and two-finger rotation are off.
             * setTilt and setMapRotation also turn the view in place, so an orientation-driven camera matches the drag.
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
             * The map point under the finger follows it exactly. On a tilted view the speed then changes
             * during the gesture, accelerating as the finger moves toward the far part of the screen.
             */
            PANNING_SPEED_MODE_MAP,
            /**
             * The scale is measured where the pan starts and stays fixed for the whole gesture,
             * so the speed never changes while the finger is down. The default.
             */
            PANNING_SPEED_MODE_ANCHORED,
            /**
             * The scale is measured at the centre of the screen, so it depends neither on where
             * the finger started nor on where it goes - every pan moves the map at the same rate.
             */
            PANNING_SPEED_MODE_CONSTANT
        };
    }

    namespace TileLODProfile {
        /**
         * A named set of the tile LOD numbers, so a platform picks a density with one call rather
         * than by tuning four knobs that multiply.
         */
        enum TileLODProfile {
            /**
             * The reference density: TileLODFactor 1, the tangram/mapbox/maplibre rule - a tile is refined
             * while it covers more than a 2x2 block of nominal tiles. Fewest tiles.
             */
            TILE_LOD_PROFILE_REFERENCE,
            /**
             * Half a level finer than the reference, with a shorter style zoom lift. Meant for a
             * phone: visibly sharper than the reference at roughly twice its tile count.
             */
            TILE_LOD_PROFILE_MOBILE,
            /**
             * A full level finer than the reference (TileLODFactor 0.5, the historical default),
             * about 4x its tile count. Meant for a desktop or a web page on a real GPU.
             */
            TILE_LOD_PROFILE_DESKTOP
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
         * Sets the state of the tile border debug overlay: every tile layer outlines the tiles it
         * draws, following the terrain in 3D, with a colour per zoom level. The default is false.
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
         * Returns how far a pointer may travel before a press stops counting as a click.
         * @return The tolerance in density-independent pixels (dp). The default is 32.
         */
        float getClickMovingTolerance() const;
        /**
         * Sets how far a pointer may travel before a press stops counting as a click and the map
         * starts panning. The default, 32 dp, is a finger-sized threshold; a mouse wants far less
         * (maplibre uses 3 px), which is why a desktop or web host lowers it.
         * @param tolerance The new tolerance in density-independent pixels (dp).
         */
        void setClickMovingTolerance(float tolerance);
    
        /**
         * Returns the double click detection state.
         * @return True if double click detection is enabled.
         */
        bool isDoubleClickDetection() const;
        /**
         * Sets the state of the double click detection flag. If set to true, double clicks are detected separately from normal clicks.
         * Resolving the click type takes about 400ms (see setDoubleClickMaxDuration), so apps that don't need it can turn it off.
         * The default is true.
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
         * Sets the tile size for drawing map tiles, to compensate for datasources with bigger or smaller tiles. The default is 256.
         * Style sizes do not follow it: labels stay the same dp whatever tile the layer picks.
         * Set it before adding a layer; a layer reads it when it joins the map.
         * @param tileDrawSize The new tile size in density-independent pixels (dp).
         */
        void setTileDrawSize(int tileDrawSize);

        /**
         * Returns how many zoom levels the camera is offset from the tile-size convention.
         * @return The zoom offset in levels. The default is 0.
         */
        float getZoomOffset() const;
        /**
         * Sets how many zoom levels the camera is offset from the SDK's 256-pixel tile convention; 1 adopts
         * the 512-pixel one of maplibre and mapbox-gl. It only renumbers (label and line sizes do not move),
         * so getZoom(), a stored camera and a visibleZoomRange shift with it.
         * @param offset The new zoom offset in levels. The default is 0.
         */
        void setZoomOffset(float offset);

        /**
         * Returns the factor on the screen size a tile may cover before it is refined.
         * @return The tile LOD factor. The default is 0.5; 1 is exactly tangram's rule.
         */
        float getTileLODFactor() const;
        /**
         * Sets how big a tile may get on screen before the next zoom level is used, as a factor on tangram's
         * rule (refine while the tile covers at least a 2x2 block of nominal tiles): 1 is that rule, larger is
         * coarser, smaller finer. On a tilted view this, not the draw distance, decides the horizon's detail.
         * @param factor The new tile LOD factor. The default is 0.5.
         */
        void setTileLODFactor(float factor);

        /**
         * Returns how many distinct zoom levels a tilted view may spread over.
         * @return The zoom levels on screen. The default is 9.314, maplibre's.
         */
        float getTileLODMaxZoomLevelsOnScreen() const;
        /**
         * Sets how many distinct zoom levels the frame may spread over with the horizon at the top of the
         * screen (maplibre's maxZoomLevelsOnScreen). Higher coarsens the far field faster and costs fewer
         * tiles, lower keeps it finer. The default matches the screen-area rule. Replaces TileLODForeshorteningLimit.
         * @param levels The zoom levels on screen. The default is 9.314.
         */
        void setTileLODMaxZoomLevelsOnScreen(float levels);

        /**
         * Returns how many times more tiles a tilted view may load than a top-down one.
         * @return The ratio. The default is 3, maplibre's.
         */
        float getTileLODTileCountRatio() const;
        /**
         * Sets the cap on how many more tiles a tilted view may load than a top-down one (maplibre's
         * tileCountMaxMinRatio); past it the level is lowered uniformly. Inert at the default
         * TileLODMaxZoomLevelsOnScreen: it only binds when that asks for a gentler far field.
         * @param ratio The ratio. The default is 3.
         */
        void setTileLODTileCountRatio(float ratio);

        /**
         * Applies a named set of TileLODFactor, TileLODMaxZoomLevelsOnScreen, TileLODTileCountRatio
         * and TileStyleZoomLift at once. They multiply into the tile count, so set a profile for the
         * platform and override one number only if a specific map needs it.
         * @param profile The profile to apply.
         */
        void setTileLODProfile(TileLODProfile::TileLODProfile profile);

        /**
         * Returns how many zoom levels above its own a coarsened tile may be styled at.
         * @return The lift in zoom levels. The default is 2.
         */
        int getTileStyleZoomLift() const;
        /**
         * Sets how many zoom levels above its own zoom a coarsened tile matches its style rules at, so a
         * converted style's view-zoom `minzoom` survives LOD coarsening; which tiles are drawn is unchanged.
         * Bounded, as a far tile styled at the camera zoom emits all near-field labels. 0 uses the tile's own zoom.
         * @param levels The lift in zoom levels. The default is 2.
         */
        void setTileStyleZoomLift(int levels);

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
         * Sets a new draw distance value: higher shows more tiles on a tilted map, at a performance and network cost.
         * It moves the horizon, so a sky bitmap may no longer match up. The default is 16.
         * @param drawDistance The new draw distance value.
         */
        void setDrawDistance(float drawDistance);

        /**
         * Returns how far labels are placed, in multiples of the camera-to-focus distance.
         * @return The label view distance. The default is 5, and 0 means no limit.
         */
        float getLabelViewDistance() const;
        /**
         * Sets how far from the camera a label may be placed, in multiples of the camera-to-focus distance.
         * The default 5 is maplibre's cut. Raise it, or set 0, for a view along the ground (panorama,
         * first person), where the far features are the subject. Placement cost grows with it.
         * @param viewDistance The new label view distance, or 0 for no limit.
         */
        void setLabelViewDistance(float viewDistance);

        /**
         * Returns how far outside the viewport labels are placed, in screen pixels.
         * @return The label padding, or a negative value while it follows the tilt.
         */
        float getLabelPadding() const;
        /**
         * Sets how far outside the viewport a label may be placed, in screen pixels; more padding, fewer
         * labels blinking as the view turns, at a placement and tile-loading cost. The default is negative:
         * 100 pixels scaled by sin(tilt), floor 20. A panorama wants about half a screen width.
         * @param padding The new label padding in screen pixels, or a negative value to follow the tilt.
         */
        void setLabelPadding(float padding);

        /**
         * Returns the vertical field of view angle.
         * @return The vertical field of view angle in degrees.
         */
        float getFieldOfViewY() const;
        /**
         * Sets the vertical field of view angle. Larger values increase the viewable area, at the cost of performance and
         * additional perspective distortion. The default is 70.
         * Fractional, so an AR overlay can match the field of view of the camera behind it exactly.
         * @param fovY The new vertical field of view angle in degrees.
         */
        void setFieldOfViewY(float fovY);
    
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
         * Enabled by default.
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
         * Sets the background bitmap, scaled and repeated to fill the space with no map data; null disables it.
         * Width and height must be powers of two (e.g. 256 * 256 or 128 * 512), square preferred.
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
         * programmatic map panning using MapView methods is still possible. The default is true.
         * @param enabled The new state of the user input flag.
         */
        void setUserInput(bool enabled);
    
        /**
         * Returns the panning speed mode.
         * @return The panning speed mode.
         */
        PanningSpeedMode::PanningSpeedMode getPanningSpeedMode() const;
        /**
         * Sets how fast a one-finger pan moves the map on a tilted view.
         * The default is PANNING_SPEED_MODE_ANCHORED.
         * @param mode The new panning speed mode.
         */
        void setPanningSpeedMode(PanningSpeedMode::PanningSpeedMode mode);

        /**
         * Returns the free roam mode.
         * @return The free roam mode.
         */
        FreeRoamMode::FreeRoamMode getFreeRoamMode() const;
        /**
         * Sets the free roam mode: what a one-finger drag does, and which camera model tilt and rotation follow.
         * It makes sky content (CelestialLayer) reachable; looking above the horizon also needs a negative
         * tilt range, e.g. setTiltRange(MapRange(-90, 90)). The default is FREE_ROAM_MODE_OFF.
         * @param mode The new free roam mode.
         */
        void setFreeRoamMode(FreeRoamMode::FreeRoamMode mode);

        /**
         * Returns how fast a FREE_ROAM_MODE_LOOK drag turns the view.
         * @return The turn in degrees per inch of drag.
         */
        float getFreeRoamLookSensitivity() const;
        /**
         * Sets how fast a FREE_ROAM_MODE_LOOK drag turns the view, in degrees per inch. The default is 360.
         * FREE_ROAM_MODE_FIRST_PERSON ignores it: its look keeps the ground under the finger.
         * @param degrees The turn in degrees per inch.
         */
        void setFreeRoamLookSensitivity(float degrees);

        /**
         * Returns the first person move multiplier.
         * @return A multiplier on the ground the drag actually covers. 1 tracks the cursor.
         */
        float getFreeRoamMoveSpeed() const;
        /**
         * Scales the two-finger move in FREE_ROAM_MODE_FIRST_PERSON over the ground the drag covers.
         * The default 1 keeps the ground under the cursor; below 1 it lags, above 1 it runs ahead.
         * @param multiplier The multiplier on the tracked distance.
         */
        void setFreeRoamMoveSpeed(float multiplier);

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
         * Sets the tilt range constraint; the current tilt is unaffected until it next changes. Values are clamped to [-90, 90].
         * A negative tilt pitches the view above the horizon, which is opt-in.
         * The default value is MapRange(3, 90).
         * @param tiltRange The new tilt range constraint in degrees.
         */
        void setTiltRange(const MapRange& tiltRange);
    
        /**
         * Returns the zoom range constraint.
         * @return The zoom range constraint.
         */
        MapRange getZoomRange() const;
        /**
         * Sets the zoom range constraint; the current zoom is unaffected until it next changes.
         * Values are clamped to [0, 24]. The default value is MapRange(0, 24).
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
         * Sets the map panning bounds constraints, in the base projection's coordinate system; the current position
         * is unaffected until the camera next moves. Bounds larger than the world are clamped to world bounds.
         * The default value covers the whole world.
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
         * Sets the base projection. All MapView, MapEventListener and Options methods (getters and setters)
         * use the coordinate system of this projection. The default is EPSG3857.
         * @param baseProjection The new base projection.
         */
        void setBaseProjection(const std::shared_ptr<Projection>& baseProjection);
        
        /**
         * Returns the projection surface.
         * @return The projection surface.
         */
        std::shared_ptr<ProjectionSurface> getProjectionSurface() const;
        /**
         * Returns the base tile transformer for the current render projection (plane or globe, never
         * terrain); a layer that displaces by elevation decorates it. Internal, not in the public API.
         * @return The base tile transformer.
         */
        std::shared_ptr<vt::TileTransformer> getTileTransformer() const;

        /**
         * Returns the terrain options. May be null if no terrain is configured.
         * @return The terrain options.
         */
        std::shared_ptr<TerrainOptions> getTerrainOptions() const;
        /**
         * Sets the terrain options: 3D terrain is rendered from their elevation data source; null disables it.
         * Terrain is currently only supported with the PLANAR render projection mode.
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
        // 0.2 inch at 160 dpi: a finger-sized threshold.
        static const float DEFAULT_CLICK_MOVING_TOLERANCE;
        
        void notifyOptionChanged(const std::string& optionName);
        
        Color _ambientLightColor;
        Color _mainLightColor;
        MapVec _mainLightDir;
    
        RenderProjectionMode::RenderProjectionMode _renderProjectionMode;
    
        bool _debugTileBorders;
        bool _clickTypeDetection;
        float _clickMovingTolerance;
        bool _doubleClickDetection;
        float _longClickDuration;
        float _doubleClickMaxDuration;
    
        int _tileDrawSize;
        float _zoomOffset;
        float _tileLODFactor;
        float _tileLODMaxZoomLevelsOnScreen;
        float _tileLODTileCountRatio;
        int _tileStyleZoomLift;

        static const int MAX_TILE_STYLE_ZOOM_LIFT;
    
        float _dpi;
    
        float _drawDistance;
        float _labelViewDistance;
        float _labelPadding;

        float _fovY;
    
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
        std::shared_ptr<vt::TileTransformer> _tileTransformer;

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
