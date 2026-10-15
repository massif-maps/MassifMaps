#import <Foundation/Foundation.h>
#import "MassifMaps.h"

@class DemoMap;

/**
 * The one-shot actions, the counterpart of scripts/android-dev's DemoTests.java: things you trigger
 * from the panel rather than configure - routing, search, the GeoJSON tests and the maneuver-arrow
 * gallery. Anything that adds a layer of its own adds it here so 'clear' can remove them without
 * touching the base map.
 */
@interface DemoTests : NSObject

/** Runs an action by name; the panel's ACTIONS rows are these strings. */
+ (void)run:(NSString *)action demo:(DemoMap *)demo;

/** The route test layer: a route as GeoJSON vector tiles, styled the way a navigation app draws. */
+ (MSFLayer *)createRouteTestLayer:(DemoMap *)demo;

/**
 * The maneuver gallery: one arrow per shape real navigation produces, laid out on a grid around the
 * start position, so the whole set can be judged in one screenshot instead of waiting for a route
 * to happen to contain a hairpin. Returns how many arrows it made.
 */
+ (int)seedManeuverArrows:(DemoMap *)demo;

/** The route of -routeFile as the app draws it (navigation line + an arrow on every turn); @return the arrow count. */
+ (int)drawRouteFile:(NSString *)path demo:(DemoMap *)demo;

@end
