/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MSFMASSIF_H_
#define _MSFMASSIF_H_

#import <Foundation/Foundation.h>

@class MSFMassifObject;
@class MSFMassifSource;
@class MSFMassifLayer;
@class MSFTileDataSource;

NS_ASSUME_NONNULL_BEGIN

/**
 * The JSON spec an object is built from, without writing JSON.
 *
 * Deliberately NOT one builder per source or layer type. A named setter per option would have to
 * grow every time the SDK gains one, which is the maintenance the facade exists to remove - so
 * `set` is the whole surface and a new option works the day it is declared.
 */
__attribute__ ((visibility("default")))
NS_SWIFT_NAME(Spec)
@interface MSFSpec : NSObject

/** A spec of the given type - "http", "mbtiles", "vector", "raster", "cartocss"... */
+ (instancetype)of:(NSString *)type;

/** Sets a key. Chainable. The value may be a string, number, MSFSpec or array. */
- (instancetype)set:(NSString *)key value:(id)value;

/** The spec as JSON, which is what the facade actually takes. */
@property (nonatomic, readonly) NSString *json;

@end

/**
 * A live event subscription.
 *
 * Removing it is `invalidate`, and it removes itself on dealloc - so a subscription stored in a
 * property dies with its owner rather than outliving the object that reads it.
 */
__attribute__ ((visibility("default")))
NS_SWIFT_NAME(Subscription)
@interface MSFSubscription : NSObject

@property (nonatomic, readonly, getter=isActive) BOOL active;

/** Removes it. Idempotent. */
- (void)invalidate;

@end

/**
 * The registry, for objects that do not belong to a map.
 *
 * This is the half of the object API worth keeping: a source driving an app's own tile server, or
 * one source shared by two maps, without either map owning it. Ids are per kind.
 */
__attribute__ ((visibility("default")))
NS_SWIFT_NAME(Massif)
@interface MSFMassif : NSObject

/**
 * Builds a source, or returns the existing one when the spec is IDENTICAL - so two maps can ask
 * for the same source without coordinating.
 * @return nil when the spec does not build, with the reason in `error`.
 */
+ (nullable MSFMassifSource *)source:(NSString *)objectId
                                spec:(MSFSpec *)spec
                               error:(NSError **)error;

/** The source registered under an id, or nil. */
+ (nullable MSFMassifSource *)source:(NSString *)objectId;

+ (nullable MSFMassifObject *)style:(NSString *)objectId
                               spec:(MSFSpec *)spec
                              error:(NSError **)error;

+ (nullable MSFMassifObject *)style:(NSString *)objectId;

/** Builds a layer. It is NOT added to a map - use MSFMassifMap's add for that. */
+ (nullable MSFMassifLayer *)layer:(NSString *)objectId
                              spec:(MSFSpec *)spec
                             error:(NSError **)error;

+ (nullable MSFMassifLayer *)layer:(NSString *)objectId;

+ (BOOL)has:(NSString *)kind objectId:(NSString *)objectId;

/** Drops an id. Handles held elsewhere go stale rather than dangling. */
+ (BOOL)destroy:(NSString *)kind objectId:(NSString *)objectId;

/** The object-API source behind an id, for anything the facade does not reach yet. */
+ (nullable MSFTileDataSource *)rawSource:(NSString *)objectId;

@end

/** The error domain every facade failure is reported under. The code is the facade's Result. */
FOUNDATION_EXPORT NSString * const MSFMassifErrorDomain;

NS_ASSUME_NONNULL_END

#endif
