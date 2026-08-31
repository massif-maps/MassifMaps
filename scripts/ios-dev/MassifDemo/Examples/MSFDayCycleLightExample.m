#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * The hour drives the whole palette, and the curve that decides how is the app's to replace.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/styles/DayCycleLightExample.java.
 */
@interface MSFDayCycleLightExample : NSObject <MSFExample>
@end

@implementation MSFDayCycleLightExample {
    __weak id<MSFExampleHost> _host;
    NSUInteger _style;
    NSUInteger _formula;
    double _hour;
    double _sunAltitude;
    double _sunAzimuth;
    NSUInteger _preset;
}

+ (NSString *)exampleId {
    return @"day-cycle-light";
}

/**
 * Mapbox Standard reads MAPBOX's own vector tiles - its layers name mapbox-streets-v8 source
 * layers, so no other tileset can feed it. Put your own token here; a demo cannot ship one.
 */
static NSString *const kMapboxToken = @"<your-mapbox-access-token>";

/**
 * The two styles, both converted by `massif-style mapbox2css --live-light`: the colours stay as the
 * style authored them and the `*-emissive-strength` values ride along, for the SDK to light at draw
 * time. That is what leaves ONE palette covering every hour.
 */
static NSArray<NSArray<NSString *> *> *styles(void) {
    return @[ @[ @"Mapbox Standard", @"mapbox-standard" ],
              @[ @"MapTiler Streets", @"maptiler-streets" ] ];
}

/**
 * A curve is a list of lights anchored on SUN HEIGHTS, and the SDK interpolates between them. Empty
 * means the built-in one, which is MapBox Standard's own four light setups - so a converted
 * Standard renders as its `day` preset at noon and its `dusk` preset at 19h with nothing set here.
 *
 * The second is the same machinery pointed somewhere else entirely. Nothing about it is a special
 * case: the SDK derives the 2D grade, the 3D sun and ambient, and the brightness a style ramps its
 * labels over from whatever this returns.
 */
static NSArray<NSArray<NSString *> *> *formulas(void) {
    NSString *psychedelic =
        @"[{\"sunAltitude\":-15,\"ambientColor\":\"#2d0a4e\",\"ambientIntensity\":0.7,"
        @"\"sunColor\":\"#00e5ff\",\"sunIntensity\":0.4},"
        @"{\"sunAltitude\":2,\"ambientColor\":\"#ff2d95\",\"ambientIntensity\":0.85,"
        @"\"sunColor\":\"#ff8a00\",\"sunIntensity\":0.6},"
        @"{\"sunAltitude\":25,\"ambientColor\":\"#7cff4f\",\"ambientIntensity\":0.9,"
        @"\"sunColor\":\"#ff00d4\",\"sunIntensity\":0.5},"
        @"{\"sunAltitude\":60,\"ambientColor\":\"#00fff0\",\"ambientIntensity\":1.0,"
        @"\"sunColor\":\"#fff700\",\"sunIntensity\":0.45}]";
    NSString *mapboxSetting =
        @"[{\"sunAltitude\":-9,\"ambientColor\":\"#464d69\",\"ambientIntensity\":0.5,"
        @"\"sunColor\":\"#3f4455\",\"sunIntensity\":0.5},"
        @"{\"sunAltitude\":3,\"ambientColor\":\"#363e5e\",\"ambientIntensity\":0.8,"
        @"\"sunColor\":\"#fec286\",\"sunIntensity\":0.2},"
        @"{\"sunAltitude\":12,\"ambientColor\":\"#363e5e\",\"ambientIntensity\":0.8,"
        @"\"sunColor\":\"#fec286\",\"sunIntensity\":0.2},"
        @"{\"sunAltitude\":38,\"ambientColor\":\"#ffffff\",\"ambientIntensity\":0.8,"
        @"\"sunColor\":\"#ffffff\",\"sunIntensity\":0.2}]";
    // The same curve with Standard's `dawn` light in the twilight band instead of its `dusk` one.
    // TWO curves, because dawn and dusk are different lights at the SAME sun height: the SDK reads
    // the setting one while the sun is west and the rising one while it is east, so setting only
    // dayCycleLightStops is what made a morning render as dusk.
    NSString *mapboxRising =
        @"[{\"sunAltitude\":-9,\"ambientColor\":\"#464d69\",\"ambientIntensity\":0.5,"
        @"\"sunColor\":\"#3f4455\",\"sunIntensity\":0.5},"
        @"{\"sunAltitude\":3,\"ambientColor\":\"#ffecdc\",\"ambientIntensity\":0.75,"
        @"\"sunColor\":\"#feca8b\",\"sunIntensity\":0.5},"
        @"{\"sunAltitude\":12,\"ambientColor\":\"#ffecdc\",\"ambientIntensity\":0.75,"
        @"\"sunColor\":\"#feca8b\",\"sunIntensity\":0.5},"
        @"{\"sunAltitude\":38,\"ambientColor\":\"#ffffff\",\"ambientIntensity\":0.8,"
        @"\"sunColor\":\"#ffffff\",\"sunIntensity\":0.2}]";
    // { name, setting curve, rising curve } - one curve for both when a formula has no dawn.
    return @[ @[ @"Mapbox", mapboxSetting, mapboxRising ],
              @[ @"Psychedelic", psychedelic, psychedelic ] ];
}

/** Paris, and the camera the example opens on. */
static const double kLon = 2.3376;
static const double kLat = 48.8600;

/**
 * The EQUINOX, as its Julian day at noon UTC (2026-03-20). On it the sun rises at 6 and sets at 18
 * local solar time at every latitude, so the slider's hours mean the same thing anywhere.
 */
static const double kJulianNoon = 2461120.0;

/** Local solar time: 12 is the sun at its highest, whatever the longitude. */
static const double kStartHour = 17.4;
/**
 * The hours that land on MapBox's four presets EXACTLY, at this camera on this date: dawn 6:48
 * (sun 6.5° and east), day 12:00 (41.1°, past the 38° stop), dusk 17:24 (7.1° and west), night
 * 22:00 (-33.9°, below the -9° stop). Anywhere else on the slider the curve blends two.
 */
static const double kPresetHours[] = { 6.8, 12.0, 17.4, 22.0 };

/**
 * Local solar time to a sun position - the NOAA low-accuracy form, good to ~0.1 degree, which is
 * what LightOptions.setSunPositionFromTime computes in C++; the facade cannot reach that method,
 * so the example spells it out.
 */
static void sunPosition(double hour, double *altitude, double *azimuth) {
    double n = kJulianNoon + (hour - kLon / 15.0 - 12.0) / 24.0 - 2451545.0;
    double meanLong = 280.460 + 0.9856474 * n;
    double meanAnom = (357.528 + 0.9856003 * n) * M_PI / 180.0;
    double eclipticLong = (meanLong + 1.915 * sin(meanAnom) + 0.020 * sin(2 * meanAnom)) * M_PI / 180.0;
    double obliquity = (23.439 - 0.0000004 * n) * M_PI / 180.0;
    double rightAsc = atan2(cos(obliquity) * sin(eclipticLong), cos(eclipticLong));
    double decl = asin(sin(obliquity) * sin(eclipticLong));

    // Greenwich mean sidereal time, then the local hour angle.
    double gmst = fmod(18.697374558 + 24.06570982441908 * n, 24.0);
    double hourAngle = ((gmst < 0 ? gmst + 24.0 : gmst) * 15.0 + kLon) * M_PI / 180.0 - rightAsc;

    double lat = kLat * M_PI / 180.0;
    *altitude = asin(sin(lat) * sin(decl) + cos(lat) * cos(decl) * cos(hourAngle)) * 180.0 / M_PI;
    // atan2 here is measured from south; the SDK wants clockwise from north.
    *azimuth = atan2(sin(hourAngle), cos(hourAngle) * sin(lat) - tan(decl) * cos(lat))
             * 180.0 / M_PI + 180.0;
}

- (void)startWithHost:(id<MSFExampleHost>)host {
    _host = host;
    _hour = kStartHour;
    _preset = 2;
    MSFMassifMap *map = host.map;

    // How far a TILTED far field may coarsen: unbounded, the grazing term makes the horizon band
    // jump between levels as the camera turns, so one side keeps its buildings and the other does
    // not. This caps the grazing half alone; distance still coarsens freely.
    [map.options set:@"tileLODForeshorteningLimit" value:@1.0];

    [self buildLayer:map];

    // A TERRAIN, for the shadows. Cast shadows are drawn from the drape pass and land on the
    // terrain surface - with no terrain there is no surface to receive them and nothing casts at
    // all, however high shadowStrength goes. Paris is flat, so this is here for the light.
    [[map terrain:[[MSFSpec of:@"terrain"]
        set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
            set:@"databasePath" value:[host cachePath:@"mapterhorn-dem.db"]]
            set:@"capacity" value:@(200 * 1024 * 1024)]
            set:@"source" value:[[[[MSFSpec of:@"http"]
                set:@"url" value:@"https://tiles.mapterhorn.com/{z}/{x}/{y}.webp"]
                set:@"maxZoom" value:@16]
                set:@"metaData" value:[[MSFSpec object] set:@"dem_encoding" value:@"terrarium"]]]]]
        apply:[[[MSFSpec object] set:@"exaggeration" value:@1] set:@"cameraClearance" value:@40]];

    // The curve is only read while this is on; off, the style's and the app's own sun colours
    // stand, which is what every map did before the curve existed.
    [map light:[[[[[[MSFSpec of:@"light"]
        set:@"dayCycleLightsEnabled" value:@YES]
        set:@"sunOverridingStyle" value:@YES]
        // Without this the ground is never lit, and the shadow multiply lives in the same block -
        // so the buildings cast nothing. The style says `colors-prelit`, so lighting the ground
        // here does NOT light its colours twice: it only lets the shadows land.
        set:@"terrainLightingEnabled" value:@YES]
        // Buildings cast: a low sun is what the curve is most worth looking at, and it is also
        // when the shadows are longest. They follow the same sun the curve reads, so they stretch
        // and swing round as the hour is swept.
        set:@"shadowStrength" value:@0.35]
        set:@"shadowSoftness" value:@1.2]];

    // A sky, because the hour is the whole example: the atmosphere is integrated against the SAME
    // sun, so it reddens and darkens with the slider without a value of its own. Options starts
    // with no SkyOptions, so nothing is drawn behind the map until this line.
    [map skyWithSpec:[MSFSpec of:@"sky"] error:nil];

    [self applyFormula];
    [self applyHour];
    [map.camera moveTo:[MSFPosition positionWithLng:kLon lat:kLat] zoom:17.2 rotation:20 tilt:45];

    [host button:@"Style" action:^{
        self->_style = (self->_style + 1) % styles().count;
        [self buildLayer:self->_host.map];
        [self caption];
    }];
    [host button:@"Formula" action:^{
        self->_formula = (self->_formula + 1) % formulas().count;
        [self applyFormula];
        [self caption];
    }];
    // The HOUR, because that is what a day is: the sun walks its real arc, so dawn and dusk come
    // with the azimuth swinging round rather than being picked by hand.
    [host slider:@"Hour" min:0 max:24 value:kStartHour action:^(float value) {
        self->_hour = value;
        [self applyHour];
        [self caption];
    }];
    // Straight to MapBox's own four, so the render can be held against theirs.
    [host button:@"Preset" action:^{
        self->_preset = (self->_preset + 1) % 4;
        self->_hour = kPresetHours[self->_preset];
        [self applyHour];
        [self caption];
    }];
    [host caption:@"Two styles, two formulas: the hour picks the light, the curve picks the look."];
}

/**
 * The basemap. Each style needs the tiles it was written against - Standard names mapbox's own
 * source layers, MapTiler Streets names OpenMapTiles ones, and neither reads the other's.
 */
- (void)buildLayer:(MSFMassifMap *)map {
    // Off the stack AND out of the registry: an id is unique, so rebuilding "basemap" without this
    // fails with a duplicate-id error.
    [map removeLayer:@"basemap"];
    BOOL mapbox = (_style == 0);
    MSFSpec *source = mapbox
        ? [[[MSFSpec of:@"http"]
            set:@"url" value:[@"https://api.mapbox.com/v4/mapbox.mapbox-streets-v8,mapbox.mapbox-terrain-v2"
                              @"/{z}/{x}/{y}.vector.pbf?access_token=" stringByAppendingString:kMapboxToken]]
            set:@"maxZoom" value:@16]
        : [[[MSFSpec of:@"http"]
            set:@"url" value:@"https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf"]
            set:@"maxZoom" value:@14];

    [map addLayer:@"basemap"
             spec:[[[MSFSpec of:@"vector"]
                 // Cached on disk in front of the server: both are other people's tiles, and a demo
                 // that gets panned around re-fetches the same ones on every run.
                 set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
                     set:@"databasePath" value:[_host cachePath:(mapbox ? @"mapbox-vector.db" : @"openfreemap.db")]]
                     set:@"capacity" value:@(100 * 1024 * 1024)]
                     set:@"source" value:source]]
                 set:@"style" value:[[MSFSpec of:@"mbvt"]
                     set:@"project" value:[[MSFSpec of:@"project"]
                         set:@"assets" value:[[MSFSpec of:@"zip"]
                             set:@"data" value:[[MSFSpec of:@"url"]
                                 set:@"url" value:[NSString stringWithFormat:@"assets://styles/%@.zip",
                                                   styles()[_style][1]]]]]]]
            error:nil];
}

/**
 * The whole formula, in two properties. An empty list is the built-in curve; a list of stops
 * replaces it, and everything the SDK derives from the light follows without a re-decode - the
 * tiles are untouched, so this is a redraw. Both are written every time, or a formula without a
 * dawn of its own would keep the previous one's.
 */
- (void)applyFormula {
    [_host.map.light apply:[[[MSFSpec object]
        set:@"dayCycleLightStops" value:formulas()[_formula][1]]
        set:@"dayCycleRisingLightStops" value:formulas()[_formula][2]]];
}

/** The curve reads a sun POSITION, and the hour is where the sun actually is at that hour. */
- (void)applyHour {
    sunPosition(_hour, &_sunAltitude, &_sunAzimuth);
    [_host.map.light apply:[[[MSFSpec object]
        set:@"sunAzimuth" value:@(_sunAzimuth)]
        set:@"sunAltitude" value:@(_sunAltitude)]];
}

/**
 * Which MapBox preset this hour actually renders. The curve only returns one EXACTLY where it is
 * flat - below -9, between 3 and 12, above 38 - and everything else is a blend of two, which is why
 * an arbitrary hour never matches a `lightPreset` screenshot.
 */
- (NSString *)light {
    if (_formula != 0) {
        return @"custom curve";
    }
    NSString *twilight = _sunAzimuth <= 180.0 ? @"dawn" : @"dusk";
    if (_sunAltitude <= -9.0) {
        return @"night";
    }
    if (_sunAltitude >= 38.0) {
        return @"day";
    }
    if (_sunAltitude >= 3.0 && _sunAltitude <= 12.0) {
        return twilight;
    }
    return _sunAltitude < 3.0 ? [@"night to " stringByAppendingString:twilight]
                              : [twilight stringByAppendingString:@" to day"];
}

- (void)caption {
    [_host caption:[NSString stringWithFormat:@"%02d:%02d - sun %.0f\u00b0 - %@ - %@ on %@",
                    (int)_hour, (int)((_hour - (int)_hour) * 60), _sunAltitude, [self light],
                    formulas()[_formula][0], styles()[_style][0]]];
}

@end
