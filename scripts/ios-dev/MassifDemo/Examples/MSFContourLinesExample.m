#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * Contour lines traced from an elevation source at runtime, styled with CartoCSS.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/terrain/ContourLinesExample.java.
 */
@interface MSFContourLinesExample : NSObject <MSFExample>
@end

@implementation MSFContourLinesExample

+ (NSString *)exampleId {
    return @"contour-lines";
}

static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

// `div` is the largest round divisor of the elevation, so `div >= 100` picks the index lines.
static NSString *contourStyle(void) {
    return [@[ @"@medium: 'ios:Helvetica Neue Medium, Roboto Medium, sans-serif Medium';",
               @"#contour {",
               @"  line-color: #a0663a;",
               @"  line-opacity: 0.7;",
               @"  line-width: 0.6;",
               @"  [div >= 100] {",
               @"    line-width: 1.3;",
               @"    text-name: [ele] + ' m';",
               @"    text-face-name: @medium;",
               @"    text-size: 10;",
               @"    text-fill: #7a4b28;",
               @"    text-halo-fill: #ffffffcc;",
               @"    text-halo-radius: 1.5;",
               @"    text-placement: line;",
               @"  }",
               @"}" ] componentsJoinedByString:@"\n"];
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
    // The contour source wraps the DEM: the tiles are fetched and decoded once, for both.
    MSFMassifSource *contours =
        [map source:@"contours"
               spec:[[[MSFSpec of:@"contour"]
                       set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
                           set:@"databasePath" value:[host cachePath:@"mapterhorn-dem.db"]]
                           set:@"capacity" value:@(200 * 1024 * 1024)]
                           set:@"source" value:[[[[[MSFSpec of:@"http"]
                               set:@"url" value:@"https://tiles.mapterhorn.com/{z}/{x}/{y}.webp"]
                               set:@"minZoom" value:@1]
                               set:@"maxZoom" value:@16]
                               set:@"metaData" value:[[MSFSpec object] set:@"dem_encoding" value:@"terrarium"]]]]
                       set:@"baseInterval" value:@20]
              error:nil];
    [map addLayer:@"contours"
             spec:[[[MSFSpec of:@"vector"]
                     set:@"source" value:@"contours"]
                     set:@"style" value:[[MSFSpec of:@"mbvt"]
                         set:@"cartocss" value:[[MSFSpec of:@"cartocss"] set:@"css" value:contourStyle()]]]
            error:nil];
    [map.camera moveTo:[MSFPosition positionWithLng:5.73 lat:45.215] zoom:13.5];

    for (NSNumber *interval in @[ @10, @20, @50 ]) {
        // A live property: the source traces again, the layer is not rebuilt.
        [host button:[NSString stringWithFormat:@"%@ m", interval] action:^{
            [contours set:@"baseInterval" value:interval];
            [host caption:[NSString stringWithFormat:@"Every %@ m, multiplied per zoom band.", interval]];
        }];
    }
    [host caption:@"Contours over the Bastille and the Chartreuse, traced from a terrarium DEM."];
}

@end
