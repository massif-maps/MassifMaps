#import <Foundation/Foundation.h>

@class DemoMap;

NS_ASSUME_NONNULL_BEGIN

/**
 * Changing a knob on the RUNNING bench, the iOS counterpart of Android's DemoLive.
 *
 *   xcrun simctl openurl <device> 'massifdemo://config?fog=false'
 *   xcrun simctl openurl <device> 'massifdemo://config?zoom=14&tilt=60'
 *
 * A relaunch rebuilds every cache, which is exactly what hides a stale-redraw bug - so a bench
 * needs a way to change one value without one. Android uses a broadcast; simctl has no broadcast,
 * but it can open a URL, and the key names are the same as the launch arguments either way.
 *
 * Only the option groups whose keys arrive are re-applied, and the camera is left alone unless a
 * camera key is sent.
 */
@interface DemoLive : NSObject

/** Called by the bench when it comes up, so a URL knows what to talk to. */
+ (void)attach:(DemoMap *)demo;
+ (void)detach:(DemoMap *)demo;

/** @return NO when no bench is running, or nothing in the values was recognised. */
+ (BOOL)apply:(NSDictionary<NSString *, NSString *> *)values;

@end

NS_ASSUME_NONNULL_END
