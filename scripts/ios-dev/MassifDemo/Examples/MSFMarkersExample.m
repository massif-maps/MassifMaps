#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifElements.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"
#import "api/MSFMapEvents.h"

/**
 * Markers and a popup, both described entirely by JSON - no style builder anywhere.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/annotations/MarkersExample.java.
 */
@interface MSFMarkersExample : NSObject <MSFExample>
@end

@implementation MSFMarkersExample {
    MSFMassifObject *_popup;
    // A subscription unsubscribes when the last reference to it goes, so dropping the return value
    // would leave the handlers registered for exactly as long as this statement.
    MSFSubscription *_pinClick;
    MSFSubscription *_mapClick;
}

+ (NSString *)exampleId {
    return @"markers";
}

/** OSM's tile usage policy: a real app identifies itself, or the tiles come back as 403s. */
static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

/** name, lon, lat, metres */
static NSArray *summits(void) {
    return @[
        @[ @"Mont Blanc",       @6.8652, @45.8326, @4808 ],
        @[ @"Grandes Jorasses", @6.9866, @45.8697, @4208 ],
        @[ @"Aiguille Verte",   @6.9256, @45.9036, @4122 ],
        @[ @"Mont Dolent",      @7.0575, @45.9264, @3820 ],
    ];
}

- (void)startWithHost:(id<MSFExampleHost>)host {
    MSFMassifMap *map = host.map;

    [map addLayer:@"basemap"
             spec:[[MSFSpec of:@"raster"]
                     // Cached on disk in front of the server: OSM's tiles are a free service run
                     // on donations, and a demo that gets panned around re-fetches the same ones
                     // on every run.
                     set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
                         set:@"databasePath" value:[host cachePath:@"osm-raster.db"]]
                         set:@"capacity" value:@(100 * 1024 * 1024)]
                         set:@"source" value:[[[[MSFSpec of:@"http"]
                             set:@"url" value:@"https://tile.openstreetmap.org/{z}/{x}/{y}.png"]
                             set:@"maxZoom" value:@19]
                             set:@"HTTPHeaders" value:[[MSFSpec object] set:@"User-Agent" value:kUserAgent]]]]
            error:nil];

    // One style object shared by every marker - what matters once there are thousands of them.
    // A "style" key that is a STRING is looked up by id; an object would be built inline.
    MSFMassifObject *pin = [map.elements style:@"pin"
                                          spec:[[[[MSFSpec of:@"marker"]
                                              set:@"size" value:@26]
                                              set:@"color" value:@0xFFE5484D]
                                              set:@"clickSize" value:@40]
                                         error:nil];

    for (NSArray *summit in summits()) {
        [map addMarker:[[[MSFSpec of:@"marker"]
                           set:@"position" value:@[ summit[1], summit[2] ]]
                           set:@"style" value:pin.objectId]
                 error:nil];
    }

    [map.camera moveTo:[MSFPosition positionWithLng:6.94 lat:45.87] zoom:10.6];

    // consumeClick, not onClick: a marker tap has to CLAIM the gesture, or the map's own onClick
    // below fires for the same tap and dismisses the popup as it opens.
    _pinClick = [map.elements consumeClick:^BOOL(MSFVectorElementClickEvent *e) {
        [self show:map at:e.position];
        return YES;
    }];
    _mapClick = [map onClick:^(MSFMapClickEvent *e) {
        [self dismiss:map];
    }];
    [host caption:@"Tap a pin. Everything here - marker, style, popup - is a JSON spec."];
}

/** A balloon at a position, built the same way a marker is. */
- (void)show:(MSFMassifMap *)map at:(MSFPosition *)position {
    [self dismiss:map];
    if (!position) {
        return;
    }
    NSString *name = @"Summit";
    int metres = 0;
    for (NSArray *summit in summits()) {
        if (fabs(position.lng - [summit[1] doubleValue]) < 1e-4 &&
            fabs(position.lat - [summit[2] doubleValue]) < 1e-4) {
            name = summit[0];
            metres = [summit[3] intValue];
        }
    }
    _popup = [map addPopup:[[[[[MSFSpec of:@"balloon"]
        set:@"position" value:@[ @(position.lng), @(position.lat) ]]
        set:@"title" value:name]
        set:@"description" value:[NSString stringWithFormat:@"%d m", metres]]
        set:@"style" value:[[[[[MSFSpec of:@"balloon"]
            set:@"cornerRadius" value:@6]
            set:@"leftColor" value:@0xFFE5484D]
            set:@"titleFontSize" value:@14]
            set:@"descriptionFontSize" value:@12]]
                     error:nil];
}

- (void)dismiss:(MSFMassifMap *)map {
    if (_popup) {
        [map.elements remove:_popup];
        [_popup destroy];
        _popup = nil;
    }
}

@end
