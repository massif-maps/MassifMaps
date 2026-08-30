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
    // An EMPTY list is the built-in curve. Not @"", which reads as "no value" through some
    // bindings - the property has to arrive as a list for the SDK to clear the old one.
    return @[ @[ @"Mapbox", @"[]" ], @[ @"Psychedelic", psychedelic ] ];
}

/** The hour is swept, not picked: the curve is continuous and that is the point of it. */
static const double kStartHour = 17.4;

- (void)startWithHost:(id<MSFExampleHost>)host {
    _host = host;
    _hour = kStartHour;
    MSFMassifMap *map = host.map;

    [self buildLayer:map];

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

    [self applyFormula];
    [self applyHour];
    [map.camera moveTo:[MSFPosition positionWithLng:2.3376 lat:48.8600] zoom:17.2 rotation:20 tilt:45];

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
    // A slider, because the curve is continuous: sweeping it is what shows the sun passing THROUGH
    // dusk rather than jumping between four presets.
    [host slider:@"Hour" min:0 max:24 value:kStartHour action:^(float value) {
        self->_hour = value;
        [self applyHour];
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
             spec:[[[[MSFSpec of:@"vector"]
                 // Cached on disk in front of the server: both are other people's tiles, and a demo
                 // that gets panned around re-fetches the same ones on every run.
                 set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
                     set:@"databasePath" value:[_host cachePath:(mapbox ? @"mapbox-vector.db" : @"openfreemap.db")]]
                     set:@"capacity" value:@(100 * 1024 * 1024)]
                     set:@"source" value:source]]
                 // MapBox's tiles are 512 px and this SDK's are 256, so at the same view it would
                 // ask for a level DEEPER than mapbox-gl does - and a level deeper carries a level's
                 // worth of extra labels. The converted style already reads ([view::zoom] - 1);
                 // this is the fetch side of the same offset.
                 set:@"zoomLevelBias" value:@(mapbox ? -1.0 : 0.0)]
                 set:@"style" value:[[MSFSpec of:@"mbvt"]
                     set:@"project" value:[[MSFSpec of:@"project"]
                         set:@"assets" value:[[MSFSpec of:@"zip"]
                             set:@"data" value:[[MSFSpec of:@"url"]
                                 set:@"url" value:[NSString stringWithFormat:@"assets://styles/%@.zip",
                                                   styles()[_style][1]]]]]]]
            error:nil];
}

/**
 * The whole formula, in one property. An empty list is the built-in curve; a list of stops replaces
 * it, and everything the SDK derives from the light follows without a re-decode - the tiles are
 * untouched, so this is a redraw.
 */
- (void)applyFormula {
    [_host.map.light apply:[[MSFSpec object]
        set:@"dayCycleLightStops" value:formulas()[_formula][1]]];
}

/** An hour is a sun POSITION; the curve turns that into a light. */
- (void)applyHour {
    double altitude = 62.0 * sin(M_PI * (_hour - 6.0) / 12.0);
    double azimuth = 90.0 + (_hour - 6.0) * 15.0;
    [_host.map.light apply:[[[MSFSpec object]
        set:@"sunAzimuth" value:@(azimuth)]
        set:@"sunAltitude" value:@(altitude)]];
}

- (void)caption {
    [_host caption:[NSString stringWithFormat:@"%@ formula on %@",
                    formulas()[_formula][0], styles()[_style][0]]];
}

@end
