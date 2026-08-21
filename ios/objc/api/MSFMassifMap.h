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
@class MSFLayer;
@class MSFMassifObject;
@class MSFMassifLayer;
@class MSFPropertyGroup;
@class MSFSpec;
@class MSFSubscription;

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

// --- layers -----------------------------------------------------------------------------------

/** Adds a layer built with MSFMassif to the top of the stack. */
- (MSFMassifLayer *)add:(MSFMassifLayer *)layer;

/** Adds it at a given position. 0 is the bottom. */
- (MSFMassifLayer *)add:(MSFMassifLayer *)layer atIndex:(int)index;

/** Builds and adds in one step, which is what an app writes most of the time. */
- (nullable MSFMassifLayer *)addLayer:(NSString *)objectId
                                 spec:(MSFSpec *)spec
                                error:(NSError **)error;

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

- (nullable MSFSubscription *)onClick:(MSFMapClickHandler)handler;
- (nullable MSFSubscription *)onMove:(MSFMapEventHandler)handler;

/** Fires once the map has stopped moving and every visible tile has settled. */
- (nullable MSFSubscription *)onIdle:(MSFMapEventHandler)handler;
- (nullable MSFSubscription *)onStable:(MSFMapEventHandler)handler;

/**
 * Pans, zooms, rotations and tilts the user drove.
 *
 * Coalesced: this fires per frame while a finger is down, so a pending one is replaced rather
 * than queued, and a slow handler cannot flood the loop.
 */
- (nullable MSFSubscription *)onInteraction:(MSFMapInteractionHandler)handler;

/** Every handler on this map, gone. Layer subscriptions are the layer's own. */
- (instancetype)offAll;

/** Detaches: removes every handler and drops the map's id. The MSFMapView is untouched. */
- (void)detach;

@end

NS_ASSUME_NONNULL_END

#endif
