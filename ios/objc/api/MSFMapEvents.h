/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MSFMAPEVENTS_H_
#define _MSFMAPEVENTS_H_

#import <Foundation/Foundation.h>

@class MSFMapPos;
@class MSFMassifObject;

NS_ASSUME_NONNULL_BEGIN

/**
 * Typed event payloads.
 *
 * The point of this file: a handler should read `event.featureId`, not
 * `[MSFMassifApi getInt:payload path:@"featureId" defaultValue:-1]`. Each class is a thin, typed
 * view over the payload handle - NOTHING IS READ UNTIL IT IS ASKED FOR, so a feature with a long
 * geometry costs nothing unless the handler wants the geometry.
 *
 * An event is only valid for the duration of the handler. Copy out what you need; keeping it and
 * reading later gives nothing, because the payload has been freed.
 */
__attribute__ ((visibility("default")))
NS_SWIFT_NAME(MapEvent)
@interface MSFMapEvent : NSObject

/** The event name, for a handler subscribed to more than one. */
@property (nonatomic, readonly) NSString *name;

/** The object the event fired on. */
@property (nonatomic, readonly) MSFMassifObject *source;

/** Whether there is a payload at all - map.idle and map.moved carry none. */
@property (nonatomic, readonly) BOOL hasPayload;

/** Any payload property by path, for something a typed accessor does not cover yet. */
- (nullable NSString *)get:(NSString *)path;
- (double)getDouble:(NSString *)path defaultValue:(double)defaultValue;

/**
 * A position from the payload in a named projection, when one read wants a different one from the
 * subscription's - "EPSG:3857" for metres, say, on a handler set up for lon/lat.
 */
- (nullable MSFMapPos *)getPos:(NSString *)path projection:(NSString *)projection;

@end

/** A click on the map itself. */
__attribute__ ((visibility("default")))
NS_SWIFT_NAME(MapClickEvent)
@interface MSFMapClickEvent : MSFMapEvent

/** Where it landed, in the map's projection or the one set with eventProjection. */
@property (nonatomic, readonly, nullable) MSFMapPos *position;

/** 0 single, 1 long, 2 double, 3 dual. */
@property (nonatomic, readonly) int clickType;

@end

/** A pan, zoom, rotate or tilt the user drove. */
__attribute__ ((visibility("default")))
NS_SWIFT_NAME(MapInteractionEvent)
@interface MSFMapInteractionEvent : MSFMapEvent

@property (nonatomic, readonly) BOOL isPan;
@property (nonatomic, readonly) BOOL isZoom;
@property (nonatomic, readonly) BOOL isRotate;
@property (nonatomic, readonly) BOOL isTilt;

@end

/** A click on a feature of a vector tile layer. */
__attribute__ ((visibility("default")))
NS_SWIFT_NAME(VectorTileClickEvent)
@interface MSFVectorTileClickEvent : MSFMapEvent

@property (nonatomic, readonly) long long featureId;

/** The style layer the feature came from - "roads", "buildings". */
@property (nonatomic, readonly) NSString *layerName;

/** The clicked point, MultiPoint-aware rather than the centre of the whole feature. */
@property (nonatomic, readonly, nullable) MSFMapPos *position;

/** Where the finger landed, which is not the same as where the feature is. */
@property (nonatomic, readonly, nullable) MSFMapPos *clickPosition;

/** POINT 0, LINE 1, POLYGON 2, MULTIPOINT 3, MULTILINE 4, MULTIPOLYGON 5, COLLECTION 6. */
@property (nonatomic, readonly) int geometryType;

/** One property of the clicked feature, read on its own - the whole bag is never parsed. */
- (nullable NSString *)property:(NSString *)key;
- (double)propertyDouble:(NSString *)key defaultValue:(double)defaultValue;

/** Every property as JSON. Only pay for this when you really want them all. */
@property (nonatomic, readonly) NSString *properties;

/** The geometry as GeoJSON, serialised by the SDK rather than by each binding. */
@property (nonatomic, readonly) NSString *geoJson;

@end

/** A click on a vector element - a marker, a line, a polygon an app added. */
__attribute__ ((visibility("default")))
NS_SWIFT_NAME(VectorElementClickEvent)
@interface MSFVectorElementClickEvent : MSFMapEvent

@property (nonatomic, readonly, nullable) MSFMapPos *position;
@property (nonatomic, readonly, nullable) MSFMapPos *clickPosition;
@property (nonatomic, readonly) int clickType;

@end

/** Called on the thread the subscription asked for. */
typedef void (^MSFMapEventHandler)(MSFMapEvent *event) NS_SWIFT_NAME(MapEventHandler);
typedef void (^MSFMapClickHandler)(MSFMapClickEvent *event) NS_SWIFT_NAME(MapClickHandler);
typedef void (^MSFMapInteractionHandler)(MSFMapInteractionEvent *event) NS_SWIFT_NAME(MapInteractionHandler);
typedef void (^MSFVectorTileClickHandler)(MSFVectorTileClickEvent *event) NS_SWIFT_NAME(VectorTileClickHandler);

/** The same, for a handler that can claim the event. Return YES and the gesture is handled. */
typedef void (^MSFVectorElementClickHandler)(MSFVectorElementClickEvent *event)
    NS_SWIFT_NAME(VectorElementClickHandler);

/**
 * A handler that can CLAIM the tap: return YES and the SDK treats the gesture as handled, so the
 * map's own onClick does not also fire for it. Without this a marker tap runs both handlers, and
 * the usual pair of "open a popup on the marker" and "dismiss it when the map is tapped" cancel
 * each other out.
 */
typedef BOOL (^MSFVectorElementClickFilter)(MSFVectorElementClickEvent *event)
    NS_SWIFT_NAME(VectorElementClickFilter);

typedef BOOL (^MSFVectorTileClickFilter)(MSFVectorTileClickEvent *event) NS_SWIFT_NAME(VectorTileClickFilter);

NS_ASSUME_NONNULL_END

#endif
