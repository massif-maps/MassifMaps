#import "MSFExample.h"
#import "MassifMaps.h"
#import "../CustomLayer/MSFCustomHeatmap.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * A layer the app draws itself, in C: a heatmap laid on the ground, and its colour legend.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/terrain/CustomLayerHeatmapExample.java. The drawing is the same
 * C file (scripts/android-dev/app/src/main/cpp/heatmap_layer.c) registered over the C ABI.
 */
@interface MSFCustomLayerHeatmapExample : NSObject <MSFExample>
@end

@implementation MSFCustomLayerHeatmapExample

+ (NSString *)exampleId {
    return @"custom-layer-heatmap";
}

static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

/** The Chartreuse and Belledonne ridges above Grenoble: relief on three sides of the grid. */
static const double kLongitude = 5.76, kLatitude = 45.24;
static const float kZoom = 12.2f;
/** tilt 90 is straight down in this SDK, so a landscape view is a LOW tilt. */
static const float kTilt3D = 35, kTilt2D = 90;

- (void)startWithHost:(id<MSFExampleHost>)host {
    MSFMassifMap *map = host.map;

    // One DEM behind the mesh, the hillshade and the height read.
    [map source:@"dem"
           spec:[[[[MSFSpec of:@"persistent-cache"]
                set:@"databasePath" value:[host cachePath:@"mapterhorn-dem.db"]]
                set:@"capacity" value:@(200 * 1024 * 1024)]
                set:@"source" value:[[[[[MSFSpec of:@"http"]
                    set:@"url" value:@"https://tiles.mapterhorn.com/{z}/{x}/{y}.webp"]
                    set:@"minZoom" value:@1]
                    set:@"maxZoom" value:@16]
                    set:@"metaData" value:[[MSFSpec object] set:@"dem_encoding" value:@"terrarium"]]]
          error:nil];

    [map addLayer:@"satellite"
             spec:[[MSFSpec of:@"raster"]
                     set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
                         set:@"databasePath" value:[host cachePath:@"world-imagery.db"]]
                         set:@"capacity" value:@(200 * 1024 * 1024)]
                         set:@"source" value:[[[[MSFSpec of:@"http"]
                             set:@"url" value:@"https://server.arcgisonline.com/ArcGIS/rest/services/"
                                               @"World_Imagery/MapServer/tile/{z}/{y}/{x}"]
                             set:@"maxZoom" value:@18]
                             set:@"HTTPHeaders" value:[[MSFSpec object] set:@"User-Agent" value:kUserAgent]]]]
            error:nil];
    [[map addLayer:@"hillshade" spec:[[MSFSpec of:@"hillshade"] set:@"source" value:@"dem"] error:nil] opacity:0.45f];

    MSFPropertyGroup *terrain =
        [map terrainWithSpec:[[MSFSpec of:@"terrain"] set:@"source" value:@"dem"] error:nil];
    [terrain apply:[[[MSFSpec object] set:@"exaggeration" value:@1] set:@"viewDistanceFactor" value:@1.6]];
    [map skyWithSpec:[MSFSpec of:@"sky"] error:nil];
    [map fogWithSpec:[[[MSFSpec of:@"fog"] set:@"rangeStart" value:@2.2] set:@"rangeEnd" value:@8] error:nil];

    [map.camera moveTo:[MSFPosition positionWithLng:kLongitude lat:kLatitude]
                  zoom:kZoom rotation:0 tilt:kTilt3D];

    MSFMassifLayer *heat = [MSFCustomHeatmap createWithId:@"heatmap" scene:YES];
    MSFMassifLayer *legend = [MSFCustomHeatmap createWithId:@"heatmap-legend" scene:NO];
    if (!heat || !legend) {
        [host caption:@"The custom layer did not register."];
        return;
    }
    [map add:heat];
    [map add:legend];

    // The layer draws what it is given: the ground height under each cell comes from the DEM,
    // through the hillshade layer, which reads it. Blocks on tile loading, so off the main queue.
    __block NSData *ground = nil;
    NSData *positions = [MSFCustomHeatmap gridPositionsForLongitude:kLongitude latitude:kLatitude];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        const double *p = positions.bytes;
        NSMutableArray *list = [NSMutableArray arrayWithCapacity:positions.length / (2 * sizeof(double))];
        for (NSUInteger k = 0; k < positions.length / (2 * sizeof(double)); k++) {
            [list addObject:@[ @(p[2 * k]), @(p[2 * k + 1]) ]];
        }
        MSFMassifObject *result = [[map layer:@"hillshade"] call:@"getElevations" args:@[ list ] error:nil];
        NSData *heights = [result.doubles copy];
        [result destroy];
        dispatch_async(dispatch_get_main_queue(), ^{
            ground = heights;
            [MSFCustomHeatmap setHeatmapAtLongitude:kLongitude latitude:kLatitude heights:ground];
        });
    });

    [host toggle:@"3D" on:YES action:^(BOOL on) {
        // Flat ground has no heights to lay the grid on, so it goes flat with it.
        [MSFCustomHeatmap setHeatmapAtLongitude:kLongitude latitude:kLatitude heights:on ? ground : nil];
        [map.terrain set:@"flattened" value:@(!on)];
        [[map.camera animate:1.2f] tilt:on ? kTilt3D : kTilt2D];
    }];
    [host toggle:@"Drape" on:YES action:^(BOOL on) {
        [MSFCustomHeatmap setDrape:on];
    }];
    [host toggle:@"Heatmap" on:YES action:^(BOOL on) {
        [heat visible:on];
        [legend visible:on];
    }];
    [host slider:@"Opacity" min:0 max:1 value:1 action:^(float value) {
        [heat opacity:value];
    }];
    [host caption:@"Tilt the map, or switch 3D off. Drape off shows the ground cutting through the heatmap. Tap it to dim it."];
}

@end
