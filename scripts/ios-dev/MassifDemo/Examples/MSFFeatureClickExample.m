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
    MSFSubscription *_mapClick;
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
                         // Cached on disk in front of the server: openfreemap is a free service,
                         // and a demo panned around re-fetches the same tiles on every run.
                         set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
                             set:@"databasePath" value:[host cachePath:@"openfreemap.db"]]
                             set:@"capacity" value:@(100 * 1024 * 1024)]
                             set:@"source" value:[[[[MSFSpec of:@"http"]
                                 set:@"url" value:@"https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf"]
                                 set:@"maxZoom" value:@14]
                                 set:@"HTTPHeaders" value:[[MSFSpec object]
                                     set:@"User-Agent" value:@"MassifMapsExamples/1.0"]]]]
                         set:@"style" value:@"alpine"]
                error:nil];

    [map.camera moveTo:[MSFPosition positionWithLng:5.7245 lat:45.1885] zoom:14.5];

    // consumeFeatureClick, not onFeatureClick: returning YES CLAIMS the click, so nothing after
    // this handler sees it - no other subscriber, and no map.clicked. That is how you stop as soon
    // as you have found the feature you care about, instead of letting the tap fall through and
    // also drop a pin.
    //
    // It runs on the thread the click came from, because the SDK asks whether the event was
    // consumed NOW rather than later. Keep it short and do not touch the map from it.
    _featureClick = [base consumeFeatureClick:^BOOL(MSFVectorTileClickEvent *e) {
        // Each of these is one read out of the payload. Nothing else is touched.
        NSString *name = [e property:@"name"];
        NSString *kind = [e property:@"class"];
        if (!name) {
            // Nothing worth stopping for - let the click carry on to the map.
            return NO;
        }
        MSFPosition *where = e.position;
        [host caption:[NSString stringWithFormat:@"took %@ - %@%@  %.5f, %.5f",
                       e.layerName, name,
                       kind ? [NSString stringWithFormat:@" (%@)", kind] : @"",
                       where ? where.lat : 0, where ? where.lng : 0]];
        return YES;
    }];

    // Only reached when the handler above declined: a named feature never gets here.
    _mapClick = [map onClick:^(MSFMapClickEvent *e) {
        [host caption:@"nothing named there - the click fell through to the map"];
    }];

    [host caption:@"Tap a named road or building, then somewhere empty."];
}

@end
