#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * A CartoCSS style project, and the two kinds of runtime parameter it can declare.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/styles/StyleParametersExample.java.
 */
@interface MSFStyleParametersExample : NSObject <MSFExample>
@end

@implementation MSFStyleParametersExample {
    NSUInteger _water;
}

+ (NSString *)exampleId {
    return @"style-parameters";
}

static NSArray<NSString *> *waterColours(void) {
    return @[ @"#8fb8d8", @"#2f6f4f", @"#7f5af0" ];
}

- (void)startWithHost:(id<MSFExampleHost>)host {
    MSFMassifMap *map = host.map;

    // The style project is a zip in the bundle: alpine.json (which declares the parameters) plus
    // style.mss, zipped from scripts/android-dev/app/src/main/style-projects/alpine by the
    // project's own build phase - the same folder the Android demo zips into its assets.
    //
    // Registered under an id of its own rather than inlined in the layer spec, because the example
    // talks to it afterwards - a layer's style property cannot be read back as a handle. A spec key
    // that is a STRING is looked up in the registry, which is what "style": "alpine" below does.
    //
    // The parameters are part of the spec, so the style is built with them already applied rather
    // than being corrected on the first frame.
    MSFMassifObject *style = [map style:@"alpine"
                                   spec:[[[MSFSpec of:@"mbvt"]
                                           set:@"project" value:[[MSFSpec of:@"project"]
                                               set:@"assets" value:[[MSFSpec of:@"zip"]
                                                   set:@"data" value:[[MSFSpec of:@"url"]
                                                       set:@"url" value:@"assets://styles/alpine.zip"]]]]
                                           set:@"params" value:[[[MSFSpec object]
                                               set:@"water_color" value:waterColours()[0]]
                                               set:@"show_buildings" value:@"true"]]
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

    [map.camera moveTo:[MSFPosition positionWithLng:5.7245 lat:45.1885] zoom:13.5];

    [host button:@"Water colour" action:^{
        self->_water = (self->_water + 1) % waterColours().count;
        // A style parameter is a PROPERTY: the rest of the path is the parameter's name.
        // LIVE: the decoded tiles already point at this value, so it swaps and redraws.
        [style set:@"params.water_color" value:waterColours()[self->_water]];
    }];
    [host toggle:@"Buildings" on:YES action:^(BOOL on) {
        // In a FILTER: this decides what the tile contains, so every tile decodes again.
        [style set:@"params.show_buildings" value:on ? @"true" : @"false"];
    }];
    [host button:@"Night" action:^{
        // Several at once, in ONE crossing - which is what a theme swap is.
        [style apply:[[MSFSpec object] set:@"params" value:[[[MSFSpec object]
            set:@"water_color" value:@"#0b2b4a"]
            set:@"show_buildings" value:@"false"]]];
    }];
    [host caption:@"Two parameters, two costs: a colour swaps live, a filter re-decodes."];
}

@end
