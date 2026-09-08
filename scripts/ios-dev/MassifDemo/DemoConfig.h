#import <Foundation/Foundation.h>

/**
 * Every knob in one place, the counterpart of scripts/android-dev's DemoConfig.java.
 *
 * The Java version is ~230 static fields plus an applyIntentOverrides that maps each one onto an
 * intent extra. Mirroring that field for field in Objective-C would be ~2000 lines of property
 * boilerplate, so the values live in a dictionary keyed by the SAME names the Android demo uses
 * as intent extras. That keeps the thing that actually matters - a camera or a layer set reads
 * identically for both demos - and makes the override pass automatic: any key with a default
 * here can be set with '-key value' at launch, no per-knob plumbing.
 *
 *   xcrun simctl launch <device> com.massifmaps.MassifDemo -zoom 14 -hillshade true -style inline
 *
 * Read with the typed accessors; write with set*, which is what the panel does before calling
 * back into a DemoMap apply* method.
 */
@interface DemoConfig : NSObject

+ (BOOL)boolFor:(NSString *)key;
+ (float)floatFor:(NSString *)key;
+ (double)doubleFor:(NSString *)key;
+ (int)intFor:(NSString *)key;
+ (NSString *)stringFor:(NSString *)key;
/** "#rrggbb" or "#rrggbbaa" as an ARGB integer. */
+ (unsigned int)colorFor:(NSString *)key;

+ (void)setValue:(id)value forKey:(NSString *)key;

/**
 * The hour the sky is drawn for: the explicit 'sunHour' if one was given, the day cycle's hour
 * while it runs, and otherwise the real UTC hour - so by default the demo shows the sky that is
 * actually up there.
 */
+ (double)currentHourUtc;

/** Fold the launch arguments over the defaults. Called once, before the map is built. */
+ (void)applyLaunchArgumentOverrides;

/** Every known key, for the settings panel. */
+ (NSArray<NSString *> *)allKeys;

@end
