#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * Flying between places, moving everything in one flight.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/camera/FlyToExample.java.
 */
@interface MSFFlyToExample : NSObject <MSFExample>
@end

@implementation MSFFlyToExample

+ (NSString *)exampleId {
    return @"fly-to";
}

/** A tile server wants to know who is asking: a real app identifies itself. */
static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

- (void)startWithHost:(id<MSFExampleHost>)host {
    MSFMassifMap *map = host.map;

    // The Massif streets style (styles/massif/carto, bundled by the build as styles/massif.zip) over
    // OpenFreeMap's vector tiles, cached on disk: a free service a demo would re-fetch every run.
    [map addLayer:@"basemap"
             spec:[[[MSFSpec of:@"vector"]
                     set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
                         set:@"databasePath" value:[host cachePath:@"openfreemap.db"]]
                         set:@"capacity" value:@(100 * 1024 * 1024)]
                         set:@"source" value:[[[[MSFSpec of:@"http"]
                             set:@"url" value:@"https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf"]
                             set:@"maxZoom" value:@14]
                             set:@"HTTPHeaders" value:[[MSFSpec object] set:@"User-Agent" value:kUserAgent]]]]
                     set:@"style" value:[[MSFSpec of:@"mbvt"] set:@"project" value:[[[MSFSpec of:@"project"]
                         set:@"assets" value:[[MSFSpec of:@"zip"] set:@"data" value:[[MSFSpec of:@"url"]
                             set:@"url" value:@"assets://styles/massif.zip"]]]
                         set:@"name" value:@"streets"]]]
            error:nil];
    [map.camera moveTo:[MSFPosition positionWithLng:5.7245 lat:45.1885] zoom:6];

    // name, lon, lat, zoom, rotation, tilt
    NSArray *places = @[
        @[ @"Mont Blanc", @6.8652, @45.8326, @12.5, @0,   @60 ],
        @[ @"Grenoble",   @5.7245, @45.1885, @13.5, @25,  @45 ],
        @[ @"Verdon",     @6.3320, @43.7500, @13.0, @-30, @70 ],
    ];
    for (NSArray *place in places) {
        [host button:place[0] action:^{
            // animate(seconds) applies to the next move only, then resets to 0.
            [[map.camera animate:3]
                moveTo:[MSFPosition positionWithLng:[place[1] doubleValue] lat:[place[2] doubleValue]]
                  zoom:[place[3] floatValue]
              rotation:[place[4] floatValue]
                  tilt:[place[5] floatValue]];
        }];
    }
    [host button:@"Stop" action:^{ [map.camera stop]; }];
    [host caption:@"Tap a place. Each flight moves all four camera values at once."];
}

@end
