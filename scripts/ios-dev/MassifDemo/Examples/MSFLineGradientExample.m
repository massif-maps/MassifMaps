#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * A line coloured along its length: the geojson source tags every tile piece of a line with the part
 * of the route it covers, and the style turns that into a colour ramp.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/sources/LineGradientExample.java.
 */
@interface MSFLineGradientExample : NSObject <MSFExample>
@end

@implementation MSFLineGradientExample

+ (NSString *)exampleId {
    return @"line-gradient";
}

/** A tile server wants to know who is asking: a real app identifies itself. */
static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

/** A stretch of the Tour du Mont Blanc, coloured smoothly, and a spur coloured in steps. */
static NSString * const kRoute =
    @"{\"type\":\"FeatureCollection\",\"features\":["
    @"{\"type\":\"Feature\",\"properties\":{\"gradient\":\"0:#2ecc71,0.5:#f1c40f,1:#e74c3c\"},\"geometry\":"
    @"{\"type\":\"LineString\",\"coordinates\":["
    @"[6.8694,45.9237],[6.8290,45.9081],[6.8027,45.8862],[6.7861,45.8548],"
    @"[6.8062,45.8281],[6.8556,45.8090],[6.9016,45.7992],[6.9584,45.8138],"
    @"[6.9821,45.8452],[6.9612,45.8813],[6.9163,45.9096],[6.8694,45.9237]]}},"
    @"{\"type\":\"Feature\",\"properties\":{\"gradient\":\"step;0:#3b82f6,0.33:#8b5cf6,0.66:#ec4899\"},\"geometry\":"
    @"{\"type\":\"LineString\",\"coordinates\":["
    @"[6.7400,45.9300],[6.7800,45.9500],[6.8300,45.9600],[6.8800,45.9650]]}}]}";

static NSString * const kStyle =
    @"#tour {\n"
    @"  line-gradient: [gradient];\n"
    @"  line-width: linear([view::zoom], (8, 3), (14, 8));\n"
    @"  line-join: round;\n"
    @"  line-cap: round;\n"
    @"}";

- (void)startWithHost:(id<MSFExampleHost>)host {
    MSFMassifMap *map = host.map;

    // The Massif streets style (styles/massif/carto, bundled by the build as styles/massif.zip) over
    // OpenFreeMap's vector tiles, cached on disk: a free service a demo would re-fetch every run.
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

    // lineMetrics is what lets the style know where along a route each tile piece sits.
    MSFMassifSource *tour = [map source:@"tour-data"
                                   spec:[[[MSFSpec of:@"geojson"] set:@"maxZoom" value:@14] set:@"lineMetrics" value:@YES]
                                  error:nil];
    int layer = [tour createLayer:@"tour"];
    [tour setLayerGeoJSON:layer geoJson:kRoute];

    [map addLayer:@"tour"
             spec:[[[MSFSpec of:@"vector"]
                     set:@"source" value:@"tour-data"]
                     set:@"style" value:[[MSFSpec of:@"mbvt"]
                         set:@"cartocss" value:[[MSFSpec of:@"cartocss"] set:@"css" value:kStyle]]]
            error:nil];

    [map.camera moveTo:[MSFPosition positionWithLng:6.862 lat:45.886] zoom:10.2];
    [host caption:@"A ramp along each route, smooth or in steps, from a string on the feature."];
}

@end
