#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifElements.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * Searching the vector tiles the map is already showing, and pinning what comes back.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/search/SearchFeaturesExample.java.
 */
@interface MSFSearchFeaturesExample : NSObject <MSFExample>
@end

@implementation MSFSearchFeaturesExample {
    /** The style every result marker shares. */
    MSFMassifObject *_hitStyle;
}

+ (NSString *)exampleId {
    return @"search-features";
}

static const double kCentreLon = 5.7245;
static const double kCentreLat = 45.1885;
/** Degrees around the centre. A search with NO geometry scans the whole world at its zoom. */
static const double kSpan = 0.08;

static NSArray *corner(double dLon, double dLat) {
    return @[ @(kCentreLon + dLon), @(kCentreLat + dLat) ];
}

- (void)startWithHost:(id<MSFExampleHost>)host {
    MSFMassifMap *map = host.map;

    [map style:@"alpine"
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

    [map.camera moveTo:[MSFPosition positionWithLng:kCentreLon lat:kCentreLat] zoom:13.5];

    // The service is built FROM THE LAYER: it takes the source and the decoder the layer is already
    // showing, so there is nothing to keep in step.
    MSFMassifObject *service = [map object:@"search"
                                  objectId:@"poi"
                                      spec:[[MSFSpec of:@"vectortile"] set:@"layer" value:@"basemap"]
                                     error:nil];
    [service set:@"minZoom" value:@14];
    [service set:@"maxZoom" value:@14];
    [service set:@"maxResults" value:@12];
    // A struct property: a JSON array, not a call that had to be added for it.
    [service set:@"layers" value:@[ @"place", @"poi", @"mountain_peak" ]];

    _hitStyle = [map.elements style:@"hit"
                               spec:[[[MSFSpec of:@"marker"] set:@"size" value:@20]
                                       set:@"color" value:@0xFF3A6EA5]
                              error:nil];

    // The filter is an ECMAScript regex, which has no inline flags - "(?i)" is a parse error, not a
    // case-insensitive match, and the search comes back as a failure.
    [host button:@"Search \"gare\"" action:^{
        [self search:host map:map service:service regex:@".*[Gg]are.*"];
    }];
    [host button:@"Search \"parc\"" action:^{
        [self search:host map:map service:service regex:@".*[Pp]arc.*"];
    }];
    [host caption:@"Grenoble. Tap a search - results are pinned as markers."];
}

- (void)search:(id<MSFExampleHost>)host
           map:(MSFMassifMap *)map
       service:(MSFMassifObject *)service
         regex:(NSString *)regex {
    [map.elements clear];
    [host caption:@"Searching..."];

    MSFMassifObject *request =
        [map object:@"search"
           objectId:[NSString stringWithFormat:@"request-%lu", (unsigned long)regex.hash]
               spec:[MSFSpec of:@"request"]
              error:nil];
    [request set:@"regexFilter" value:regex];
    // Bound it, or the search walks every tile in the world at zoom 14.
    [request set:@"geometry" value:[map object:@"geometry" objectId:@"area"
                                          spec:[[MSFSpec of:@"geojson"]
                                                  set:@"geojson" value:[[[MSFSpec object]
                                                      set:@"type" value:@"Polygon"]
                                                      set:@"coordinates" value:@[ @[
                                                          corner(-kSpan, -kSpan),
                                                          corner(kSpan, -kSpan),
                                                          corner(kSpan, kSpan),
                                                          corner(-kSpan, kSpan),
                                                          corner(-kSpan, -kSpan) ] ]]]
                                         error:nil]];
    [request set:@"projection" value:[map object:@"projection" objectId:@"wgs84"
                                            spec:[MSFSpec of:@"EPSG:4326"] error:nil]];

    // ASYNC: findFeatures fetches and decodes every tile in range - on the caller's thread that is
    // a frozen UI, measured at zoom 14 over this camera.
    [service callAsync:@"findFeatures"
                  args:@[ @(request.handle) ]
            completion:^(MSFMassifObject *found) {
        if (!found) {
            [host caption:@"Search failed."];
            return;
        }
        long long count = [found getLong:@"featureCount" defaultValue:0];
        for (long long i = 0; i < count; i++) {
            MSFMassifObject *feature = [found call:@"getFeature" args:@[ @(i) ] error:nil];
            MSFPosition *at = [feature getPos:@"geometry.centerPos" projection:@"EPSG:4326"];
            if (at) {
                [map addMarker:[[[MSFSpec of:@"marker"]
                                   set:@"position" value:@[ @(at.lng), @(at.lat) ]]
                                   set:@"style" value:self->_hitStyle.objectId]
                         error:nil];
            }
            [feature destroy];
        }
        [host caption:[NSString stringWithFormat:@"%lld result%@.", count, count == 1 ? @"" : @"s"]];
    }];
}

@end
