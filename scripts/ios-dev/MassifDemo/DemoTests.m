#import "DemoTests.h"
#import "DemoConfig.h"
#import "DemoMap.h"
#import "DemoStyles.h"
#import "DemoToast.h"
#import "MSFGeoJSONVectorTileDataSource.h"
#import "MSFManeuverArrowBuilder.h"
#import "MSFValhallaOnlineRoutingService.h"
#import "MSFVectorTileSearchService.h"

/** Cell spacing of the gallery grid, and the length of each leg, in metres. */
static const double GALLERY_SPACING = 300;
static const float GALLERY_LEG = 60;

@implementation DemoTests

/** Layers these actions add, so 'clear' can drop them without touching the base map. */
static NSMutableArray<MSFLayer *> *sTestLayers = nil;
/** Vector elements the routing tests draw, on one source that survives between runs. */
static MSFLocalVectorDataSource *sResults = nil;

+ (void)run:(NSString *)action demo:(DemoMap *)demo {
    if (!demo.mapView) {
        return;
    }
    if (!sTestLayers) {
        sTestLayers = [NSMutableArray array];
    }
    if ([action isEqualToString:@"maneuverHead"]) {
        [DemoToast show:[NSString stringWithFormat:@"maneuver head: %@", [demo cycleManeuverHead]]];
    } else if ([action isEqualToString:@"maneuverGallery"]) {
        [DemoToast show:[NSString stringWithFormat:@"%d gallery arrows", [self seedManeuverArrows:demo]]];
    } else if ([action isEqualToString:@"onlineRouting"]) {
        [self runOnlineRouting:demo];
    } else if ([action isEqualToString:@"search"]) {
        [self runVectorTileSearch:demo];
    } else if ([action isEqualToString:@"geojsonLine"]) {
        [self addGeoJSONLine:demo];
    } else if ([action isEqualToString:@"geojsonBench"]) {
        [self runGeoJSONBench:demo];
    } else if ([action isEqualToString:@"popupFonts"]) {
        [self runPopupFonts:demo];
    } else if ([action isEqualToString:@"clear"]) {
        [self clear:demo];
    }
}

+ (void)clear:(DemoMap *)demo {
    MSFLayers *layers = [demo.mapView getLayers];
    for (MSFLayer *layer in sTestLayers) {
        [layers remove:layer];
    }
    [sTestLayers removeAllObjects];
    [sResults clear];
    [demo clearManeuverArrows];
}

/** The vector layer the routing tests draw into, created on first use. */
+ (MSFLocalVectorDataSource *)results:(DemoMap *)demo {
    if (!sResults) {
        MSFProjection *projection = [[demo.mapView getOptions] getBaseProjection];
        sResults = [[MSFLocalVectorDataSource alloc] initWithProjection:projection];
        MSFVectorLayer *layer = [[MSFVectorLayer alloc] initWithDataSource:sResults];
        [[demo.mapView getLayers] add:layer];
        [sTestLayers addObject:layer];
    }
    return sResults;
}

// =================================================================================================
// ROUTE TEST LAYER
// =================================================================================================

/**
 * The LINE JOIN bench: a route served as GeoJSON vector tiles and styled with CartoCSS, so it takes
 * the SAME tesselator, shaders and drape path as the base map's roads - a Line vector element would
 * not (LineDrawData is a second, independent tesselator).
 *
 * Casing + fill, as a navigation app draws a route. What to look at, zooming OUT: the outside of a
 * sharp turn (miter needles), the inside of a turn with line-opacity below 1 (the join blends twice
 * where the triangles overlap) and the switchback ends (cap / split joins).
 */
+ (MSFLayer *)createRouteTestLayer:(DemoMap *)demo {
    NSString *geoJSON = [self routeTestGeoJSON:demo];
    MSFMBVectorTileDecoder *decoder = [[MSFMBVectorTileDecoder alloc] initWithCartoCSSStyleSet:
        [[MSFCartoCSSStyleSet alloc] initWithCartoCSS:[DemoStyles routeTestStyle]]];
    MSFGeoJSONVectorTileDataSource *source =
        [[MSFGeoJSONVectorTileDataSource alloc] initWithMinZoom:0 maxZoom:24];
    @try {
        int layerIndex = [source createLayer:@"route"];
        [source setLayerGeoJSONString:layerIndex geoJSON:geoJSON];
    } @catch (NSException *exception) {
        NSLog(@"MassifDemo: route test geojson rejected: %@", exception.reason);
        return nil;
    }
    return [[MSFVectorTileLayer alloc] initWithDataSource:source decoder:decoder];
}

/**
 * A switchback road through the current view. Sharp corners, near-reversals and a hairpin are what
 * a join has to survive; a gentle line proves nothing.
 */
+ (NSString *)routeTestGeoJSON:(DemoMap *)demo {
    double lon = [DemoConfig doubleFor:@"lon"], lat = [DemoConfig doubleFor:@"lat"];
    NSMutableArray<NSString *> *points = [NSMutableArray array];
    for (int i = 0; i < 24; i++) {
        // A zig-zag that tightens as it climbs: the last turns are near-reversals.
        double amplitude = 0.010 - i * 0.00035;
        double dx = ((i % 2) ? amplitude : -amplitude);
        double dy = (i - 12) * 0.0009;
        [points addObject:[NSString stringWithFormat:@"[%f,%f]", lon + dx, lat + dy]];
    }
    return [NSString stringWithFormat:
        @"{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\",\"properties\":{},"
        @"\"geometry\":{\"type\":\"LineString\",\"coordinates\":[%@]}}]}",
        [points componentsJoinedByString:@","]];
}

// =================================================================================================
// MANEUVER ARROWS
// =================================================================================================

/** Offsets a WGS84 position by metres east / north - exact enough over the few hundred here. */
+ (MSFMapPos *)offsetLon:(double)lon lat:(double)lat east:(double)east north:(double)north {
    double metresPerDegLat = 111320.0;
    double metresPerDegLon = metresPerDegLat * cos(lat * M_PI / 180.0);
    return [[MSFMapPos alloc] initWithX:lon + east / metresPerDegLon y:lat + north / metresPerDegLat];
}

+ (int)addGalleryArrow:(DemoMap *)demo arrowId:(int)arrowId lon:(double)lon lat:(double)lat
                   col:(int)col row:(int)row points:(NSArray<NSValue *> *)points
         maneuverIndex:(int)maneuverIndex before:(float)before after:(float)after {
    MSFManeuverArrowBuilder *builder = [self builderFor:demo before:before after:after];
    double east = (col - 1) * GALLERY_SPACING;
    double north = (row - 0.5) * GALLERY_SPACING;
    MSFMapPosVector *poses = [[MSFMapPosVector alloc] init];
    for (NSValue *value in points) {
        CGPoint point = value.CGPointValue;
        [poses add:[self offsetLon:lon lat:lat east:east + point.x north:north + point.y]];
    }
    // The shape is WGS84, hence the nil projection.
    [demo setManeuverArrow:arrowId
                     arrow:[builder buildArrowAtIndex:nil points:poses maneuverIndex:maneuverIndex]];
    return 1;
}

+ (MSFManeuverArrowBuilder *)builderFor:(DemoMap *)demo before:(float)before after:(float)after {
    // The builder lives on the demo so the panel's before/after knobs reach it; the gallery only
    // borrows it for the length of one arrow.
    MSFManeuverArrowBuilder *builder = [[MSFManeuverArrowBuilder alloc] init];
    [builder setLengthBefore:before];
    [builder setLengthAfter:after];
    return builder;
}

+ (NSArray<NSValue *> *)pointsFrom:(const CGPoint *)points count:(int)count {
    NSMutableArray<NSValue *> *result = [NSMutableArray array];
    for (int i = 0; i < count; i++) {
        [result addObject:[NSValue valueWithCGPoint:points[i]]];
    }
    return result;
}

/**
 * A roundabout: straight approach from the west, then the ring, then the exit north. The maneuver
 * point is the entry, which is what a routing engine reports.
 */
+ (NSArray<NSValue *> *)roundabout:(double)radius exitQuarters:(int)exitQuarters step:(int)stepDegrees {
    NSMutableArray<NSValue *> *points = [NSMutableArray array];
    [points addObject:[NSValue valueWithCGPoint:CGPointMake(-radius - 90, -radius)]];
    [points addObject:[NSValue valueWithCGPoint:CGPointMake(-radius, -radius)]]; // entry: the maneuver
    for (int angle = 180; angle >= 180 - 90 * exitQuarters; angle -= stepDegrees) {
        double a = angle * M_PI / 180.0;
        [points addObject:[NSValue valueWithCGPoint:CGPointMake(cos(a) * radius, sin(a) * radius - radius)]];
    }
    double a = (180 - 90 * exitQuarters) * M_PI / 180.0;
    [points addObject:[NSValue valueWithCGPoint:
        CGPointMake(cos(a) * radius, sin(a) * radius - radius - 90)]];
    return points;
}

/**
 * Each entry is a synthetic route in METRES around its own cell, fed through the real builder with
 * a maneuver index - so the slicing, not just the drawing, is what is on screen. The roundabout
 * gets longer lengths, because what a driver needs to see there is the whole arc.
 */
+ (int)seedManeuverArrows:(DemoMap *)demo {
    double lon = [DemoConfig doubleFor:@"lon"], lat = [DemoConfig doubleFor:@"lat"];
    // Longer than the 30 m default: these are drawn side by side to be READ, and a shaft only as
    // long as the head is, is a triangle with a stub, not a maneuver.
    float leg = GALLERY_LEG;
    int count = 0;
    @try {
        const CGPoint right90[] = { {-90, 0}, {0, 0}, {0, -90} };
        count += [self addGalleryArrow:demo arrowId:count lon:lon lat:lat col:0 row:1
                                points:[self pointsFrom:right90 count:3]
                         maneuverIndex:1 before:leg after:leg];
        const CGPoint left90[] = { {-90, 0}, {0, 0}, {0, 90} };
        count += [self addGalleryArrow:demo arrowId:count lon:lon lat:lat col:1 row:1
                                points:[self pointsFrom:left90 count:3]
                         maneuverIndex:1 before:leg after:leg];
        const CGPoint slightRight[] = { {-90, 0}, {0, 0}, {65, -65} };
        count += [self addGalleryArrow:demo arrowId:count lon:lon lat:lat col:2 row:1
                                points:[self pointsFrom:slightRight count:3]
                         maneuverIndex:1 before:leg after:leg];
        const CGPoint sharpRight[] = { {-90, 0}, {0, 0}, {-65, -65} };
        count += [self addGalleryArrow:demo arrowId:count lon:lon lat:lat col:0 row:0
                                points:[self pointsFrom:sharpRight count:3]
                         maneuverIndex:1 before:leg after:leg];
        const CGPoint uTurn[] = { {-90, 22}, {0, 22}, {20, 11}, {20, -11}, {0, -22}, {-90, -22} };
        count += [self addGalleryArrow:demo arrowId:count lon:lon lat:lat col:1 row:0
                                points:[self pointsFrom:uTurn count:6]
                         maneuverIndex:1 before:leg after:leg];
        count += [self addGalleryArrow:demo arrowId:count lon:lon lat:lat col:2 row:0
                                points:[self roundabout:45 exitQuarters:3 step:10]
                         maneuverIndex:2 before:40 after:260];
    } @catch (NSException *exception) {
        NSLog(@"MassifDemo: maneuver gallery failed: %@", exception.reason);
    }
    NSLog(@"MassifDemo: seeded %d gallery maneuver arrows around %f, %f", count, lat, lon);
    return count;
}

// =================================================================================================
// ROUTE FROM A FILE
// =================================================================================================

+ (int)drawRouteFile:(NSString *)path demo:(DemoMap *)demo {
    NSData *data = [NSData dataWithContentsOfFile:path];
    NSDictionary *json = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    NSArray<NSArray<NSNumber *> *> *points = json[@"points"];
    if (points.count < 3) {
        NSLog(@"MassifDemo: -routeFile %@ unreadable", path);
        return 0;
    }
    MSFMapPosVector *wgs = [[MSFMapPosVector alloc] init];
    NSMutableArray<NSArray<NSNumber *> *> *coordinates = [NSMutableArray array];
    for (NSArray<NSNumber *> *point in points) {
        [wgs add:[[MSFMapPos alloc] initWithX:point[0].doubleValue y:point[1].doubleValue]];
        [coordinates addObject:@[point[0], point[1]]];
    }
    // the navigation layer of the app's inner style: class=route
    NSDictionary *geoJSON = @{ @"type": @"FeatureCollection", @"features": @[ @{
        @"type": @"Feature",
        @"properties": @{ @"class": @"route" },
        @"geometry": @{ @"type": @"LineString", @"coordinates": coordinates } } ] };
    NSData *geoJSONData = [NSJSONSerialization dataWithJSONObject:geoJSON options:0 error:nil];
    MSFGeoJSONVectorTileDataSource *source = [demo maneuverSource];
    int navigationLayer = [source createLayer:@"navigation"];
    [source setLayerGeoJSONString:navigationLayer
                          geoJSON:[[NSString alloc] initWithData:geoJSONData encoding:NSUTF8StringEncoding]];

    MSFManeuverArrowBuilder *builder = [self builderFor:demo
                                                before:[DemoConfig floatFor:@"maneuverBefore"]
                                                 after:[DemoConfig floatFor:@"maneuverAfter"]];
    int arrows = 0;
    for (int index = 1; index + 1 < (int)points.count; index++) {
        double before = atan2(points[index][1].doubleValue - points[index - 1][1].doubleValue,
                              (points[index][0].doubleValue - points[index - 1][0].doubleValue) * cos(points[index][1].doubleValue * M_PI / 180));
        double after = atan2(points[index + 1][1].doubleValue - points[index][1].doubleValue,
                             (points[index + 1][0].doubleValue - points[index][0].doubleValue) * cos(points[index][1].doubleValue * M_PI / 180));
        double turn = fabs(fmod((after - before) * 180 / M_PI + 540, 360) - 180);
        if (turn < 35) {
            continue;
        }
        [demo setManeuverArrow:arrows++ arrow:[builder buildArrowAtIndex:nil points:wgs maneuverIndex:index]];
    }
    NSLog(@"MassifDemo: route %@ drawn, %d arrows", path.lastPathComponent, arrows);
    return arrows;
}

// =================================================================================================
// ROUTING
// =================================================================================================

/**
 * Online Valhalla routing. The result carries the decoded geometry and the instructions, so this
 * draws the line AND puts a maneuver arrow on every real turn - the same arrows the gallery above
 * shows in isolation, on a route that actually exists.
 */
+ (void)runOnlineRouting:(DemoMap *)demo {
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @try {
            MSFValhallaOnlineRoutingService *service = [[MSFValhallaOnlineRoutingService alloc] init];
            [service setCustomServiceURL:@"https://valhalla.openstreetmap.de/route"];
            [service setProfile:@"pedestrian"];

            MSFProjection *projection = [[demo.mapView getOptions] getBaseProjection];
            MSFMapPosVector *points = [[MSFMapPosVector alloc] init];
            [points add:[projection fromWgs84:[[MSFMapPos alloc] initWithX:5.7249 y:45.1877]]]; // Place Grenette
            [points add:[projection fromWgs84:[[MSFMapPos alloc] initWithX:5.7148 y:45.1916]]]; // Gare de Grenoble
            MSFRoutingResult *result =
                [service calculateRoute:[[MSFRoutingRequest alloc] initWithProjection:projection points:points]];
            if (!result) {
                [DemoToast show:@"online routing returned nothing"];
                return;
            }
            dispatch_async(dispatch_get_main_queue(), ^{ [self drawRoute:demo result:result]; });
        } @catch (NSException *exception) {
            [DemoToast show:[NSString stringWithFormat:@"online routing failed: %@", exception.reason]];
        }
    });
}

+ (void)drawRoute:(DemoMap *)demo result:(MSFRoutingResult *)result {
    MSFMapPosVector *poses = [result getPoints];
    MSFLineStyleBuilder *style = [[MSFLineStyleBuilder alloc] init];
    [style setWidth:4];
    [style setColor:[[MSFColor alloc] initWithR:255 g:0 b:0 a:255]];
    [[self results:demo] add:[[MSFLine alloc] initWithPoses:poses style:[style buildStyle]]];

    // One arrow per real turn: the start and the destination have nothing to point at.
    [demo clearManeuverArrows];
    MSFProjection *projection = [result getProjection];
    MSFMapPosVector *wgs = [[MSFMapPosVector alloc] init];
    for (int i = 0; i < (int)[poses size]; i++) {
        [wgs add:[projection toWgs84:[poses get:i]]];
    }
    MSFRoutingInstructionVector *instructions = [result getInstructions];
    int arrows = 0;
    MSFManeuverArrowBuilder *builder = [self builderFor:demo
                                                before:[DemoConfig floatFor:@"maneuverBefore"]
                                                 after:[DemoConfig floatFor:@"maneuverAfter"]];
    for (int i = 0; i < (int)[instructions size]; i++) {
        MSFRoutingInstruction *instruction = [instructions get:i];
        int index = [instruction getPointIndex];
        if (index <= 0 || index + 1 >= (int)[wgs size]) {
            continue;
        }
        [demo setManeuverArrow:arrows++
                         arrow:[builder buildArrowAtIndex:nil points:wgs maneuverIndex:index]];
    }
    [DemoToast show:[NSString stringWithFormat:@"route drawn (%d points, %d arrows, %.0f m)",
                     (int)[poses size], arrows, [result getTotalDistance]]];
}

// =================================================================================================
// SEARCH
// =================================================================================================

/**
 * Searches the base layer's own tiles around the map centre. Needs a VECTOR base layer: the search
 * service reads the same source and decoder the layer renders from.
 */
+ (void)runVectorTileSearch:(DemoMap *)demo {
    MSFLayers *layers = [demo.mapView getLayers];
    MSFVectorTileLayer *baseLayer = nil;
    for (int i = 0; i < [layers count]; i++) {
        MSFLayer *layer = [layers get:i];
        if ([layer isKindOfClass:[MSFVectorTileLayer class]]) {
            baseLayer = (MSFVectorTileLayer *)layer;
            break;
        }
    }
    if (!baseLayer) {
        [DemoToast show:@"search needs the vector base layer (LAYERS > base map)"];
        return;
    }

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @try {
            MSFProjection *projection = [[demo.mapView getOptions] getBaseProjection];
            MSFVectorTileSearchService *service =
                [[MSFVectorTileSearchService alloc] initWithDataSource:[baseLayer getDataSource]
                                                         tileDecoder:[baseLayer getTileDecoder]];
            [service setMaxResults:20];
            [service setMinZoom:14];
            [service setMaxZoom:14];

            MSFSearchRequest *request = [[MSFSearchRequest alloc] init];
            [request setProjection:projection];
            [request setGeometry:[[MSFPointGeometry alloc] initWithPos:[demo.mapView getFocusPos]]];
            [request setSearchRadius:250];

            NSDate *start = [NSDate date];
            MSFVectorTileFeatureCollection *results = [service findFeatures:request];
            NSTimeInterval ms = -[start timeIntervalSinceNow] * 1000.0;
            int count = results ? [results getFeatureCount] : 0;
            [DemoToast show:[NSString stringWithFormat:@"search: %d features within 250 m in %.0f ms",
                             count, ms]];
            for (int i = 0; i < MIN(count, 5); i++) {
                MSFVectorTileFeature *feature = [results getFeature:i];
                NSLog(@"MassifDemo:   layer '%@' id %lld", [feature getLayerName], [feature getId]);
            }
        } @catch (NSException *exception) {
            [DemoToast show:[NSString stringWithFormat:@"search failed: %@", exception.reason]];
        }
    });
}

// =================================================================================================
// GEOJSON
// =================================================================================================

+ (void)addGeoJSONLine:(DemoMap *)demo {
    @try {
        MSFMBVectorTileDecoder *decoder = [[MSFMBVectorTileDecoder alloc] initWithCartoCSSStyleSet:
            [[MSFCartoCSSStyleSet alloc] initWithCartoCSS:
             @"#items { line-color: #374C70; line-cap: round; line-join: round; line-width: 12; }"]];
        MSFGeoJSONVectorTileDataSource *source =
            [[MSFGeoJSONVectorTileDataSource alloc] initWithMinZoom:0 maxZoom:24];
        int layerIndex = [source createLayer:@"items"];
        [source addGeoJSONStringFeature:layerIndex geoJSON:
            @"{\"type\":\"Feature\",\"id\":1,\"properties\":{\"name\":\"test\"},"
            @"\"geometry\":{\"type\":\"LineString\",\"coordinates\":"
            @"[[5.7249,45.1982],[5.7225,45.1975],[5.7220,45.1949],[5.7201,45.1935],[5.7255,45.1915]]}}"];
        MSFVectorTileLayer *layer = [[MSFVectorTileLayer alloc] initWithDataSource:source decoder:decoder];
        [[demo.mapView getLayers] add:layer];
        [sTestLayers addObject:layer];
        [DemoToast show:@"geojson line added"];
    } @catch (NSException *exception) {
        [DemoToast show:[NSString stringWithFormat:@"geojson test failed: %@", exception.reason]];
    }
}

/**
 * One BalloonPopup per '|' separated font list, stacked around the camera: what a font name
 * resolves to on THIS device. A popup draws with CoreText, not with the tile labels' FreeType
 * path, so this is where the two font stacks can be compared side by side.
 *
 * '-popupFonts sample' at launch (or the panel button) shows the built-in set - one entry per
 * capability - and '-popupFonts "A|B, C"' a set of your own.
 */
+ (void)runPopupFonts:(DemoMap *)demo {
    NSString *which = [DemoConfig stringFor:@"popupFonts"];
    if (!which.length || [which caseInsensitiveCompare:@"sample"] == NSOrderedSame
            || [which caseInsensitiveCompare:@"true"] == NSOrderedSame) {
        which = [DemoConfig stringFor:@"popupFontsSample"];
    }
    NSArray<NSString *> *fonts = [which componentsSeparatedByString:@"|"];
    MSFLocalVectorDataSource *source = [self results:demo];
    MSFProjection *projection = [[demo.mapView getOptions] getBaseProjection];
    MSFMapPos *wgs = [projection toWgs84:[demo.mapView getFocusPos]];
    if ([wgs getX] == 0 && [wgs getY] == 0) { // no frame yet at startup
        wgs = [[MSFMapPos alloc] initWithX:[DemoConfig doubleFor:@"lon"] y:[DemoConfig doubleFor:@"lat"]];
    }
    for (NSUInteger i = 0; i < fonts.count; i++) {
        NSString *font = fonts[i];
        MSFBalloonPopupStyleBuilder *builder = [[MSFBalloonPopupStyleBuilder alloc] init];
        [builder setTitleFontName:font];
        [builder setTitleFontSize:18];
        [builder setDescriptionFontName:font];
        [builder setDescriptionFontSize:14];
        MSFMapPos *wgsPos = [[MSFMapPos alloc] initWithX:[wgs getX]
                                                       y:[wgs getY] + 0.0016 * ((double)i - (fonts.count - 1) / 2.0)];
        MSFBalloonPopup *popup = [[MSFBalloonPopup alloc] initWithPos:[projection fromWgs84:wgsPos]
                                                               style:[builder buildStyle]
                                                               title:font
                                                                desc:@"Handgloves 0123"];
        [source add:popup];
    }
    [DemoToast show:[NSString stringWithFormat:@"popup fonts: %d", (int)fonts.count]];
}

/**
 * Times GeoJSONVectorTileDataSource with no renderer in the way: build a synthetic dataset, import
 * it, then add the layer. Long lines are the shape the geojson-vt pyramid work was measured on.
 */
+ (void)runGeoJSONBench:(DemoMap *)demo {
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @try {
            double lon = [DemoConfig doubleFor:@"lon"], lat = [DemoConfig doubleFor:@"lat"];
            NSMutableArray<NSString *> *features = [NSMutableArray array];
            for (int line = 0; line < 200; line++) {
                NSMutableArray<NSString *> *points = [NSMutableArray array];
                for (int i = 0; i < 60; i++) {
                    double dx = (i - 30) * 0.002 + line * 0.0004;
                    double dy = sin(i * 0.4 + line) * 0.002 + (line - 100) * 0.0004;
                    [points addObject:[NSString stringWithFormat:@"[%f,%f]", lon + dx, lat + dy]];
                }
                [features addObject:[NSString stringWithFormat:
                    @"{\"type\":\"Feature\",\"properties\":{},\"geometry\":"
                    @"{\"type\":\"LineString\",\"coordinates\":[%@]}}",
                    [points componentsJoinedByString:@","]]];
            }
            NSString *geoJSON = [NSString stringWithFormat:
                @"{\"type\":\"FeatureCollection\",\"features\":[%@]}",
                [features componentsJoinedByString:@","]];

            NSDate *start = [NSDate date];
            MSFGeoJSONVectorTileDataSource *source =
                [[MSFGeoJSONVectorTileDataSource alloc] initWithMinZoom:0 maxZoom:24];
            int layerIndex = [source createLayer:@"route"];
            [source setLayerGeoJSONString:layerIndex geoJSON:geoJSON];
            NSTimeInterval buildMs = -[start timeIntervalSinceNow] * 1000.0;

            MSFMBVectorTileDecoder *decoder = [[MSFMBVectorTileDecoder alloc] initWithCartoCSSStyleSet:
                [[MSFCartoCSSStyleSet alloc] initWithCartoCSS:[DemoStyles routeTestStyle]]];
            dispatch_async(dispatch_get_main_queue(), ^{
                MSFVectorTileLayer *layer = [[MSFVectorTileLayer alloc] initWithDataSource:source
                                                                                 decoder:decoder];
                [[demo.mapView getLayers] add:layer];
                [sTestLayers addObject:layer];
                [DemoToast show:[NSString stringWithFormat:
                    @"geojson bench: 200 lines x 60 points, source built in %.0f ms", buildMs]];
            });
        } @catch (NSException *exception) {
            [DemoToast show:[NSString stringWithFormat:@"geojson bench failed: %@", exception.reason]];
        }
    });
}

@end
