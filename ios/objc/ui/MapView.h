/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifdef _MASSIF_USE_METALANGLE

#import "MGLKit.h"

#define MSFGLContext MGLContext
#define MSFGLKView MGLKView

#else

#import <GLKit/GLKit.h>

#define MSFGLContext EAGLContext
#define MSFGLKView GLKView

#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

@class MSFLayers;
@class MSFMapBounds;
@class MSFMapPos;
@class MSFMapVec;
@class MSFScreenPos;
@class MSFScreenBounds;
@class MSFMapEventListener;
@class MSFMapRenderer;
@class MSFOptions;

/**
 * MapView is a view class supporting map rendering and interaction.
 */
__attribute__ ((visibility("default"))) @interface MSFMapView : MSFGLKView

-(id)init;
-(id)initWithCoder:(NSCoder *)aDecoder;
-(id)initWithFrame:(CGRect)frame;
/**
 * Returns the Layers object, that can be used for adding and removing map layers.
 * @return The Layer object.
 */
-(MSFLayers*)getLayers;
/**
 * Makes the view translucent, so that whatever is behind it shows through wherever the map does
 * not paint. Combine it with a transparent clear color - [options setClearColor:
 * [[MSFColor alloc] initWithR:0 g:0 b:0 a:0]] - which is what leaves the frame empty; the SDK
 * renders with premultiplied alpha, so the result composites correctly.
 *
 * The view is an ordinary UIView, so it blends with the views behind it: a camera preview layer,
 * or any other content. The default is opaque.
 * @param translucent True to make the view translucent.
 */
-(void)setTranslucent:(BOOL)translucent;
/**
 * Returns the Options object, that can be used for modifying various map options.
 * @return the Option object.
 */
-(MSFOptions*)getOptions;
/**
 * Returns the MapRenderer object, that can be used to control rendering options.
 * @return the MapRenderer object.
 */
-(MSFMapRenderer*)getMapRenderer;
/**
 * Returns the position that the camera is currently looking at.
 * @return The current focus position in the coordinate system of the base projection.
 */
-(MSFMapPos*)getFocusPos;
/**
 * Returns the map rotation in degrees. 0 means looking north, 90 means west, -90 means east and 180 means south.
 * @return The map rotation in degrees in range of (-180 .. 180].
 */
-(float)getRotation;
/**
 * Returns the tilt angle in degrees. 0 means looking directly at the horizon, 90 means looking directly down.
 * @return The tilt angle in degrees.
 */
-(float)getTilt;
/**
 * Returns the zoom level. The value returned is never negative, 0 means absolutely zoomed out and all other
 * values describe some level of zoom.
 * @return The zoom level.
 */
-(float)getZoom;
/**
 * Pans the view relative to the current focus position. The deltaPos vector is expected to be in
 * the coordinate system of the base projection. The new calculated focus position will be clamped to
 * the world bounds and to the bounds set by Options::setPanBounds.
 *
 * If durationSeconds > 0 the panning operation will be animated over time. If the previous panning animation has not
 * finished by the time this method is called, it will be stopped.
 * @param deltaPos The coordinate difference the map should be moved by.
 * @param durationSeconds The duration in which the tilting operation will be completed in seconds.
 */
-(void)pan:(MSFMapVec*)deltaPos durationSeconds:(float)durationSeconds;
/**
 * Sets the new absolute focus position. The new focus position is expected to be in
 * the coordinate system of the base projection. The new focus position will be clamped to
 * the world bounds and to the bounds set by Options::setPanBounds.
 *
 * If durationSeconds > 0 the panning operation will be animated over time. If the previous panning animation has not
 * finished by the time this method is called, it will be stopped.
 * @param pos The new focus point position in base coordinate system.
 * @param durationSeconds The duration in which the tilting operation will be completed in seconds.
 */
-(void)setFocusPos:(MSFMapPos*)pos durationSeconds:(float)durationSeconds;
/**
 * Rotates the view relative to the current rotation value. Positive values rotate clockwise, negative values counterclockwise.
 * The new calculated rotation value will be wrapped to the range of (-180 .. 180]. Rotations are ignored if Options::setRotatable
 * is set to false.
 *
 * If durationSeconds > 0 the rotating operation will be animated over time. If the previous rotating animation has not
 * finished by the time this method is called, it will be stopped.
 * @param deltaAngle The delta angle value in degrees.
 * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
 */
-(void)rotate:(float)deltaAngle durationSeconds:(float)durationSeconds;
/**
 * Points the camera at a position and a zoom level IMMEDIATELY, with no animation.
 *
 * Prefer it over setFocusPos + setZoom, which are clamped differently depending on which is applied
 * first, and unlike flyTo it needs no frame - so it also works before the map has drawn.
 * @param pos The target position in base projection coordinate system.
 * @param zoom The target zoom level.
 */
-(void)moveTo:(MSFMapPos*)pos zoom:(float)zoom;
/**
 * The same, also setting rotation and tilt. See moveTo.
 * @param pos The target position in base projection coordinate system.
 * @param zoom The target zoom level.
 * @param rotation The rotation in degrees.
 * @param tilt The tilt in degrees.
 */
-(void)moveTo:(MSFMapPos*)pos zoom:(float)zoom rotation:(float)rotation tilt:(float)tilt;
/**
 * Moves the camera to a position and a zoom level in ONE animation, pulling back over a long move
 * and coming down at the target. Unlike setFocusPos + setZoom, which run on their own clocks and
 * cross the map at the final zoom, this keeps the whole path in view.
 *
 * A duration of 0 is NOT immediate - it derives the duration from the path. For an immediate move
 * use moveTo.
 * @param pos The target position in base projection coordinate system.
 * @param zoom The target zoom level.
 * @param durationSeconds The duration in seconds, or 0 to derive it from the length of the path.
 */
-(void)flyTo:(MSFMapPos*)pos zoom:(float)zoom durationSeconds:(float)durationSeconds;
/**
 * Moves the camera to a position, zoom, rotation and tilt in one animation. See flyTo.
 * @param pos The target position in base projection coordinate system.
 * @param zoom The target zoom level.
 * @param rotation The target rotation in degrees.
 * @param tilt The target tilt in degrees.
 * @param durationSeconds The duration in seconds, or 0 to derive it from the path.
 */
-(void)flyTo:(MSFMapPos*)pos zoom:(float)zoom rotation:(float)rotation tilt:(float)tilt durationSeconds:(float)durationSeconds;
/**
 * Moves the camera to a position, zoom, rotation and tilt in one animation, climbing over the way
 * there. The target position's Z is the height the viewpoint ends at, and the climb is added to it
 * as a parabola: highest halfway, back to nothing at both ends.
 * @param pos The target position in base projection coordinate system; its Z is the target height.
 * @param zoom The target zoom level.
 * @param rotation The target rotation in degrees.
 * @param tilt The target tilt in degrees.
 * @param climbHeight The extra height at the middle of the path, in the base projection's units.
 * @param durationSeconds The duration in seconds, or 0 to derive it from the path.
 */
-(void)flyTo:(MSFMapPos*)pos zoom:(float)zoom rotation:(float)rotation tilt:(float)tilt climbHeight:(float)climbHeight durationSeconds:(float)durationSeconds;
/**
 * Stops a flight started with flyTo, leaving the camera where it is.
 */
-(void)stopFlight;
/**
 * Returns true while a flyTo animation is running.
 * @return True if the camera is in flight.
 */
-(BOOL)isFlightActive;
/**
 * How far along a flyTo animation is, from 0 to 1, or -1 when none is running. It is the value the
 * camera is actually at, so an app animating its own state alongside the move reads it rather than
 * running a second clock beside it.
 * @return The flight progress, or -1.
 */
-(float)getFlightProgress;
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
 * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
 */
-(void)rotate:(float)deltaAngle targetPos:(MSFMapPos*)targetPos durationSeconds:(float)durationSeconds;
/**
 * Sets the new absolute rotation value. 0 means look north, 90 means west, -90 means east and 180 means south.
 * The rotation value will be wrapped to the range of (-180 .. 180]. Rotations are ignored if Options::setRotatable
 * is set to false.
 *
 * If durationSeconds > 0 the rotating operation will be animated over time. If the previous rotating animation has not
 * finished by the time this method is called, it will be stopped.
 * @param angle The new absolute rotation angle value in degrees.
 * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
 */
-(void)setRotation:(float)angle durationSeconds:(float)durationSeconds;
/**
 * Sets the new absolute rotation value. 0 means look north, 90 means west, -90 means east and 180 means south.
 * The rotation value will be wrapped to the range of (-180 .. 180]. Rotations are ignored if Options::setRotatable
 * is set to false.
 *
 * Rotating is done around the specified target position, keeping it at the same location on the screen.
 *
 * If durationSeconds > 0 the rotating operation will be animated over time. If the previous rotating animation has not
 * finished by the time this method is called, it will be stopped.
 * @param angle The new absolute rotation angle value in degrees.
 * @param targetPos The zooming target position in the coordinate system of the base projection.
 * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
 */
-(void)setRotation:(float)angle targetPos:(MSFMapPos*)targetPos durationSeconds:(float)durationSeconds;
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
-(void)tilt:(float)deltaTilt durationSeconds:(float)durationSeconds;
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
-(void)setTilt:(float)tilt durationSeconds:(float)durationSeconds;
/**
 * Zooms the view relative to the current zoom value. Positive values zoom in, negative values zoom out.
 * The new calculated zoom value will be clamped to the range of [0 .. 24] and to the range set by Options::setZoomRange.
 *
 * If durationSeconds > 0 the zooming operation will be animated over time. If the previous zooming animation has not
 * finished by the time this method is called, it will be stopped.
 * @param deltaZoom The delta zoom value.
 * @param durationSeconds The duration in which the zooming operation will be completed in seconds.
 */

-(void)zoom:(float)deltaZoom durationSeconds:(float)durationSeconds;
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
-(void)zoom:(float)deltaZoom targetPos:(MSFMapPos*)targetPos durationSeconds:(float)durationSeconds;
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
-(void)setZoom:(float)zoom durationSeconds:(float)durationSeconds;
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

-(void)setZoom:(float)zoom targetPos:(MSFMapPos*)targetPos durationSeconds:(float)durationSeconds;
/**
 * Animate the view parameters (focus position, tilt, rotation, zoom) so that the specified bounding box becomes fully visible.
 * This method does not work before the screen size is set.
 * @param mapBounds The bounding box on the map to be made visible in the base projection's coordinate system.
 * @param screenBounds The screen bounding box where to fit the map bounding box.
 * @param integerZoom If true, then closest integer zoom level will be used. If false, exact fractional zoom level will be used.
 * @param durationSeconds The duration in which the operation will be completed in seconds.
 */
-(void)moveToFitBounds:(MSFMapBounds*)mapBounds screenBounds:(MSFScreenBounds*)screenBounds integerZoom:(BOOL)integerZoom durationSeconds:(float)durationSeconds;
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
-(void)moveToFitBounds:(MSFMapBounds*)mapBounds screenBounds:(MSFScreenBounds*)screenBounds integerZoom:(BOOL)integerZoom resetRotation:(BOOL)resetRotation resetTilt:(BOOL)resetTilt durationSeconds:(float)durationSeconds;
/**
 * Returns the map event listener. May be null.
 * @return The map event listener.
 */
-(MSFMapEventListener*) getMapEventListener;

/**
 * Sets the map event listener. If a null pointer is passed no map events will be generated. The default is null.
 * @param mapEventListener The new map event listener.
 */
-(void)setMapEventListener:(MSFMapEventListener*)mapEventListener;

/**
 * Calculates the map position in base projection coordinate system corresponding to a screen position, using the current view parameters.
 * @param screenPos The screen position.
 * @return The calculated map position in base projection coordinate system. If the given screen position is not on the map, then NaNs are returned.
 */
-(MSFMapPos*)screenToMap:(MSFScreenPos*)screenPos;
/**
 * Calculates the screen position corresponding to a map position in base projection coordinate system, using the current view parameters.
 * @param mapPos The map position in base projection coordinate system.
 * @return The calculated screen position. Can be off-screen.
 */
-(MSFScreenPos*)mapToScreen:(MSFMapPos*)mapPos;

/**
 * Cancels all qued tasks such as tile and vector data fetches. Tasks that have already started
 * may continue until they finish. Tasks that are added after this method call are not affected.
 */
-(void)cancelAllTasks;

/**
 * Releases the memory occupied by the preloading area. Calling this method releases some
 * memory if preloading is enabled, but means that the area right outside the visible area has to be
 * fetched again.
 */
-(void)clearPreloadingCaches;

/**
 * Releases memory occupied by all caches. Calling this means that everything has to be fetched again,
 * including the visible area.
 */
-(void)clearAllCaches;

@end

#pragma clang diagnostic pop
