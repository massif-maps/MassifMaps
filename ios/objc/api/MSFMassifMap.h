/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MSFMASSIFMAP_H_
#define _MSFMASSIFMAP_H_

#import <Foundation/Foundation.h>
#import "MSFMapEvents.h"

@class MSFMapView;
@class MSFMapPos;
@class MSFMapBounds;
@class MSFScreenPos;
@class MSFScreenBounds;
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
 * A thin pass-through to MSFMapView rather than a reimplementation - the object API already has
 * the flight code. What this adds is one call that moves everything at once, which is otherwise
 * four calls with four animations racing each other.
 */
__attribute__ ((visibility("default")))
NS_SWIFT_NAME(MapCamera)
@interface MSFMapCamera : NSObject

/** Seconds for the moves that follow. 0 is immediate. Resets to 0 after each move. */
- (instancetype)animate:(float)seconds;

- (instancetype)position:(MSFMapPos *)pos;
- (instancetype)zoom:(float)zoom;
- (instancetype)rotation:(float)degrees;
- (instancetype)tilt:(float)degrees;

/**
 * Moves everything in ONE flight. Four separate setters animate independently and visibly fight
 * each other; this is the call an app actually wants.
 */
- (instancetype)moveTo:(MSFMapPos *)pos zoom:(float)zoom rotation:(float)rotation tilt:(float)tilt;
- (instancetype)moveTo:(MSFMapPos *)pos zoom:(float)zoom;

/**
 * Frames a bounding box, which is what "zoom to this route" or "fit these markers" means.
 * @param integerZoom Snap to a whole zoom level, which keeps raster tiles crisp.
 */
- (instancetype)fitBounds:(MSFMapBounds *)bounds
             screenBounds:(MSFScreenBounds *)screenBounds
              integerZoom:(BOOL)integerZoom;

/** The same, over the whole view. */
- (instancetype)fitBounds:(MSFMapBounds *)bounds;

@property (nonatomic, readonly) MSFMapPos *currentPosition;
@property (nonatomic, readonly) float currentZoom;
@property (nonatomic, readonly) float currentRotation;
@property (nonatomic, readonly) float currentTilt;

/** Whether a flight is still running, and how to stop it. */
@property (nonatomic, readonly, getter=isMoving) BOOL moving;
- (instancetype)stop;

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
- (nullable MSFMapPos *)screenToMapX:(float)x y:(float)y;

/** And the other way, for placing a native view over a coordinate. */
- (nullable MSFScreenPos *)mapToScreen:(MSFMapPos *)pos;

- (nullable MSFSubscription *)onClick:(MSFMapClickHandler)handler NS_WARN_UNUSED_RESULT;
- (nullable MSFSubscription *)onMove:(MSFMapEventHandler)handler NS_WARN_UNUSED_RESULT;

/** Fires once the map has stopped moving and every visible tile has settled. */
- (nullable MSFSubscription *)onIdle:(MSFMapEventHandler)handler NS_WARN_UNUSED_RESULT;
- (nullable MSFSubscription *)onStable:(MSFMapEventHandler)handler NS_WARN_UNUSED_RESULT;

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
