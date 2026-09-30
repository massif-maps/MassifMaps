#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * A GeoJSON document served AS vector tiles, so it goes through the ordinary style and renderer.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/sources/GeoJsonLineExample.java.
 */
@interface MSFGeoJsonLineExample : NSObject <MSFExample>
@end

@implementation MSFGeoJsonLineExample

+ (NSString *)exampleId {
    return @"geojson-line";
}

/** A tile server wants to know who is asking: a real app identifies itself. */
static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

/** A stretch of the Tour du Mont Blanc, plus the huts along it. */
static NSString * const kRoute =
    @"{\"type\":\"FeatureCollection\",\"features\":["
    @"{\"type\":\"Feature\",\"properties\":{\"kind\":\"trail\"},\"geometry\":"
    @"{\"type\":\"LineString\",\"coordinates\":["
    @"[6.8694,45.9237],[6.8290,45.9081],[6.8027,45.8862],[6.7861,45.8548],"
    @"[6.8062,45.8281],[6.8556,45.8090],[6.9016,45.7992],[6.9584,45.8138],"
    @"[6.9821,45.8452],[6.9612,45.8813],[6.9163,45.9096],[6.8694,45.9237]]}},"
    @"{\"type\":\"Feature\",\"properties\":{\"kind\":\"hut\",\"name\":\"Lac Blanc\"},"
    @"\"geometry\":{\"type\":\"Point\",\"coordinates\":[6.8290,45.9081]}},"
    @"{\"type\":\"Feature\",\"properties\":{\"kind\":\"hut\",\"name\":\"Bonhomme\"},"
    @"\"geometry\":{\"type\":\"Point\",\"coordinates\":[6.8062,45.8281]}},"
    @"{\"type\":\"Feature\",\"properties\":{\"kind\":\"hut\",\"name\":\"Elisabetta\"},"
    @"\"geometry\":{\"type\":\"Point\",\"coordinates\":[6.9584,45.8138]}}]}";

static NSString * const kStyle =
    @"#tour {\n"
    @"  line-color: #E5484D;\n"
    @"  line-width: linear([view::zoom], (8, 2), (14, 6));\n"
    @"  line-join: round;\n"
    @"  line-cap: round;\n"
    @"}\n"
    @"#tour['kind'='hut'] {\n"
    @"  marker-fill: #FFFFFF;\n"
    @"  marker-line-color: #E5484D;\n"
    @"  marker-line-width: 2;\n"
    @"  marker-width: 9;\n"
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

    // The source re-tiles whatever it is given, so an update is one call rather than a rebuild.
    MSFMassifSource *tour = [map source:@"tour-data"
                                   spec:[[MSFSpec of:@"geojson"] set:@"maxZoom" value:@14]
                                  error:nil];
    int layer = [tour createLayer:@"tour"];
    [tour setLayerGeoJSON:layer geoJson:kRoute];

    [map addLayer:@"tour"
             spec:[[[MSFSpec of:@"vector"]
                     set:@"source" value:@"tour-data"]
                     set:@"style" value:[[MSFSpec of:@"mbvt"]
                         set:@"cartocss" value:[[MSFSpec of:@"cartocss"] set:@"css" value:kStyle]]]
            error:nil];

    [map.camera moveTo:[MSFPosition positionWithLng:6.882 lat:45.866] zoom:10.4];
    [host caption:@"One FeatureCollection, tiled on the fly and styled with CartoCSS."];
}

@end
