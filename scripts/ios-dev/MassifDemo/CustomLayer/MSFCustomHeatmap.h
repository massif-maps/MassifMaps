#import <Foundation/Foundation.h>

@class MSFMassifLayer;

NS_ASSUME_NONNULL_BEGIN

/**
 * The custom layers of scripts/android-dev/app/src/main/cpp/heatmap_layer.c - the same C file the
 * Android demo builds - a colour legend in screen space and a heatmap on the ground, both drawn by
 * C over the SDK's C ABI (mm_layer_create_custom). This is the Objective-C end: it registers them
 * and hands the heatmap its grid. The twin of CustomHeatmap.java.
 */
@interface MSFCustomHeatmap : NSObject

/** The grid is N x N cells over a square SIDE_METERS across, centred on the start. */
+ (int)gridSize;

/** The legend (scene NO) or the heatmap (scene YES), registered under an id; nil when it did not register. */
+ (nullable MSFMassifLayer *)createWithId:(NSString *)layerId scene:(BOOL)scene;

/** Longitude and latitude of the grid nodes, row by row, flat: lon, lat, lon, lat... (2 * N * N doubles). */
+ (NSData *)gridPositionsForLongitude:(double)lon latitude:(double)lat;

/** Hands the heatmap its grid: the ground height in metres (as drawn) at each node, N * N doubles, or nil for flat. */
+ (void)setHeatmapAtLongitude:(double)lon latitude:(double)lat heights:(nullable NSData *)heights;

/** Draped (the default): pulled forward of the ground in depth, so it lies on it between its nodes. */
+ (void)setDrape:(BOOL)drape;

@end

NS_ASSUME_NONNULL_END
