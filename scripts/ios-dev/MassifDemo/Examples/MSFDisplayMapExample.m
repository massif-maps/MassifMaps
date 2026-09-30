#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"
#import "api/MassifApiNames.h"

/**
 * The smallest thing that is a map: one layer with the Massif style, and a camera.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/basics/DisplayMapExample.java.
 */
@interface MSFDisplayMapExample : NSObject <MSFExample>
@end

@implementation MSFDisplayMapExample

+ (NSString *)exampleId {
    return @"display-a-map";
}

/** A tile server wants to know who is asking: a real app identifies itself. */
static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

- (void)startWithHost:(id<MSFExampleHost>)host {
    MSFMassifMap *map = host.map;

    // A spec describes the whole stack: the layer, the source under it and the style over it - the
    // Massif streets style, bundled by the build as styles/massif.zip from styles/massif/carto.
    [map addLayer:@"basemap"
             spec:[[[MSFSpec of:@"vector"]
                     set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
                         set:@"databasePath" value:[host cachePath:@"openfreemap.db"]]
                         set:@"capacity" value:@(100 * 1024 * 1024)]
                         set:@"source" value:[[[[MSFSpec of:@"http"]
                             set:@"url" value:@"https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf"]
                             set:@"maxZoom" value:@14]
                             set:@"HTTPHeaders" value:[[MSFSpec object] set:@"User-Agent" value:kUserAgent]]]]
                     set:@"style" value:[[MSFSpec of:@"mbvt"] set:@"project" value:[[[MSFSpec of:@"project"]
                         set:@"assets" value:[[MSFSpec of:@"zip"] set:@"data" value:[[MSFSpec of:@"url"]
                             set:@"url" value:@"assets://styles/massif.zip"]]]
                         set:@"name" value:@"streets"]]]
            error:nil];

    // The same property two ways. The string is the API; MassifProperty is the GENERATED typed
    // enum, which completes in Xcode - and in Swift reads as `.opacity`.
    [[map layer:@"basemap"] set:@"opacity" value:@1.0];
    [[map layer:@"basemap"] set:MassifPropertyOpacity value:@1.0];

    // Positions are lon/lat: the map view was set up with EPSG:4326 as its base projection.
    [map.camera moveTo:[MSFPosition positionWithLng:6.8652 lat:45.8326] zoom:11];

    [host caption:@"Mont Blanc, drawn by the Massif streets style over OpenFreeMap vector tiles."];
}

@end
