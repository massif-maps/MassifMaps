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

/** A drive round Annecy station, as OSRM routes it: roundabouts, a U-turn, both turns. */
static const double kRoute[][2] = {
    { 6.121812, 45.901688 }, { 6.121752, 45.901660 }, { 6.121601, 45.901624 }, { 6.120724, 45.901472 }, { 6.119987, 45.901336 },
    { 6.119685, 45.901347 }, { 6.119667, 45.901362 }, { 6.119623, 45.901381 }, { 6.119589, 45.901386 }, { 6.119537, 45.901382 },
    { 6.119480, 45.901356 }, { 6.119457, 45.901330 }, { 6.119447, 45.901301 }, { 6.119213, 45.901192 }, { 6.118270, 45.901021 },
    { 6.118021, 45.900993 }, { 6.118270, 45.901021 }, { 6.119213, 45.901192 }, { 6.119505, 45.901219 }, { 6.119555, 45.901205 },
    { 6.119609, 45.901206 }, { 6.119664, 45.901077 }, { 6.119682, 45.901049 }, { 6.119713, 45.901022 }, { 6.119974, 45.900895 },
    { 6.120225, 45.900750 }, { 6.120287, 45.900686 }, { 6.120363, 45.900643 }, { 6.120416, 45.900599 }, { 6.120501, 45.900591 },
    { 6.120565, 45.900594 }, { 6.121097, 45.900714 }, { 6.121143, 45.900719 }, { 6.121209, 45.900716 }, { 6.121222, 45.900697 },
    { 6.121259, 45.900678 }, { 6.121321, 45.900679 }, { 6.121345, 45.900689 }, { 6.121366, 45.900709 }, { 6.121374, 45.900734 },
    { 6.121366, 45.900758 }, { 6.121409, 45.900806 }, { 6.121458, 45.900838 }, { 6.121733, 45.900903 }, { 6.121889, 45.900912 },
    { 6.121956, 45.900900 }, { 6.122027, 45.900879 }, { 6.122141, 45.900825 }, { 6.122171, 45.900807 }, { 6.122211, 45.900767 },
    { 6.122356, 45.900473 }, { 6.122629, 45.899880 }, { 6.122331, 45.899798 }, { 6.121724, 45.899628 }, { 6.121764, 45.899520 },
    { 6.122008, 45.899000 }, { 6.122070, 45.898810 }, { 6.121217, 45.898719 },
};
enum { kRouteCount = sizeof(kRoute) / sizeof(kRoute[0]) };

/**
 * Route point index of each maneuver as a routing engine reports it, the metres of route kept
 * before and after it, and a sideways shift in metres for a lane change (0 = follow the route).
 */
typedef struct { int index; double before, after, shift; } MSFManeuver;
static const MSFManeuver kManeuvers[] = {
    { 3, 30, 35, 3.5 }, { 5, 20, 45, 0 }, { 15, 30, 30, 0 }, { 18, 30, 30, 0 },
    { 33, 25, 45, 0 }, { 51, 30, 30, 0 }, { 53, 30, 30, 0 }, { 56, 30, 30, 0 },
};
static NSString * const kManeuverText[] = {
    @"Move to the left lane on Rue de l'Industrie",
    @"At the roundabout, take the exit onto Avenue de Chevêne",
    @"Make a U-turn on Avenue de Chevêne",
    @"At the small roundabout, keep right on Avenue de Chevêne",
    @"At the roundabout, take the exit onto Rue Vaugelas",
    @"Turn right onto Rue Royale",
    @"Turn left onto Rue de la Gare",
    @"Turn right: you have arrived",
};
enum { kManeuverCount = sizeof(kManeuvers) / sizeof(kManeuvers[0]) };

static const double kMetresPerDegree = 111319.5;

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

/** Ahead of point `index` along its segment, moving `shift` metres left over the first 40%. */
static NSArray *laneChange(int index, double after, double shift) {
    const double *at = kRoute[index], *next = kRoute[index + 1];
    double k = cos(at[1] * M_PI / 180);
    double dx = (next[0] - at[0]) * k, dy = next[1] - at[1], d = hypot(dx, dy);
    double left = shift / kMetresPerDegree;
    NSMutableArray *out = [NSMutableArray arrayWithCapacity:2];
    for (int i = 0; i < 2; i++) {
        double along = (i == 0 ? 0.4 : 1.0) * after / kMetresPerDegree;
        [out addObject:@[ @(at[0] + (dx * along - dy * left) / d / k), @(at[1] + (dy * along + dx * left) / d) ]];
    }
    return out;
}

static NSString *arrows(MSFMassifObject *builder, NSString *head) {
    NSMutableArray *route = [NSMutableArray arrayWithCapacity:kRouteCount];
    for (int i = 0; i < kRouteCount; i++) {
        [route addObject:@[ @(kRoute[i][0]), @(kRoute[i][1]) ]];
    }
    NSMutableArray *features = [NSMutableArray array];
    for (int i = 0; i < kManeuverCount; i++) {
        MSFManeuver maneuver = kManeuvers[i];
        // A lane change leaves the route: the builder walks the part behind, the shift is drawn ahead.
        [builder set:@"lengthBefore" value:@(maneuver.before)];
        [builder set:@"lengthAfter" value:@(maneuver.shift != 0 ? 0 : maneuver.after)];
        MSFMassifObject *result = [builder call:@"buildArrowAtIndex" args:@[ route, @(maneuver.index) ] error:nil];
        NSDictionary *arrow = [NSJSONSerialization JSONObjectWithData:[result.json dataUsingEncoding:NSUTF8StringEncoding]
                                                              options:0
                                                                error:nil];
        [result destroy];
        for (NSDictionary *feature in arrow[@"features"]) {
            NSMutableDictionary *withHead = [feature mutableCopy];
            withHead[@"properties"] = @{ @"head" : head };
            if (maneuver.shift != 0) {
                NSMutableArray *coordinates = [feature[@"geometry"][@"coordinates"] mutableCopy];
                NSArray *last = coordinates.lastObject;
                if (fabs([last[0] doubleValue] - kRoute[maneuver.index][0]) + fabs([last[1] doubleValue] - kRoute[maneuver.index][1]) < 1e-9) {
                    [coordinates removeLastObject];
                }
                [coordinates addObjectsFromArray:laneChange(maneuver.index, maneuver.after, maneuver.shift)];
                withHead[@"geometry"] = @{ @"type" : @"LineString", @"coordinates" : coordinates };
            }
            [features addObject:withHead];
        }
    }
    NSData *json = [NSJSONSerialization dataWithJSONObject:@{ @"type" : @"FeatureCollection", @"features" : features }
                                                   options:0
                                                     error:nil];
    return [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding];
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
    [[map.camera animate:seconds] moveTo:[MSFPosition positionWithLng:6.1203 lat:45.9002] zoom:16.3 rotation:0 tilt:80];
    [host caption:@"One arrow per maneuver, cut from the route either side of it."];
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
    MSFMassifObject *builder = [map object:@"geometry"
                                  objectId:@"maneuver-arrows"
                                      spec:[MSFSpec of:@"maneuver-arrow"]
                                     error:nil];
    __block int head = 0;
    [maneuvers setLayerGeoJSON:layer geoJson:arrows(builder, kHeads[head])];
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
        int index = kManeuvers[step].index;
        [[map.camera animate:1.5]
            moveTo:[MSFPosition positionWithLng:kRoute[index][0] lat:kRoute[index][1]]
              zoom:17
          rotation:(float)-bearing(index)
              tilt:70];
        [host caption:[NSString stringWithFormat:@"%d/%d: %@.", step + 1, kManeuverCount, kManeuverText[step]]];
    }];
    [host button:@"Head shape" action:^{
        head = (head + 1) % kHeadCount;
        [maneuvers setLayerGeoJSON:layer geoJson:arrows(builder, kHeads[head])];
        [host caption:[NSString stringWithFormat:@"%@ head: line-arrow-width and -length, no marker and no bitmap.",
                                                 kHeads[head]]];
    }];
    [host button:@"Overview" action:^{ overview(map, host, 1.5); }];
}

@end
