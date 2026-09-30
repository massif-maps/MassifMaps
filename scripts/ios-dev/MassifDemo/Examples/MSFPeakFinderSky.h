#import <Foundation/Foundation.h>

@class MSFMassifMap;
@class MSFMassifLayer;

NS_ASSUME_NONNULL_BEGIN

/**
 * The peak finder's sun, ported from web/examples/peak-finder/sun.mjs: the day's path, the sun now,
 * hour marks, and where it rises and sets over the TERRAIN in front of the viewpoint.
 */
@interface MSFPeakFinderSky : NSObject

/** The path goes on `below`, under the summit names; the sun and the times on `above`. */
- (instancetype)initWithMap:(MSFMassifMap *)map below:(MSFMassifLayer *)below above:(MSFMassifLayer *)above;

/** Places the sun now and replans what moved; returns the rise and set, for the caption. */
- (NSString *)updateLat:(double)lat lon:(double)lon eye:(double)eye hours:(BOOL)hours;

+ (NSString *)compass:(double)heading;

@end

NS_ASSUME_NONNULL_END
