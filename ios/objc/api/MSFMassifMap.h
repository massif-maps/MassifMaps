/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MSFMASSIFMAP_H_
#define _MSFMASSIFMAP_H_

#import <Foundation/Foundation.h>

#import "MSFValueTypes.h"
#import "MSFMapEvents.h"

@class MSFMapView;
@class MSFLayer;
@class MSFMassifObject;
@class MSFMassifLayer;
@class MSFPropertyGroup;
@class MSFSpec;
@class MSFSubscription;
@class MSFMassifSource;
@class MSFMassifElements;

NS_ASSUME_NONNULL_BEGIN

/**
 * The camera, fluent, and animated by default.
 *
 * Every call goes through the FACADE - the map view is adopted, and this is get/call on its
 * handle. Not a pass-through to MSFMapView any more: that was the one part of the sugar a binding
 * could not reproduce from the C ABI, and the facade is also what applies the projection, so a
 * position from a click event can be handed straight back here (#159).
 *
 * Positions are WGS84 - longitude, latitude - unless the map was told otherwise.
 */
__attribute__ ((visibility("default")))
NS_SWIFT_NAME(MapCamera)
@interface MSFMapCamera : NSObject

/** Seconds for the moves that follow. 0 is immediate. Resets to 0 after each move. */
- (instancetype)animate:(float)seconds;

/**
 * Arches the next flight - highest halfway, nothing at either end, which is what clears the ridge
 * between two valleys. In the base projection's units. Resets after the move.
 */
- (instancetype)climb:(float)height;

- (instancetype)position:(MSFPosition *)pos;
- (instancetype)zoom:(float)zoom;
- (instancetype)rotation:(float)degrees;
- (instancetype)tilt:(float)degrees;

/**
 * Moves everything in ONE flight. Four separate setters animate independently and visibly fight
 * each other; this is the call an app actually wants.
 */
- (instancetype)moveTo:(MSFPosition *)pos zoom:(float)zoom rotation:(float)rotation tilt:(float)tilt;
- (instancetype)moveTo:(MSFPosition *)pos zoom:(float)zoom;

/**
 * Frames a bounding box, which is what "zoom to this route" or "fit these markers" means.
 * @param integerZoom Snap to a whole zoom level, which keeps raster tiles crisp.
 */
- (instancetype)fitBounds:(MSFBounds *)bounds
               screenRect:(MSFScreenRect *)screenRect
              integerZoom:(BOOL)integerZoom;

/** The same, also straightening the camera on the way there. */
- (instancetype)fitBounds:(MSFBounds *)bounds
               screenRect:(MSFScreenRect *)screenRect
              integerZoom:(BOOL)integerZoom
            resetRotation:(BOOL)resetRotation
                resetTilt:(BOOL)resetTilt;

/** The same, over the whole view. */
- (instancetype)fitBounds:(MSFBounds *)bounds width:(float)width height:(float)height;

@property (nonatomic, readonly, nullable) MSFPosition *currentPosition;
@property (nonatomic, readonly) float currentZoom;
@property (nonatomic, readonly) float currentRotation;
@property (nonatomic, readonly) float currentTilt;

/** Whether a flight is still running, and how to stop it. */
@property (nonatomic, readonly, getter=isMoving) BOOL moving;
/** 0 to 1 through the current flight, so a UI can follow it. */
@property (nonatomic, readonly) float progress;
- (instancetype)stop;

/** Where a touch point is on the map, and the other way for placing a view over a coordinate. */
- (nullable MSFPosition *)screenToMapX:(float)x y:(float)y;
- (nullable MSFScreenPoint *)mapToScreen:(MSFPosition *)pos;

@end

/**
 * A map, as an app talks to it.
 *
 * The MSFMapView stays the app's: this attaches to one rather than replacing it, and the object
 * API keeps working alongside. Attaching twice with the same id gives the same map back.
 */
__attribute__ ((visibility("default")))
NS_SWIFT_NAME(MassifMap)
@interface MSFMassifMap : NSObject

/** Attaches to a map view under the id "main". */
+ (nullable instancetype)attach:(MSFMapView *)view;

/**
 * Attaches under a given id, which is what makes more than one map work: every handle, event and
 * layer belongs to the map it was registered under.
 */
+ (nullable instancetype)attach:(MSFMapView *)view objectId:(NSString *)objectId;

/** The view this is attached to. Nothing here replaces it. */
@property (nonatomic, readonly) MSFMapView *view;

/** The map's Options, as a facade object - every option the SDK has, by path. */
@property (nonatomic, readonly) MSFMassifObject *options;

@property (nonatomic, readonly) MSFMapCamera *camera;

// --- option groups ----------------------------------------------------------------------------

/*
 * One accessor per options class, not per option. The names below are the whole list and they do
 * not grow with the SDK; the properties inside them are data.
 */
@property (nonatomic, readonly) MSFPropertyGroup *fog;
@property (nonatomic, readonly) MSFPropertyGroup *sky;
@property (nonatomic, readonly) MSFPropertyGroup *terrain;
@property (nonatomic, readonly) MSFPropertyGroup *light;

/**
 * Turns 3D terrain on from an elevation source, and returns its options for tuning.
 *
 * The elevation decoder comes from the source's own `encoding`, so nothing here names one.
 * Options starts with these EMPTY, which is why they are BUILT rather than written through: the
 * no-argument accessors above only work once something is there.
 */
- (nullable MSFPropertyGroup *)terrainWithSpec:(MSFSpec *)spec error:(NSError **)error;

/**
 * Fog, on the mapbox model. Independent of the terrain - it fogs a plain 2D map too. The range is
 * in MULTIPLES of the camera-to-focus distance, so one pair of values holds at every zoom.
 */
- (nullable MSFPropertyGroup *)fogWithSpec:(MSFSpec *)spec error:(NSError **)error;

/** The sky dome behind the map. */
- (nullable MSFPropertyGroup *)skyWithSpec:(MSFSpec *)spec error:(NSError **)error;

/** Sun direction and colour, which the terrain and 3D buildings shade from. */
- (nullable MSFPropertyGroup *)lightWithSpec:(MSFSpec *)spec error:(NSError **)error;

// --- layers -----------------------------------------------------------------------------------

/** Adds a layer built with MSFMassif to the top of the stack. */
- (MSFMassifLayer *)add:(MSFMassifLayer *)layer;

/** Adds it at a given position. 0 is the bottom. */
- (MSFMassifLayer *)add:(MSFMassifLayer *)layer atIndex:(int)index;

/** Builds and adds in one step, which is what an app writes most of the time. */
- (nullable MSFMassifLayer *)addLayer:(NSString *)objectId
                                 spec:(MSFSpec *)spec
                                error:(NSError **)error;

/**
 * Adopts the layer at a stack position, so a map built with the object API can be driven through
 * the facade without rebuilding it.
 */
- (nullable MSFMassifLayer *)adoptLayer:(NSString *)objectId atIndex:(int)index;

/**
 * Builds a source this map owns, so it is released with the map rather than living on under its
 * id. MSFMassif's own +source: is the one for a source shared between maps.
 */
- (nullable MSFMassifSource *)source:(NSString *)objectId spec:(MSFSpec *)spec error:(NSError **)error;

/**
 * The same for a style. Worth an id whenever the app talks to it later - a style parameter, a
 * theme switch - because an object property cannot yet be read back as a handle.
 */
- (nullable MSFMassifObject *)style:(NSString *)objectId spec:(MSFSpec *)spec error:(NSError **)error;

/** Builds an object of any kind, owned by this map. */
- (nullable MSFMassifObject *)object:(NSString *)kind
                            objectId:(NSString *)objectId
                                spec:(MSFSpec *)spec
                               error:(NSError **)error;

// --- markers and popups -------------------------------------------------------------------------

/** The map's own markers and popups, on a layer created the first time this is called. */
@property (nonatomic, readonly, nullable) MSFMassifElements *elements;

/**
 * Adds a marker. The spec carries its position AND its style, inline or by id:
 *
 *     [map addMarker:[[MSFSpec of:@"marker"]
 *         set:@"position" value:@[ @6.865, @45.832 ]]
 *         set:@"style" value:[[MSFSpec of:@"marker"] set:@"size" value:@30]] error:nil];
 */
- (nullable MSFMassifObject *)addMarker:(MSFSpec *)spec error:(NSError **)error;

/** The same for a balloon popup - a label anchored to a position, with a title and a body. */
- (nullable MSFMassifObject *)addPopup:(MSFSpec *)spec error:(NSError **)error;

/** A layer already on this map, by id, or nil. */
- (nullable MSFMassifLayer *)layer:(NSString *)objectId;

- (instancetype)remove:(MSFMassifLayer *)layer;

@property (nonatomic, readonly) int layerCount;

/** A layer by stack position, for the ones the app did not build through the registry. */
- (nullable MSFLayer *)rawLayerAt:(int)index;

// --- events -----------------------------------------------------------------------------------

/**
 * The projection this map's event positions are read in - "EPSG:4326" for lon/lat.
 *
 * Set once, and every handler subscribed afterwards gets positions already converted, instead of
 * each one repeating the same toWgs84 chain. Empty leaves them in the map's own projection.
 */
@property (nonatomic, copy) NSString *eventProjection;

/** Where a touch point is on the map. Straight through to the view; here so one class has it. */
- (nullable MSFPosition *)screenToMapX:(float)x y:(float)y;

/** And the other way, for placing a native view over a coordinate. */
- (nullable MSFScreenPoint *)mapToScreen:(MSFPosition *)pos;

- (nullable MSFSubscription *)onClick:(MSFMapClickHandler)handler NS_WARN_UNUSED_RESULT;
/** Every camera change, whatever caused it. Fires well above frame rate during a drag. */
- (nullable MSFSubscription *)onMove:(MSFMapMoveHandler)handler NS_WARN_UNUSED_RESULT;

/**
 * Fires when the renderer has nothing left to draw. Tiles may still be loading - this is the end
 * of the frame queue, not of the data.
 */
- (nullable MSFSubscription *)onIdle:(MSFMapEventHandler)handler NS_WARN_UNUSED_RESULT;

/**
 * Fires once when a movement ENDS - animations finished, fingers lifted, inertia died out.
 *
 * Once per movement, with the cause: a tap that did not move the camera does not fire it at all.
 * This is the one to hang "the map settled, refresh my data" on.
 */
- (nullable MSFSubscription *)onStable:(MSFMapMoveHandler)handler NS_WARN_UNUSED_RESULT;

/**
 * Pans, zooms, rotations and tilts the user drove.
 *
 * Coalesced: this fires per frame while a finger is down, so a pending one is replaced rather
 * than queued, and a slow handler cannot flood the loop.
 */
- (nullable MSFSubscription *)onInteraction:(MSFMapInteractionHandler)handler NS_WARN_UNUSED_RESULT;

/** Every handler on this map, gone. Layer subscriptions are the layer's own. */
- (instancetype)offAll;

/** Detaches: removes every handler and drops the map's id. The MSFMapView is untouched. */
- (void)detach;

@end

NS_ASSUME_NONNULL_END

#endif
