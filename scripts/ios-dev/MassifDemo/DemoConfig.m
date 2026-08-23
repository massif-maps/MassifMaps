#import "DemoConfig.h"
#import "DemoAstro.h"

@implementation DemoConfig

static NSMutableDictionary *sValues = nil;

+ (void)initialize {
    if (self != [DemoConfig class]) {
        return;
    }
    // Defaults, grouped and named exactly as in DemoConfig.java. A key that is not here cannot be
    // overridden at launch either, so adding a knob is one line.
    sValues = [@{
        // --- base map ---
        @"base":                @"composite",   // plain | composite
        @"style":               @"inline",      // dir | zip | inline | project
        @"map":                 @YES,
        @"singlePass":          @YES,

        // --- layers ---
        @"satLayer":            @NO,
        @"hillshade":           @NO,
        @"hypso":               @NO,
        @"contourLayer":        @NO,
        @"contourTiles":        @NO,
        @"elements":            @NO,
        @"routes":              @NO,

        // --- composite slots ---
        @"hs":                  @NO,
        @"sat":                 @NO,
        // Composite contour slot: merged INTO the master source and styled by the '#contour'
        // rules, which is why it is on by default while the stand-alone layer is not.
        @"contour":             @YES,
        @"hsBias":              @0.0f,

        // --- tile sources ---
        @"vectorUrl":           @"https://tiles.akylas.fr/data/france/{z}/{x}/{y}.pbf",
        @"vectorMaxZoom":       @14,
        @"demUrl":              @"https://tiles.mapterhorn.com/{z}/{x}/{y}.webp",
        @"demMaxZoom":          @16,
        @"demEncoding":         @"terrarium",
        @"contourTilesUrl":     @"https://tiles.akylas.fr/data/contours/{z}/{x}/{y}.pbf",
        @"contourTilesMaxZoom": @14,
        @"rasterUrl":           @"https://tile.openstreetmap.org/{z}/{x}/{y}.png",
        @"userAgent":           @"MassifDemo-iOS/1.0",

        // --- camera ---
        @"lon":                 @5.718957,
        @"lat":                 @45.187362,
        @"zoom":                @16.22f,
        @"tilt":                @26.0f,
        @"rotation":            @(-15.12f),

        // --- 3D terrain ---
        @"terrain":             @YES,
        @"exaggeration":        @1.0f,
        @"meshResolution":      @64,
        @"clearance":           @60.0f,
        @"drape":               @YES,
        @"drapeLines":          @NO,
        @"drapeResolution":     @0,
        @"stitch":              @YES,
        @"seamlessEdges":       @YES,
        @"prefetch":            @YES,
        @"occlusion":           @YES,
        @"occlusionTolerance":  @0.0f,
        @"maxTileZoomOffset":   @0,
        @"coarsening":          @8,
        @"backgroundBitmap":    @NO,
        @"tilePool":            @1,
        @"lodFactor":           @0.5f,
        @"labelMaxDistance":    @2000.0f,

        // --- fog / view distance / sky ---
        @"fog":                 @NO,
        // Ranges are multiples of the camera-to-focus distance, not metres, so one pair holds at
        // every zoom. Same names and values as the Android demo's FOG_* fields.
        @"fogRangeStart":       @0.8f,
        @"fogRangeEnd":         @8.0f,
        @"fogBlend":            @(12.0f / 90.0f),
        @"fogHorizon":          @(-1.0f),
        @"viewDistance":        @1.0f,
        @"viewDistanceMeters":  @170000.0f,
        @"sky":                 @YES,

        // --- lighting / shadows ---
        @"terrainLight":        @NO,
        @"sunHour":             @(-1.0f),
        @"sunAzimuth":          @355.0f,
        @"sunAltitude":         @9.0f,
        @"sunIntensity":        @1.0f,
        @"ambient":             @1.0f,
        @"shadow":              @0.3f,
        @"shadowSoftness":      @1.0f,
        @"shadowMapSize":       @1024,
        @"shadowCascades":      @3,
        @"shadowBias":          @1.0f,
        @"shadowDistance":      @0.0f,
        @"shadowMargin":        @3,
        @"daycycle":            @NO,
        @"dayCycleHour":        @12.0f,

        // --- hillshade ---
        @"hsMethod":            @"IGOR",
        @"apiSet":              @"",
        @"hsContrast":          @0.5f,
        @"hsHeightScale":       @0.05f,
        @"hsExaggeration":      @1.0f,
        @"hsIllumination":      @180.0f,
        @"hsContours":          @NO,
        @"hsContourInterval":   @100.0f,
        @"slopes":              @NO,

        // --- on-the-fly contours ---
        @"contourInterval":     @10.0f,
        @"contourResolution":   @128,
        @"contourSimplify":     @1.5f,
        @"contourMinZoom":      @5,
        @"contourSeamless":     @YES,
        @"contourStubs":        @NO,
        @"contourStubInterval": @0.0f,
        @"stubsFromTerrain":    @YES,

        // --- inline style ---
        @"bg":                  @"#eef2f0",
        @"bld3d":               @NO,
        @"bldLight":            @1.0f,
        @"bldAmbient":          @0.35f,
        @"bldHeight":           @14.0f,
        @"styleLight":          @NO,
        @"labels":              @YES,
        @"minimal":             @NO,
        @"compOp":              @"",
        @"roadWidth":           @"linear([view::zoom], (12, 0.6), (18, 4.0))",
        @"motorwayWidth":       @"linear([view::zoom], (12, 1.5), (18, 9.0))",
        @"contourWidth":        @"linear([view::zoom], (12, 0.4), (18, 1))",
        @"landcoverOpacity":    @1.0f,
        @"satZoom":             @11,

        // --- POI labels ---
        @"poiAnchors":          @"right,left,top,bottom",
        @"poiTextOptional":     @YES,
        @"poiTextDx":           @2.0f,
        @"poiFontIcon":         @YES,
        @"poiBitmapIcon":       @NO,
        @"poiTextAlign":        @"auto",
        @"poiTextBg":           @NO,
        @"poiIconBg":           @NO,
        @"poiBgRadius":         @3.0f,
        @"poiBgPadding":        @3.0f,
        @"poiWrapWidth":        @90.0f,

        // --- route test / maneuvers ---
        @"routeTest":           @NO,
        @"routeWidth":          @10.0f,
        @"routeCaseWidth":      @16.0f,
        @"routeColor":          @"#4285F4",
        @"routeCaseColor":      @"#FFFFFF",
        @"routeJoin":           @"round",
        @"routeCap":            @"round",
        @"routeMiterLimit":     @4.0f,
        @"routeOpacity":        @1.0f,
        @"routeOpacityMode":    @"geom",      // geom | layer
        @"maneuvers":           @NO,
        @"maneuverBefore":      @30.0f,
        @"maneuverAfter":       @30.0f,
        @"maneuverWidth":       @8.0f,
        @"maneuverCaseWidth":   @13.0f,
        @"maneuverColor":       @"#FFFFFF",
        @"maneuverCaseColor":   @"#1A73E8",
        @"maneuverArrowWidth":  @2.4f,
        @"maneuverArrowLength": @1.9f,
        @"maneuverZoomRef":     @17.0f,
        @"maneuverZoomMin":     @12.0f,
        @"maneuverMinScale":    @0.3f,
        // An explicit head outline wins over the SVG; both empty = the built-in triangle.
        @"maneuverPath":        @"",
        @"maneuverSvg":         @"",
        @"maneuverPathScale":   @1.0f,
        @"maneuverPathRotation": @0.0f,

        // --- peak callout labels ---
        @"peaks":               @NO,
        @"peaksMinZoom":        @8,
        @"peaksTextSize":       @16.0f,
        @"peaksAngle":          @55.0f,
        @"peaksBand":           @0.25f,
        @"peaksOffset":         @10.0f,
        @"peaksStep":           @26.0f,
        @"peaksRows":           @1,
        @"peaksMinDistance":    @14.0f,
        @"peaksPersist":        @2,
        @"peaksLineWidth":      @1.0f,
        @"peaksLineAnchor":     @"",
        @"peaksAlign":          @"",
        @"peaksPinTop":         @YES,
        @"peaksTopOffset":      @0.03f,
        @"peaksEleScale":       @0.62f,
        @"peaksEleColor":       @"#6b7280",
        @"peaksEleGap":         @3.0f,
        @"peaksEleDy":          @0.0f,
        @"peaksDistanceRank":   @100.0f,
        @"peaksBgColor":        @"#ffffff",
        @"peaksBgOpacity":      @0.8f,
        @"peaksBgRadius":       @3.0f,
        @"peaksBgPaddingX":     @4.0f,
        @"peaksBgPaddingY":     @2.0f,
        @"peaksMaxDistance":    @120000.0f,

        // --- peak finder / relief surface / outline effect ---
        @"peakfinder":          @NO,
        @"peakfinderDelay":     @8000.0f,
        @"peakFinderTilt":      @25.0f,
        @"peakFinderElevation": @0.0f,
        @"peakFinderOcclusion": @0.15f,
        @"peakFinderViewDistance": @3.0f,
        @"peakFinderFlyElevation": @1000.0f,
        @"peakFinderFlyZoom":   @13.6f,
        @"peakFinderFlyDuration": @3.5f,
        @"peakFinderFlyClimb":  @1500.0f,
        @"reliefSurface":       @NO,
        @"reliefOutline":       @NO,
        @"reliefDark":          @NO,
        @"reliefShade":         @0.55f,
        @"reliefAmbient":       @0.35f,
        @"reliefHaze":          @0.7f,
        @"reliefHazeDistance":  @60000.0f,
        @"reliefWidth":         @1.2f,
        @"reliefHorizonBoost":  @2.5f,
        @"reliefThreshold":     @1.0f,
        @"reliefCrease":        @0.6f,
        // The relief palette, one pair per mode - the surface, the ink, the plate and the sky all
        // come from these.
        @"reliefInkLight":      @"#14141a",
        @"reliefInkDark":       @"#e8ecf5",
        @"reliefPaperLight":    @"#f7f7f4",
        @"reliefPaperDark":     @"#10131a",
        @"reliefShadeLight":    @"#6c7280",
        @"reliefShadeDark":     @"#5a6070",
        @"reliefSkyLight":      @"#9fc6e8",
        @"reliefSkyDark":       @"#070a12",

        // --- AR: the relief view over the camera ---
        @"ar":                  @NO,
        @"arOrientation":       @YES,
        @"arCamera":            @YES,

        // --- free roam ---
        @"freeRoam":            @"off",       // off | look | fps
        @"panSpeed":            @"anchored",  // map | anchored | constant
        @"lookSensitivity":     @90.0f,
        @"moveSpeed":           @0.5f,
        @"lookUp":              @90.0f,

        // --- celestial objects ---
        @"celestial":           @NO,
        @"celestialSun":        @YES,
        @"celestialMoon":       @YES,
        @"celestialArc":        @YES,
        @"celestialMoonArc":    @YES,
        @"celestialMoonPhase":  @YES,
        @"celestialSunSize":    @2.5f,
        @"celestialMoonSize":   @2.0f,
        @"celestialArcWidth":   @2.0f,

        // --- stars ---
        @"stars":               @NO,
        @"starsStars":          @YES,
        @"starsFigures":        @YES,
        @"starsPlanets":        @YES,
        @"starsEquator":        @NO,
        @"starsSize":           @5.0f,
        @"starsSizePerMagnitude": @0.55f,
        @"starsFaintestSize":   @1.4f,
        @"starsFigureWidth":    @1.5f,
        @"starsFigureClickRadius": @2.5f,
        @"starsPlanetSize":     @1.2f,
        @"starsLabels":         @YES,
        @"starsLabelTextSize":  @15.0f,
        @"starsLabelScale":     @1.0f,
        @"starsLabelOpacity":   @0.85f,

        // --- star sky: the map switched off entirely, only the sky left ---
        @"starSky":             @NO,
        @"starSkyFade":         @600.0f,
        @"starSkyOrientation":  @NO,
        @"starSkyCamera":       @NO,
        @"starSkyTranslucent":  @YES,

        // The date the sky is drawn for; 0 means TODAY, which is what makes the sun, the moon, the
        // planets and the stars the ones actually up there.
        @"sunYear":             @0,
        @"sunMonth":            @0,
        @"sunDay":              @0,

        // --- scripted camera moves, for screen recordings without touch input ---
        @"anim":                @"",          // "" | zoom | pan | rotate | zoomseq
        @"animDelay":           @12000.0f,
        @"animDuration":        @8.0f,
        @"animZoomDelta":       @3.0f,
        @"animLonDelta":        @0.05f,
        @"animLatDelta":        @0.0f,
        @"animRotation":        @180.0f,
        @"animZoomOut":         @10.2f,
        @"animSettle":          @8000.0f,

        // --- style parameter ---
        @"paramInterval":        @0.0f,

        // --- marker popup fonts (DemoTests runPopupFonts) ---
        // "" = off at startup. sample | a '|' separated list of CSS-like font lists.
        @"popupFonts":          @"",
        // What 'sample' shows: one popup per capability, in order.
        @"popupFontsSample":    @"Roboto"                       // a named system font
                                 "|serif"                       // a generic family
                                 "|monospace"
                                 "|HelveticaNeue-Light"         // a weight of a family
                                 "|NoSuchFont, monospace"       // list: the 1st name is skipped
                                 "|android:sans-serif-light, ios:HelveticaNeue-Light, windows:Segoe UI Light",

        // --- debug ---
        @"tileBorders":         @NO,

        // --- app ---
        @"ui":                  @YES,
    } mutableCopy];

    // TODAY, unless the launch arguments say otherwise: what the sky demos draw is then the sky
    // that is actually up there.
    int year, month, day;
    double hour;
    [DemoAstro nowUtcYear:&year month:&month day:&day hour:&hour];
    sValues[@"sunYear"] = @(year);
    sValues[@"sunMonth"] = @(month);
    sValues[@"sunDay"] = @(day);
}

+ (double)currentHourUtc {
    float explicitHour = [self floatFor:@"sunHour"];
    if (explicitHour >= 0) {
        return explicitHour;
    }
    if ([self boolFor:@"daycycle"]) {
        return [self floatFor:@"dayCycleHour"];
    }
    int year, month, day;
    double hour;
    [DemoAstro nowUtcYear:&year month:&month day:&day hour:&hour];
    return hour;
}

+ (id)valueFor:(NSString *)key {
    return sValues[key];
}

+ (BOOL)boolFor:(NSString *)key {
    id value = sValues[key];
    if ([value isKindOfClass:[NSString class]]) {
        return [value caseInsensitiveCompare:@"true"] == NSOrderedSame || [value intValue] != 0;
    }
    return [value boolValue];
}

+ (float)floatFor:(NSString *)key { return [sValues[key] floatValue]; }
+ (double)doubleFor:(NSString *)key { return [sValues[key] doubleValue]; }
+ (int)intFor:(NSString *)key { return [sValues[key] intValue]; }

+ (NSString *)stringFor:(NSString *)key {
    id value = sValues[key];
    return [value isKindOfClass:[NSString class]] ? value : [value stringValue];
}

+ (unsigned int)colorFor:(NSString *)key {
    NSString *text = [[self stringFor:key] stringByReplacingOccurrencesOfString:@"#" withString:@""];
    unsigned int value = 0;
    [[NSScanner scannerWithString:text] scanHexInt:&value];
    // "#rrggbb" carries no alpha; the SDK wants ARGB, so assume opaque.
    return text.length <= 6 ? (0xff000000 | value) : value;
}

+ (void)setValue:(id)value forKey:(NSString *)key {
    sValues[key] = value;
}

+ (NSArray<NSString *> *)allKeys {
    return [sValues.allKeys sortedArrayUsingSelector:@selector(compare:)];
}

+ (void)applyLaunchArgumentOverrides {
    // UIKit has already folded '-key value' launch arguments into NSUserDefaults; anything that
    // names a known key wins over the default. Values stay strings, which the accessors coerce.
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    for (NSString *key in [sValues.allKeys copy]) {
        NSString *override = [defaults stringForKey:key];
        if (override) {
            sValues[key] = override;
        }
    }
}

@end
