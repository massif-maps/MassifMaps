/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MSFMASSIFOBJECT_H_
#define _MSFMASSIFOBJECT_H_

#import <Foundation/Foundation.h>

#import "MSFValueTypes.h"
#import "MSFMapEvents.h"

@class MSFPropertyGroup;
@class MSFSpec;
@class MSFSubscription;
@class MSFMassifMap;
@class MSFLayer;

NS_ASSUME_NONNULL_BEGIN

/**
 * A registered object, addressed by handle.
 *
 * Properties, methods and events on one class, so an app never sees a handle or a result code.
 */
__attribute__ ((visibility("default")))
NS_SWIFT_NAME(MassifObject)
@interface MSFMassifObject : NSObject

/** The raw handle, for the flat API underneath. Rarely needed. */
@property (nonatomic, readonly) int handle;

/** The id it is registered under, or nil for a call result, which has none an app chose. */
@property (nonatomic, readonly, nullable) NSString *objectId;

/** Whether the object is still registered. NO after destroy. */
@property (nonatomic, readonly, getter=isValid) BOOL valid;

// --- properties -------------------------------------------------------------------------------

/**
 * Sets a property. The path may walk object properties - "fogOptions.rangeStart".
 * @param value An NSNumber, NSString, MSFPosition, MSFBounds, another MSFMassifObject to point
 *        an object property at, or nil/NSNull to clear one.
 * @return NO when the path does not resolve or the property is read-only.
 */
- (BOOL)set:(NSString *)path value:(id)value;

/**
 * Writes several properties in ONE crossing, from a spec of path to value.
 *
 * `set:value:` per key is one bridge call each, and configuring a layer writes a dozen. An
 * object-valued property is not carried here - that needs a handle, so write it with set:value:.
 * @return NO when a key did not resolve. The others still applied, and the log names them.
 */
- (BOOL)apply:(MSFSpec *)values;

/**
 * Writes a position in a named projection - the write counterpart of getPos:projection:.
 *
 * set:value: takes one in WGS84, which is what a read returns; this is for an app that already
 * holds map coordinates.
 */
- (BOOL)set:(NSString *)path position:(MSFPosition *)value projection:(NSString *)projection;

- (double)getDouble:(NSString *)path defaultValue:(double)defaultValue;
- (long long)getLong:(NSString *)path defaultValue:(long long)defaultValue;
- (BOOL)getBool:(NSString *)path defaultValue:(BOOL)defaultValue;
- (nullable NSString *)getString:(NSString *)path defaultValue:(nullable NSString *)defaultValue;

/** A position, in the object's own projection. */
- (nullable MSFPosition *)getPos:(NSString *)path;

/** A position, converted - "EPSG:4326" for lon/lat. */
- (nullable MSFPosition *)getPos:(NSString *)path projection:(NSString *)projection;

- (nullable MSFBounds *)getBounds:(NSString *)path;

/** A view scoped to a path prefix, so a group of options reads without repeating it. */
- (MSFPropertyGroup *)group:(NSString *)prefix;

// --- methods ----------------------------------------------------------------------------------

/**
 * Runs a method. Each element of `args` is one positional parameter; an NSArray of numbers
 * becomes a JSON array, which is how a tile or a list of positions is passed.
 * @return The result, WHICH THE CALLER OWNS - send it `destroy`. nil on failure, with `error` set.
 */
- (nullable MSFMassifObject *)call:(NSString *)method
                              args:(nullable NSArray *)args
                             error:(NSError **)error;

/**
 * The same on a worker thread, with the result handed to the block on the main queue.
 * The result is destroyed once the block returns, so copy out what you need.
 * @return A call id for `cancelCall:`, or 0.
 */
- (int)callAsync:(NSString *)method
            args:(nullable NSArray *)args
      completion:(void (^)(MSFMassifObject * _Nullable result))completion;

/**
 * Cancels a call. Cancelling stops it being STARTED and stops its result being DELIVERED; it
 * cannot abort one already running.
 */
- (BOOL)cancelCall:(int)call;

/** Cancels every pending call on this object. */
- (int)cancelAllCalls;

/** A binary property as bytes - "data" on a tile, or "" when this object IS the blob. */
- (nullable NSData *)data:(NSString *)path;

/** A bulk numeric result, flat, as raw doubles. Read with `bytes` cast to `const double *`. */
@property (nonatomic, readonly, nullable) NSData *doubles;

/** This object's value as JSON, for a result that is a document rather than an object. */
@property (nonatomic, readonly) NSString *json;

// --- events -----------------------------------------------------------------------------------

/** Subscribes to an event, delivered on the main queue. */
- (nullable MSFSubscription *)on:(NSString *)event handler:(MSFMapEventHandler)handler NS_WARN_UNUSED_RESULT;

/** Removes every handler of one event on this object. */
- (int)off:(NSString *)event;

/** Removes every handler on this object. */
- (int)offAll;

// --- lifetime ---------------------------------------------------------------------------------

/** Drops the id and the context's reference. Handles held elsewhere go stale, not dangling. */
- (BOOL)destroy;

@end

/**
 * A view of one object scoped to a path prefix.
 *
 * Deliberately not a method per option: there are over seven hundred and they change with the SDK.
 * The prefix is what makes the string short enough to live with.
 */
__attribute__ ((visibility("default")))
NS_SWIFT_NAME(PropertyGroup)
@interface MSFPropertyGroup : NSObject

- (BOOL)set:(NSString *)name value:(id)value;
/** Several at once, prefixed - so the object writes them in one crossing. */
- (BOOL)apply:(MSFSpec *)values;
- (double)getDouble:(NSString *)name defaultValue:(double)defaultValue;
- (long long)getLong:(NSString *)name defaultValue:(long long)defaultValue;
- (BOOL)getBool:(NSString *)name defaultValue:(BOOL)defaultValue;
- (nullable NSString *)getString:(NSString *)name defaultValue:(nullable NSString *)defaultValue;
- (nullable MSFPosition *)getPos:(NSString *)name;

/** The object this group belongs to, for anything outside the prefix. */
@property (nonatomic, readonly) MSFMassifObject *object;

@end

/** A tile data source, usable with or without a map. */
__attribute__ ((visibility("default")))
NS_SWIFT_NAME(MassifSource)
@interface MSFMassifSource : MSFMassifObject

/**
 * Fetches one tile, on the calling thread. BLOCKS - an HTTP source does network I/O here.
 * @return The tile's bytes, or nil.
 */
- (nullable NSData *)loadTileX:(int)x y:(int)y zoom:(int)zoom;

/**
 * The same on a worker thread. Returns void so Swift imports it as `async` - use the generic
 * `callAsync:args:completion:` when the call id is needed for cancellation.
 */
- (void)loadTileX:(int)x
                y:(int)y
             zoom:(int)zoom
       completion:(void (^)(NSData * _Nullable data))completion;

/**
 * Adds a named layer to a "geojson" source and returns its index, or -1.
 *
 * The index, not the name, is what the other calls take - it is what the SDK's own API uses, and
 * a lookup per feature update would be the wrong trade for a source that exists to be updated.
 */
- (int)createLayer:(NSString *)name;

/**
 * Replaces a layer's whole content with a GeoJSON document. The source re-tiles it, so the
 * features go through the same style and the same renderer as a tile server's would.
 */
- (BOOL)setLayerGeoJSON:(int)layer geoJson:(NSString *)geoJson;

@end

/** A layer, with the things an app does to one. */
__attribute__ ((visibility("default")))
NS_SWIFT_NAME(MassifLayer)
@interface MSFMassifLayer : MSFMassifObject

- (instancetype)opacity:(float)opacity;
- (instancetype)visible:(BOOL)visible;

/** Moves the layer within the map's stack. 0 is the bottom. */
- (instancetype)moveTo:(int)index;

/** Removes it from the map. The object stays registered until it is destroyed. */
- (instancetype)detach;

/** The object-API layer, for anything the facade does not reach yet. */
@property (nonatomic, readonly, nullable) MSFLayer *layer;

/**
 * Subscribes to feature clicks. Installs the bridge on first use and chains to whatever listener
 * was already there, so an app that also uses the object API keeps working.
 */
- (nullable MSFSubscription *)onFeatureClick:(MSFVectorTileClickHandler)handler NS_WARN_UNUSED_RESULT;

/** Subscribes to clicks on the ELEMENTS of a vector layer - a marker, a popup an app added. */
- (nullable MSFSubscription *)onElementClick:(MSFVectorElementClickHandler)handler NS_WARN_UNUSED_RESULT;

/** The same, claiming the tap, so the map's own onClick does not also fire. */
- (nullable MSFSubscription *)consumeElementClick:(MSFVectorElementClickFilter)handler NS_WARN_UNUSED_RESULT;

/**
 * The same, for a handler that can claim the click. It runs on the thread the click came from,
 * because the SDK asks whether the event was consumed NOW.
 */
- (nullable MSFSubscription *)consumeFeatureClick:(MSFVectorTileClickFilter)handler NS_WARN_UNUSED_RESULT;

/**
 * Elevations under a set of positions, as one flat array of doubles - a profile over a track is
 * thousands of numbers. Only a hillshade layer answers; anything else gives nil.
 */
- (nullable NSData *)elevations:(NSArray<MSFPosition *> *)positions;

@end

NS_ASSUME_NONNULL_END

#endif
