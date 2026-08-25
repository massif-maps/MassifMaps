#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"
#import "api/MSFMapEvents.h"

/**
 * The camera event an app should hang its data refresh on, and how to tell whose move it was.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/interaction/MapEventsExample.java.
 */
@interface MSFMapEventsExample : NSObject <MSFExample>
@end

@implementation MSFMapEventsExample {
    // A subscription unsubscribes when the last reference to it goes, so dropping the return value
    // would leave the handler registered for exactly as long as this statement.
    MSFSubscription *_move;
    MSFSubscription *_stable;
    int _moves;
    int _stables;
}

+ (NSString *)exampleId {
    return @"map-events";
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

    [map addLayer:@"basemap"
             spec:[[[MSFSpec of:@"vector"]
                     // Cached on disk in front of the server: openfreemap is a free service, and
                     // a demo that gets panned around re-fetches the same tiles on every run.
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

    [map eventProjection:@"EPSG:4326"];
    [map.camera moveTo:[MSFPosition positionWithLng:6.8652 lat:45.8326] zoom:11];

    // Every camera change, whatever caused it - 47 to 159 a second during a drag, which is exactly
    // why it is the wrong place to refresh anything. Throttled to 4 a second: events inside the
    // window are DROPPED, not delivered late, because the payload does not outlive the emit. Good
    // for a readout that should track the movement.
    _move = [map onMove:^(MSFMapMoveEvent *e) {
        self->_moves++;
        [host caption:[NSString stringWithFormat:@"moving - %d frames delivered", self->_moves]];
    } throttle:250];

    // The end of a movement, once, with what caused it. A tap that did not move the camera does
    // not fire it, so there is no "did it actually move?" flag to keep.
    _stable = [map onStable:^(MSFMapMoveEvent *e) {
        self->_stables++;
        // A refresh should follow the USER, not the app's own camera calls - otherwise the
        // "fly here" button below would trigger the very fetch it just made stale.
        NSString *action = e.byUser
            ? @"refreshing"
            : [NSString stringWithFormat:@"ignored (%@)",
               e.cause == MSFMapMoveCauseAnimation ? @"animation" : @"api"];
        [host caption:[NSString stringWithFormat:@"%d moves -> %d stable  ·  %@",
                       self->_moves, self->_stables, action]];
    }];

    [host button:@"Fly to Chamonix" action:^{
        // Raises stable with cause "animation", not "gesture".
        [[map.camera animate:1.5f] moveTo:[MSFPosition positionWithLng:6.8652 lat:45.9237] zoom:13];
    }];

    [host caption:@"Drag the map, then press the button. Watch the reason change."];
}

@end
