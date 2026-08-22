/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MSFMASSIFINTERNAL_H_
#define _MSFMASSIFINTERNAL_H_

#import "MSFMassif.h"
#import "MSFMassifObject.h"
#import "MSFMassifMap.h"
#import "MSFMapEvents.h"
#import "MSFMassifElements.h"

@class MSFMapPos;

NS_ASSUME_NONNULL_BEGIN

/** How a handler is built from a raw payload. One per typed event class. */
typedef NS_ENUM(NSInteger, MSFEventKind) {
    MSFEventKindPlain,
    MSFEventKindClick,
    MSFEventKindInteraction,
    MSFEventKindTileClick,
    MSFEventKindElementClick
};

/** Where a handler runs. Main is the default because that is where an app wants to be. */
typedef NS_ENUM(NSInteger, MSFDelivery) {
    MSFDeliveryOrigin = 0,
    MSFDeliveryMain = 1,
    MSFDeliveryBackground = 2
};

/**
 * The JSON the facade uses for the small by-value structs, as the SDK's own types. One place, so
 * no binding grows its own position parser.
 */
@interface MSFValues : NSObject
+ (nullable MSFMapPos *)posFromJson:(nullable NSString *)json;
+ (NSString *)jsonFromPos:(MSFMapPos *)pos;
+ (NSString *)argsJson:(nullable NSArray *)args;
+ (NSError *)errorWithResult:(int)result message:(NSString *)message;
/** A string property, or nil - Swig's std::string typemap will not take a nil default. */
+ (nullable NSString *)stringOrNil:(int)handle path:(NSString *)path;
+ (nullable NSString *)string:(int)handle path:(NSString *)path defaultValue:(nullable NSString *)defaultValue;
@end

@interface MSFSpec ()
@property (nonatomic, readonly) NSMutableDictionary *values;
@end

@interface MSFSubscription ()
/**
 * @param listener Retained for the subscription's lifetime. The C++ side holds the director
 *        through a raw pointer, so without this ARC frees it the moment subscribe returns and the
 *        handler silently never runs.
 */
- (instancetype)initWithId:(int)subscriptionId listener:(id)listener;
@end

@interface MSFMassifObject ()
- (instancetype)initWithHandle:(int)handle kind:(nullable NSString *)kind objectId:(nullable NSString *)objectId;
- (nullable MSFSubscription *)subscribe:(NSString *)event
                                   kind:(MSFEventKind)kind
                               delivery:(MSFDelivery)delivery
                               coalesce:(BOOL)coalesce
                             projection:(nullable NSString *)projection
                                  block:(id)block
                             consuming:(BOOL)consuming;
@end

@interface MSFMassifLayer ()
- (instancetype)initWithHandle:(int)handle objectId:(nullable NSString *)objectId map:(nullable MSFMassifMap *)map;
@end

@interface MSFPropertyGroup ()
- (instancetype)initWithObject:(MSFMassifObject *)target prefix:(NSString *)prefix;
@end

@interface MSFMapEvent ()
- (instancetype)initWithTarget:(int)target name:(NSString *)name payload:(int)payload;
@property (nonatomic, readonly) int payload;
@end

@interface MSFMassifElements ()
- (instancetype)initWithMap:(MSFMassifMap *)map
                   objectId:(NSString *)objectId
                 sourceSpec:(nullable MSFSpec *)sourceSpec;
@end

@interface MSFMapCamera ()
- (instancetype)initWithView:(id)view;
@end

NS_ASSUME_NONNULL_END

#endif
