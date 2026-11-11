#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * Shaded relief computed on the fly from an elevation source and laid over the basemap.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/terrain/HillshadeExample.java.
 */
@interface MSFHillshadeExample : NSObject <MSFExample>
@end

@implementation MSFHillshadeExample

+ (NSString *)exampleId {
    return @"hillshade";
}

static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

static NSArray<NSString *> *methods(void) {
    return @[ @"IGOR", @"COMBINED", @"MULTIDIRECTIONAL", @"STANDARD", @"BASIC" ];
}

- (void)startWithHost:(id<MSFExampleHost>)host {
    MSFMassifMap *map = host.map;

    [map style:@"massif"
          spec:[[MSFSpec of:@"mbvt"]
                  set:@"project" value:[[[MSFSpec of:@"project"]
                      set:@"assets" value:[[MSFSpec of:@"zip"]
                          set:@"data" value:[[MSFSpec of:@"url"]
                              set:@"url" value:@"assets://styles/massif.zip"]]]
                      set:@"name" value:@"streets"]]
         error:nil];
    [map addLayer:@"basemap"
             spec:[[[MSFSpec of:@"vector"]
                     set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
                         set:@"databasePath" value:[host cachePath:@"openfreemap.db"]]
                         set:@"capacity" value:@(100 * 1024 * 1024)]
                         set:@"source" value:[[[[MSFSpec of:@"http"]
                             set:@"url" value:@"https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf"]
                             set:@"maxZoom" value:@14]
                             set:@"HTTPHeaders" value:[[MSFSpec object] set:@"User-Agent" value:kUserAgent]]]]
                     set:@"style" value:@"massif"]
            error:nil];
    // heightScale 1 is the true slope, what MapLibre shades; the default is a twentieth of it.
    MSFMassifLayer *hillshade =
        [map addLayer:@"hillshade"
                 spec:[[[[MSFSpec of:@"hillshade"]
                         set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
                             set:@"databasePath" value:[host cachePath:@"mapterhorn-dem.db"]]
                             set:@"capacity" value:@(200 * 1024 * 1024)]
                             set:@"source" value:[[[[[MSFSpec of:@"http"]
                                 set:@"url" value:@"https://tiles.mapterhorn.com/{z}/{x}/{y}.webp"]
                                 set:@"minZoom" value:@1]
                                 set:@"maxZoom" value:@16]
                                 set:@"metaData" value:[[MSFSpec object] set:@"dem_encoding" value:@"terrarium"]]]]
                         set:@"hillshadeMethod" value:methods()[0]]
                         set:@"heightScale" value:@1]
                error:nil];
    [map.camera moveTo:[MSFPosition positionWithLng:5.83 lat:45.25] zoom:10.5];

    __block NSUInteger method = 0;
    [host button:@"Method" action:^{
        method = (method + 1) % methods().count;
        [hillshade set:@"hillshadeMethod" value:methods()[method]];
        [host caption:[NSString stringWithFormat:@"%@ hillshade.", methods()[method]]];
    }];
    // A shader uniform: the relief changes without a tile being decoded again.
    [host slider:@"Exaggeration" min:0 max:3 value:1 action:^(float value) {
        [hillshade set:@"exaggeration" value:@(value)];
    }];
    [host caption:@"Chartreuse, Vercors and Belledonne, shaded from a terrarium DEM."];
}

@end
