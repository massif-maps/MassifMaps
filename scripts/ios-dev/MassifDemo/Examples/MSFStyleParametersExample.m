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
    MSFMassifObject *style = [map style:@"alpine"
                                   spec:[[MSFSpec of:@"mbvt"]
                                           set:@"project" value:[[MSFSpec of:@"project"]
                                               set:@"assets" value:[[MSFSpec of:@"zip"]
                                                   set:@"data" value:[[MSFSpec of:@"url"]
                                                       set:@"url" value:@"assets://styles/alpine.zip"]]]]
                                  error:nil];

    [map addLayer:@"basemap"
             spec:[[[MSFSpec of:@"vector"]
                     set:@"source" value:[[[[MSFSpec of:@"http"]
                         set:@"url" value:@"https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf"]
                         set:@"maxZoom" value:@14]
                         set:@"HTTPHeaders" value:[[MSFSpec object]
                             set:@"User-Agent" value:@"MassifMapsExamples/1.0"]]]
                     set:@"style" value:@"alpine"]
            error:nil];

    [map.camera moveTo:[[MSFMapPos alloc] initWithX:5.7245 y:45.1885] zoom:13.5];

    [host button:@"Water colour" action:^{
        self->_water = (self->_water + 1) % waterColours().count;
        // LIVE: the decoded tiles already point at this value, so it swaps and redraws.
        [self setOn:style name:@"water_color" value:waterColours()[self->_water]];
    }];
    [host toggle:@"Buildings" on:YES action:^(BOOL on) {
        // In a FILTER: this decides what the tile contains, so every tile decodes again.
        [self setOn:style name:@"show_buildings" value:on ? @"true" : @"false"];
    }];
    [host caption:@"Two parameters, two costs: a colour swaps live, a filter re-decodes."];
}

- (void)setOn:(MSFMassifObject *)style name:(NSString *)name value:(NSString *)value {
    // The result is the CALLER's - setStyleParameter returns nothing useful, but a handle is a
    // handle and leaking one per tap is still a leak.
    [[style call:@"setStyleParameter" args:@[ name, value ] error:nil] destroy];
}

@end
