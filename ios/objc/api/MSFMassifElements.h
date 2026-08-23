/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MSFMASSIFELEMENTS_H_
#define _MSFMASSIFELEMENTS_H_

#import <Foundation/Foundation.h>
#import "MSFMapEvents.h"

@class MSFSpec;
@class MSFMassifObject;
@class MSFMassifLayer;
@class MSFMassifMap;
@class MSFSubscription;

NS_ASSUME_NONNULL_BEGIN

/**
 * The map's own markers and popups.
 *
 * The mapbox-style half of the API - addMarker, remove, clear - with the SDK's style builders kept
 * out of it: an element and its style are BOTH described by a spec, so an app that wants a bigger
 * pin changes a number in JSON rather than reaching for MSFMarkerStyleBuilder.
 *
 * The layer and the source behind this are created on first use and released with the map. They
 * are ordinary registered objects, so ordering, opacity and visibility are the same properties as
 * on any other layer.
 */
__attribute__ ((visibility("default")))
NS_SWIFT_NAME(MassifElements)
@interface MSFMassifElements : NSObject

/**
 * Adds an element - a "marker", a "balloon". The spec's "style" may name a style registered
 * earlier or carry one inline, which is what most apps write.
 */
- (nullable MSFMassifObject *)add:(MSFSpec *)spec error:(NSError **)error;

/**
 * Registers a style under an id so many elements can share it - one style object rather than one
 * per marker, which is what matters once there are thousands.
 */
- (nullable MSFMassifObject *)style:(NSString *)styleId spec:(MSFSpec *)spec error:(NSError **)error;

/** Removes one element. It stays registered until it is closed. */
- (BOOL)remove:(MSFMassifObject *)element;

/** Removes every element this has added. */
- (instancetype)clear;

/** Clicks on the elements themselves, with the element's position on the payload. */
- (nullable MSFSubscription *)onClick:(MSFVectorElementClickHandler)handler NS_WARN_UNUSED_RESULT;

/**
 * The same, claiming the tap so the map's own onClick does not also fire - which is what an app
 * wants whenever "tap a marker" and "tap the map" mean different things.
 */
- (nullable MSFSubscription *)consumeClick:(MSFVectorElementClickFilter)handler NS_WARN_UNUSED_RESULT;

/** The layer they are drawn on, for opacity, visibility and ordering. */
@property (nonatomic, readonly, nullable) MSFMassifLayer *layer;

/** The source holding them, for anything the facade reaches on a local source. */
@property (nonatomic, readonly, nullable) MSFMassifObject *source;

@end

NS_ASSUME_NONNULL_END

#endif
