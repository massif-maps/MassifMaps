#import "DemoCfg.h"

@implementation DemoCfg

+ (BOOL)has:(NSString *)key {
    return [[NSUserDefaults standardUserDefaults] objectForKey:key] != nil;
}


+ (BOOL)hasKey:(NSString *)key {
    return [[NSUserDefaults standardUserDefaults] objectForKey:key] != nil;
}

+ (BOOL)boolFor:(NSString *)key defaultValue:(BOOL)defaultValue {
    if (![self hasKey:key]) {
        return defaultValue;
    }
    // Accept the Android spelling ('true'/'false') as well as 0/1, since the same knob is meant
    // to be described the same way for both demos.
    NSString *value = [[NSUserDefaults standardUserDefaults] stringForKey:key];
    return [value caseInsensitiveCompare:@"true"] == NSOrderedSame || [value intValue] != 0;
}

+ (float)floatFor:(NSString *)key defaultValue:(float)defaultValue {
    return [self hasKey:key] ? [[NSUserDefaults standardUserDefaults] floatForKey:key] : defaultValue;
}

+ (double)doubleFor:(NSString *)key defaultValue:(double)defaultValue {
    return [self hasKey:key] ? [[NSUserDefaults standardUserDefaults] doubleForKey:key] : defaultValue;
}

+ (int)intFor:(NSString *)key defaultValue:(int)defaultValue {
    return [self hasKey:key] ? (int)[[NSUserDefaults standardUserDefaults] integerForKey:key] : defaultValue;
}

+ (NSString *)stringFor:(NSString *)key defaultValue:(NSString *)defaultValue {
    NSString *value = [[NSUserDefaults standardUserDefaults] stringForKey:key];
    return value ?: defaultValue;
}

@end
