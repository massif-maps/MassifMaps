#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * The Massif style family: five maps from one project, switched by a style parameter.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/styles/MassifVariantsExample.java.
 */
@interface MSFMassifVariantsExample : NSObject <MSFExample>
@end

@implementation MSFMassifVariantsExample {
    MSFMassifLayer *_base;
    MSFMassifSource *_dem;
    MSFMassifSource *_contours;
}

+ (NSString *)exampleId {
    return @"massif-variants";
}

static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

/** CompositeSourceType, as the facade takes it. */
static const int kSourceHillshade = 1;
static const int kSourceVector = 2;

static NSArray<NSArray<NSString *> *> *variants(void) {
    return @[ @[ @"streets", @"Streets", @"the everyday map" ],
              @[ @"outdoor", @"Outdoor", @"trails by difficulty, peaks, huts, cliffs" ],
              @[ @"topo", @"Topo", @"outdoor on a cooler, map-like ground" ],
              @[ @"hybrid", @"Hybrid", @"roads and labels over imagery" ],
              @[ @"eink", @"E-ink", @"black on white, for e-paper; inverts at night" ] ];
}

- (void)startWithHost:(id<MSFExampleHost>)host {
    MSFMassifMap *map = host.map;

    // Hybrid draws over imagery the app supplies: a raster under the vector layer, shown for it alone.
    MSFMassifLayer *imagery =
        [map addLayer:@"imagery"
                 spec:[[[MSFSpec of:@"raster"]
                         set:@"visible" value:@NO]
                         set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
                             set:@"databasePath" value:[host cachePath:@"world-imagery.db"]]
                             set:@"capacity" value:@(200 * 1024 * 1024)]
                             set:@"source" value:[[[[MSFSpec of:@"http"]
                                 set:@"url" value:@"https://server.arcgisonline.com/ArcGIS/rest/services/"
                                                   @"World_Imagery/MapServer/tile/{z}/{y}/{x}"]
                                 set:@"maxZoom" value:@18]
                                 set:@"HTTPHeaders" value:[[MSFSpec object] set:@"User-Agent" value:kUserAgent]]]]
                error:nil];

    // ONE project for all five (styles/massif/carto, bundled as styles/massif.zip).
    MSFMassifObject *style = [map style:@"massif"
                                   spec:[[MSFSpec of:@"mbvt"]
                                           set:@"project" value:[[[MSFSpec of:@"project"]
                                               set:@"assets" value:[[MSFSpec of:@"zip"]
                                                   set:@"data" value:[[MSFSpec of:@"url"]
                                                       set:@"url" value:@"assets://styles/massif.zip"]]]
                                               set:@"name" value:@"streets"]]
                                  error:nil];

    // Every variant has a `hillshade` and a `contour` slot; a composite layer fills them with the app's
    // own DEM, here only for outdoor and topo - see showRelief.
    _base = [map addLayer:@"basemap"
                     spec:[[[MSFSpec of:@"composite-vector"]
                             set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
                                 set:@"databasePath" value:[host cachePath:@"openfreemap.db"]]
                                 set:@"capacity" value:@(100 * 1024 * 1024)]
                                 set:@"source" value:[[[[MSFSpec of:@"http"]
                                     set:@"url" value:@"https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf"]
                                     set:@"maxZoom" value:@14]
                                     set:@"HTTPHeaders" value:[[MSFSpec object] set:@"User-Agent" value:kUserAgent]]]]
                             set:@"style" value:@"massif"]
                    error:nil];
    _dem = [map source:@"dem"
                  spec:[[[[MSFSpec of:@"persistent-cache"]
                          set:@"databasePath" value:[host cachePath:@"mapterhorn-dem.db"]]
                          set:@"capacity" value:@(200 * 1024 * 1024)]
                          set:@"source" value:[[[[[MSFSpec of:@"http"]
                              set:@"url" value:@"https://tiles.mapterhorn.com/{z}/{x}/{y}.webp"]
                              set:@"minZoom" value:@1]
                              set:@"maxZoom" value:@16]
                              set:@"metaData" value:[[MSFSpec object] set:@"dem_encoding" value:@"terrarium"]]]
                 error:nil];
    _contours = [map source:@"contours"
                       spec:[[[MSFSpec of:@"contour"] set:@"source" value:@"dem"] set:@"baseInterval" value:@20]
                      error:nil];

    // Grenoble's Bastille: trails, a cable car, POIs and the old town in one view.
    [map.camera moveTo:[MSFPosition positionWithLng:5.7262 lat:45.1968] zoom:14.5];

    __weak __typeof(self) weakSelf = self;
    for (NSArray<NSString *> *variant in variants()) {
        [host button:variant[1] action:^{
            [style set:@"params.variant" value:variant[0]];
            [imagery set:@"visible" value:@([variant[0] isEqualToString:@"hybrid"])];
            [weakSelf showRelief:[variant[0] isEqualToString:@"outdoor"] || [variant[0] isEqualToString:@"topo"]];
            [host caption:[NSString stringWithFormat:@"Massif %@: %@.", variant[1], variant[2]]];
        }];
    }
    [host caption:@"Massif Streets: the everyday map. Pick another variant."];
}

/** Relief and contours are in every variant's style; the app decides where they draw. */
- (void)showRelief:(BOOL)on {
    if (on) {
        [[_base call:@"addExternalDataSource" args:@[ @"hillshade", @(_dem.handle), @(kSourceHillshade) ] error:nil] destroy];
        [[_base call:@"addExternalDataSource" args:@[ @"contour", @(_contours.handle), @(kSourceVector) ] error:nil] destroy];
    } else {
        [[_base call:@"removeExternalDataSource" args:@[ @"hillshade" ] error:nil] destroy];
        [[_base call:@"removeExternalDataSource" args:@[ @"contour" ] error:nil] destroy];
    }
}

@end
