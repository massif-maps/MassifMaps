#import "DemoLive.h"
#import "DemoConfig.h"
#import "DemoMap.h"

@implementation DemoLive

static __weak DemoMap *sDemo;

+ (void)attach:(DemoMap *)demo {
    sDemo = demo;
}

+ (void)detach:(DemoMap *)demo {
    if (sDemo == demo) {
        sDemo = nil;
    }
}

+ (BOOL)apply:(NSDictionary<NSString *, NSString *> *)values {
    DemoMap *demo = sDemo;
    if (!demo || values.count == 0) {
        NSLog(@"live config: no bench running");
        return NO;
    }
    // The launch arguments land in NSUserDefaults, and DemoConfig reads them from there - so
    // writing them the same way means one key map rather than two.
    for (NSString *key in values) {
        [[NSUserDefaults standardUserDefaults] setObject:values[key] forKey:key];
    }
    [DemoConfig applyLaunchArgumentOverrides];
    NSLog(@"live config: %@", values);

    // Only what the keys touch. Re-applying everything would rebuild caches the change was meant
    // to leave alone, which is the whole reason this exists.
    NSSet<NSString *> *keys = [NSSet setWithArray:values.allKeys];
    if ([self keys:keys intersect:@[ @"terrain", @"exaggeration", @"meshResolution", @"drape",
                                     @"drapeLines", @"drapeResolution", @"viewDistance",
                                     @"cameraClearance", @"stitch" ]]) {
        [demo applyTerrainOptions];
    }
    if ([self keys:keys intersect:@[ @"fog", @"fogRangeStart", @"fogRangeEnd", @"fogHigh",
                                     @"fogSpace", @"fogBlend", @"fogHorizon" ]]) {
        [demo applyFogOptions];
    }
    if ([self keys:keys intersect:@[ @"sky", @"daycycle", @"sunHour" ]]) {
        [demo applySkyOptions];
    }
    if ([self keys:keys intersect:@[ @"sunAzimuth", @"sunAltitude", @"shadow", @"light" ]]) {
        [demo applyLightOptions];
    }
    if ([self keys:keys intersect:@[ @"hs", @"hillshade" ]]) {
        [demo applyHillshadeConfig];
    }
    if ([self keys:keys intersect:@[ @"contour" ]]) {
        [demo applyContourConfig];
    }
    // A style knob is carried by the TILES, so it needs a re-decode rather than an option apply.
    if ([self keys:keys intersect:@[ @"style", @"styleLight", @"bld3d", @"base" ]]) {
        [demo rebuildBaseLayer];
    }
    if ([self keys:keys intersect:@[ @"lon", @"lat", @"zoom", @"tilt", @"rotation" ]]) {
        [demo applyCamera];
    }
    return YES;
}

+ (BOOL)keys:(NSSet<NSString *> *)keys intersect:(NSArray<NSString *> *)candidates {
    for (NSString *candidate in candidates) {
        if ([keys containsObject:candidate]) {
            return YES;
        }
    }
    return NO;
}

@end
