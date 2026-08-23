#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/**
 * A position on the facade: longitude, latitude and an optional altitude, in WGS84.
 *
 * Hand-written and facade-owned rather than MSFMapPos, which is a Swig proxy over a C++ object -
 * one allocation and one indirection per position, to carry two doubles, on a path a click
 * handler walks per event.
 *
 * Longitude first, like GeoJSON and like the JSON the facade carries. Deliberately not named
 * LatLng: a latitude-first type beside a longitude-first wire format is a swapped-coordinate bug
 * waiting to happen.
 */
@interface MSFPosition : NSObject

@property (nonatomic, readonly) double lng;
@property (nonatomic, readonly) double lat;
/** Metres above the ellipsoid. 0 when the value carried none. */
@property (nonatomic, readonly) double alt;

- (instancetype)initWithLng:(double)lng lat:(double)lat;
- (instancetype)initWithLng:(double)lng lat:(double)lat alt:(double)alt NS_DESIGNATED_INITIALIZER;
+ (instancetype)positionWithLng:(double)lng lat:(double)lat;

- (instancetype)init NS_UNAVAILABLE;

@end

/** A bounding box, as a pair of positions. */
@interface MSFBounds : NSObject

@property (nonatomic, readonly) MSFPosition *min;
@property (nonatomic, readonly) MSFPosition *max;

- (instancetype)initWithMin:(MSFPosition *)min max:(MSFPosition *)max NS_DESIGNATED_INITIALIZER;
+ (instancetype)boundsWithMinLng:(double)minLng minLat:(double)minLat
                          maxLng:(double)maxLng maxLat:(double)maxLat;

- (instancetype)init NS_UNAVAILABLE;

@end

/**
 * A point in view coordinates - points from the top-left of the map view.
 *
 * Not a position: it carries no coordinate system and is never reprojected.
 */
@interface MSFScreenPoint : NSObject

@property (nonatomic, readonly) float x;
@property (nonatomic, readonly) float y;

- (instancetype)initWithX:(float)x y:(float)y NS_DESIGNATED_INITIALIZER;

- (instancetype)init NS_UNAVAILABLE;

@end

/** A rectangle in view coordinates - the part of the view a fitBounds should frame. */
@interface MSFScreenRect : NSObject

@property (nonatomic, readonly) MSFScreenPoint *min;
@property (nonatomic, readonly) MSFScreenPoint *max;

- (instancetype)initWithMin:(MSFScreenPoint *)min max:(MSFScreenPoint *)max NS_DESIGNATED_INITIALIZER;
+ (instancetype)rectWithLeft:(float)left top:(float)top right:(float)right bottom:(float)bottom;

- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
