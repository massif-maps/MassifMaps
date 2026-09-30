#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * A style's runtime parameters, and the two kinds a style can declare: Massif's own, changed live.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/styles/StyleParametersExample.java.
 */
@interface MSFStyleParametersExample : NSObject <MSFExample>
@end

@implementation MSFStyleParametersExample

+ (NSString *)exampleId {
    return @"style-parameters";
}

- (void)startWithHost:(id<MSFExampleHost>)host {
    MSFMassifMap *map = host.map;

    // Registered under an id of its own rather than inlined in the layer spec, because the example
    // talks to it afterwards - a layer's style property cannot be read back as a handle. The params
    // are part of the spec, so the first frame is already right.
    MSFMassifObject *style = [map style:@"massif"
                                   spec:[[[MSFSpec of:@"mbvt"]
                                           set:@"project" value:[[[MSFSpec of:@"project"]
                                               set:@"assets" value:[[MSFSpec of:@"zip"]
                                                   set:@"data" value:[[MSFSpec of:@"url"]
                                                       set:@"url" value:@"assets://styles/massif.zip"]]]
                                               set:@"name" value:@"streets"]]
                                           set:@"params" value:[[MSFSpec object] set:@"poiStyle" value:@"badge"]]
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
                     set:@"style" value:@"massif"]
            error:nil];

    [map.camera moveTo:[MSFPosition positionWithLng:5.7245 lat:45.1885] zoom:15.5];

    [host toggle:@"POI discs" on:YES action:^(BOOL on) {
        // A style parameter is a PROPERTY: the rest of the path is the parameter's name.
        // LIVE: the decoded tiles point at this value, so the discs come and go with a redraw.
        [style set:@"params.poiStyle" value:on ? @"badge" : @"plain"];
    }];
    [host toggle:@"Boundaries" on:YES action:^(BOOL on) {
        // In a FILTER: this decides what the tile contains, so every tile decodes again. A string,
        // converted against the DECLARED default - 1 here, so "0" becomes the number 0.
        [style set:@"params.show_boundaries" value:on ? @"1" : @"0"];
    }];
    [host button:@"Walker" action:^{
        // Several at once, in ONE crossing - which is what a theme swap is.
        [style apply:[[MSFSpec object] set:@"params" value:[[[[MSFSpec object]
            set:@"highlight_drinking_water" value:@"1"]
            set:@"path_min_zoom" value:@"12"]
            set:@"sac_scale_labels" value:@"1"]]];
    }];
    [host caption:@"Two parameters, two costs: a value swaps live, a filter re-decodes."];
}

@end
