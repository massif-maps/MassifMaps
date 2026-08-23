#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"
#import "api/MSFMapEvents.h"

/**
 * Reading the feature under a tap, without parsing a tile.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/interaction/FeatureClickExample.java.
 */
@interface MSFFeatureClickExample : NSObject <MSFExample>
@end

@implementation MSFFeatureClickExample {
    // A subscription unsubscribes when the last reference to it goes, so dropping the return value
    // would leave the handler registered for exactly as long as this statement.
    MSFSubscription *_featureClick;
}

+ (NSString *)exampleId {
    return @"feature-click";
}

- (void)startWithHost:(id<MSFExampleHost>)host {
    MSFMassifMap *map = host.map;

    [map style:@"alpine"
          spec:[[MSFSpec of:@"mbvt"]
                  set:@"project" value:[[MSFSpec of:@"project"]
                      set:@"assets" value:[[MSFSpec of:@"zip"]
                          set:@"data" value:[[MSFSpec of:@"url"]
                              set:@"url" value:@"assets://styles/alpine.zip"]]]]
         error:nil];

    MSFMassifLayer *base =
        [map addLayer:@"basemap"
                 spec:[[[MSFSpec of:@"vector"]
                         set:@"source" value:[[[[MSFSpec of:@"http"]
                             set:@"url" value:@"https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf"]
                             set:@"maxZoom" value:@14]
                             set:@"HTTPHeaders" value:[[MSFSpec object]
                                 set:@"User-Agent" value:@"MassifMapsExamples/1.0"]]]
                         set:@"style" value:@"alpine"]
                error:nil];

    [map.camera moveTo:[MSFPosition positionWithLng:5.7245 lat:45.1885] zoom:14.5];

    _featureClick = [base onFeatureClick:^(MSFVectorTileClickEvent *e) {
        // Each of these is one read out of the payload. Nothing else is touched.
        NSString *name = [e property:@"name"];
        NSString *kind = [e property:@"class"];
        MSFPosition *where = e.position;
        [host caption:[NSString stringWithFormat:@"%@%@%@  %.5f, %.5f",
                       e.layerName,
                       name ? [@" - " stringByAppendingString:name] : @"",
                       kind ? [NSString stringWithFormat:@" (%@)", kind] : @"",
                       where ? where.lat : 0, where ? where.lng : 0]];
    }];
    [host caption:@"Tap a road, a building or the water."];
}

@end
