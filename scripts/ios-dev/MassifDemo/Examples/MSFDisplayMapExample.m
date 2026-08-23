#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"
#import "api/MassifApiNames.h"

/**
 * The smallest thing that is a map: one raster layer and a camera.
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

/** OSM's tile usage policy: a real app identifies itself, or the tiles come back as 403s. */
static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

- (void)startWithHost:(id<MSFExampleHost>)host {
    MSFMassifMap *map = host.map;

    // A spec describes the whole stack: the layer, and the source underneath it. Anything the
    // constructor does not take is applied as a property, so "opacity" needs no special case.
    [map addLayer:@"basemap"
             spec:[[MSFSpec of:@"raster"]
                     set:@"source" value:[[[[MSFSpec of:@"http"]
                         set:@"url" value:@"https://tile.openstreetmap.org/{z}/{x}/{y}.png"]
                         set:@"maxZoom" value:@19]
                         // OSM's tile policy REQUIRES an identifying User-Agent; without one the
                         // server answers 403 and every tile comes back as an error image.
                         set:@"HTTPHeaders" value:[[MSFSpec object] set:@"User-Agent" value:kUserAgent]]]
            error:nil];

    // The same property two ways. The string is the API; MassifProperty is the GENERATED typed
    // enum, which completes in Xcode - and in Swift reads as `.opacity`.
    [[map layer:@"basemap"] set:@"opacity" value:@1.0];
    [[map layer:@"basemap"] set:MassifPropertyOpacity value:@1.0];

    // Positions are lon/lat: the map view was set up with EPSG:4326 as its base projection.
    [map.camera moveTo:[MSFPosition positionWithLng:6.8652 lat:45.8326] zoom:11];

    [host caption:@"Mont Blanc, from OpenStreetMap raster tiles."];
}

@end
