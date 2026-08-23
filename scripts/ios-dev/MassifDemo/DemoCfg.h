#import <Foundation/Foundation.h>

/**
 * Launch-argument readers, the iOS counterpart of scripts/android-dev's DemoCfg.
 *
 * Android passes knobs as intent extras ('--es zoom 14'); iOS passes them as launch arguments
 * ('-zoom 14'), which UIKit folds into NSUserDefaults for us. Same idea, same key names, so a
 * camera or a layer set can be described identically for both demos:
 *
 *   adb shell am start ... --es zoom 14 --es hillshade true
 *   xcrun simctl launch <device> com.massifmaps.MassifDemo -zoom 14 -hillshade true
 */
@interface DemoCfg : NSObject

/** Whether the key was given at all, so "leave the example's own value alone" is expressible. */
+ (BOOL)has:(NSString *)key;

+ (BOOL)boolFor:(NSString *)key defaultValue:(BOOL)defaultValue;
+ (float)floatFor:(NSString *)key defaultValue:(float)defaultValue;
+ (double)doubleFor:(NSString *)key defaultValue:(double)defaultValue;
+ (int)intFor:(NSString *)key defaultValue:(int)defaultValue;
+ (NSString *)stringFor:(NSString *)key defaultValue:(NSString *)defaultValue;

@end
