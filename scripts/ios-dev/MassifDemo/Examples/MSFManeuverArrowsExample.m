#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * Turn arrows cut from a route at each maneuver, the head drawn by the line style itself.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/search/ManeuverArrowsExample.java.
 */
@interface MSFManeuverArrowsExample : NSObject <MSFExample>
@end

@implementation MSFManeuverArrowsExample

+ (NSString *)exampleId {
    return @"maneuver-arrows";
}

/** A tile server wants to know who is asking: a real app identifies itself. */
static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

/** A drive through the Eixample, Barcelona, along its one-way streets. */
static const double kRoute[][2] = {
    { 2.16274, 41.39225 }, { 2.16321, 41.39260 }, { 2.16365, 41.39294 }, { 2.16422, 41.39249 }, { 2.16433, 41.39244 },
    { 2.16476, 41.39210 }, { 2.16493, 41.39196 }, { 2.16522, 41.39168 }, { 2.16583, 41.39119 }, { 2.16597, 41.39130 },
    { 2.16606, 41.39136 }, { 2.16613, 41.39141 }, { 2.16615, 41.39143 }, { 2.16657, 41.39174 }, { 2.16697, 41.39204 },
    { 2.16729, 41.39229 }, { 2.16755, 41.39229 }, { 2.16763, 41.39233 }, { 2.16813, 41.39195 }, { 2.16861, 41.39158 },
    { 2.16940, 41.39098 }, { 2.16971, 41.39075 }, { 2.16987, 41.39063 }, { 2.17061, 41.39117 }, { 2.17077, 41.39135 },
    { 2.17097, 41.39153 }, { 2.17086, 41.39162 }, { 2.17016, 41.39215 },
};
enum { kRouteCount = sizeof(kRoute) / sizeof(kRoute[0]) };

/** Route point index of each maneuver, as a routing engine reports it. */
static const int kManeuverIndex[] = { 2, 8, 17, 22, 25 };
static NSString * const kManeuverText[] = {
    @"Turn right onto Passeig de Gràcia",
    @"Turn left onto Carrer del Consell de Cent",
    @"Turn right onto Carrer de Pau Claris",
    @"Turn left onto Gran Via de les Corts Catalanes",
    @"Turn left onto Carrer de Roger de Llúria",
};
enum { kManeuverCount = sizeof(kManeuverIndex) / sizeof(kManeuverIndex[0]) };

static NSString * const kHeads[] = { @"classic", @"wide", @"long" };
static const int kHeadCount = 3;

static NSString * const kRouteStyle =
    @"#route::case { line-color: #0D47A1; line-width: linear([view::zoom], (12, 3.5), (17, 11)); line-join: round; line-cap: round; }\n"
    @"#route { line-color: #1A73E8; line-width: linear([view::zoom], (12, 2.4), (17, 7.5)); line-join: round; line-cap: round; }";

// Casing first, head over its shaft; the casing's head numbers are smaller because they are read
// against its own wider line (docs/features/maneuver-arrows.md).
static NSString * const kArrowStyle =
    @"#maneuver::case { line-color: #0D47A1; line-width: linear([view::zoom], (12, 3.9), (17, 13)); line-join: round; line-cap: round; }\n"
    @"#maneuver::fill { line-color: #FFFFFF; line-width: linear([view::zoom], (12, 2.4), (17, 8)); line-join: round; line-cap: round; }\n"
    @"#maneuver::headcase {\n"
    @"  line-color: #0D47A1; line-width: linear([view::zoom], (12, 3.9), (17, 13));\n"
    @"  line-end-arrow: true; line-arrow-only: true; line-arrow-width: 2.18; line-arrow-length: 1.72;\n"
    @"  [head='wide'] { line-arrow-width: 2.94; line-arrow-length: 1.38; }\n"
    @"  [head='long'] { line-arrow-width: 1.71; line-arrow-length: 2.51; }\n"
    @"}\n"
    @"#maneuver::head {\n"
    @"  line-color: #FFFFFF; line-width: linear([view::zoom], (12, 2.4), (17, 8));\n"
    @"  line-end-arrow: true; line-arrow-only: true; line-arrow-width: 2.4; line-arrow-length: 1.9;\n"
    @"  [head='wide'] { line-arrow-width: 3.2; line-arrow-length: 1.5; }\n"
    @"  [head='long'] { line-arrow-width: 1.9; line-arrow-length: 2.8; }\n"
    @"}";

static const double kMetresPerDegree = 111319.5;

/** Walks `length` metres from point `index` in `step`'s direction, writing the points it passes. */
static int walk(int index, int step, double length, double out[][2]) {
    double k = cos(kRoute[index][1] * M_PI / 180);
    const double *at = kRoute[index];
    int count = 0;
    for (int i = index + step; i >= 0 && i < kRouteCount && length > 0; i += step) {
        const double *next = kRoute[i];
        double d = hypot((next[0] - at[0]) * k, next[1] - at[1]) * kMetresPerDegree;
        double t = d > length ? length / d : 1;
        out[count][0] = at[0] + (next[0] - at[0]) * t;
        out[count][1] = at[1] + (next[1] - at[1]) * t;
        count++;
        length -= d;
        at = next;
    }
    return count;
}

/**
 * The route from `before` metres behind point `index` to `after` metres past it, clamped at the ends,
 * as GeoJSON coordinates. The facade has no ManeuverArrowBuilder.buildArrow yet: this is its walk.
 */
static NSString *arrowAt(int index, double before, double after) {
    double back[kRouteCount][2], ahead[kRouteCount][2];
    int backCount = walk(index, -1, before, back), aheadCount = walk(index, 1, after, ahead);
    NSMutableString *json = [NSMutableString stringWithString:@"["];
    for (int i = backCount - 1; i >= 0; i--) {
        [json appendFormat:@"[%.6f,%.6f],", back[i][0], back[i][1]];
    }
    [json appendFormat:@"[%.6f,%.6f]", kRoute[index][0], kRoute[index][1]];
    for (int i = 0; i < aheadCount; i++) {
        [json appendFormat:@",[%.6f,%.6f]", ahead[i][0], ahead[i][1]];
    }
    [json appendString:@"]"];
    return json;
}

static NSString *arrows(NSString *head) {
    NSMutableString *json = [NSMutableString stringWithString:@"{\"type\":\"FeatureCollection\",\"features\":["];
    for (int i = 0; i < kManeuverCount; i++) {
        [json appendFormat:@"%@{\"type\":\"Feature\",\"properties\":{\"head\":\"%@\"},"
                           @"\"geometry\":{\"type\":\"LineString\",\"coordinates\":%@}}",
                           i > 0 ? @"," : @"", head, arrowAt(kManeuverIndex[i], 30, 30)];
    }
    [json appendString:@"]}"];
    return json;
}

static NSString *routeGeoJSON(void) {
    NSMutableString *coordinates = [NSMutableString string];
    for (int i = 0; i < kRouteCount; i++) {
        [coordinates appendFormat:@"%@[%.6f,%.6f]", i > 0 ? @"," : @"", kRoute[i][0], kRoute[i][1]];
    }
    return [NSString stringWithFormat:@"{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\","
                                      @"\"properties\":{},\"geometry\":{\"type\":\"LineString\",\"coordinates\":[%@]}}]}",
                                      coordinates];
}

/** Compass bearing of the route as it arrives at point `index`, for a heading-up camera. */
static double bearing(int index) {
    const double *a = kRoute[index - 1], *b = kRoute[index];
    return atan2((b[0] - a[0]) * cos(b[1] * M_PI / 180), b[1] - a[1]) * 180 / M_PI;
}

static void overview(MSFMassifMap *map, id<MSFExampleHost> host, float seconds) {
    [[map.camera animate:seconds] moveTo:[MSFPosition positionWithLng:2.1674 lat:41.3916] zoom:16.1 rotation:0 tilt:80];
    [host caption:@"One arrow per maneuver, cut from the route 30 m either side of the turn."];
}

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

    MSFMassifSource *route = [map source:@"route-data"
                                    spec:[[MSFSpec of:@"geojson"] set:@"maxZoom" value:@18]
                                   error:nil];
    [route setLayerGeoJSON:[route createLayer:@"route"] geoJson:routeGeoJSON()];
    [map addLayer:@"route"
             spec:[[[MSFSpec of:@"vector"]
                     set:@"source" value:@"route-data"]
                     set:@"style" value:[[MSFSpec of:@"mbvt"]
                         set:@"cartocss" value:[[MSFSpec of:@"cartocss"] set:@"css" value:kRouteStyle]]]
            error:nil];

    // A layer of its own, added last: it draws over the route and every layer before it.
    MSFMassifSource *maneuvers = [map source:@"maneuver-data"
                                        spec:[[MSFSpec of:@"geojson"] set:@"maxZoom" value:@18]
                                       error:nil];
    int layer = [maneuvers createLayer:@"maneuver"];
    __block int head = 0;
    [maneuvers setLayerGeoJSON:layer geoJson:arrows(kHeads[head])];
    [map addLayer:@"maneuver"
             spec:[[[MSFSpec of:@"vector"]
                     set:@"source" value:@"maneuver-data"]
                     set:@"style" value:[[MSFSpec of:@"mbvt"]
                         set:@"cartocss" value:[[MSFSpec of:@"cartocss"] set:@"css" value:kArrowStyle]]]
            error:nil];

    overview(map, host, 0);

    __block int step = -1;
    [host button:@"Next maneuver" action:^{
        step = (step + 1) % kManeuverCount;
        int index = kManeuverIndex[step];
        [[map.camera animate:1.5]
            moveTo:[MSFPosition positionWithLng:kRoute[index][0] lat:kRoute[index][1]]
              zoom:17
          rotation:(float)-bearing(index)
              tilt:70];
        [host caption:[NSString stringWithFormat:@"%d/%d: %@.", step + 1, kManeuverCount, kManeuverText[step]]];
    }];
    [host button:@"Head shape" action:^{
        head = (head + 1) % kHeadCount;
        [maneuvers setLayerGeoJSON:layer geoJson:arrows(kHeads[head])];
        [host caption:[NSString stringWithFormat:@"%@ head: line-arrow-width and -length, no marker and no bitmap.",
                                                 kHeads[head]]];
    }];
    [host button:@"Overview" action:^{ overview(map, host, 1.5); }];
}

@end
