#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"
#import "api/MSFMapEvents.h"

/**
 * Stars and a constellation over the Matterhorn at dusk, and a camera that can look up at them.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/terrain/CelestialObjectsExample.java.
 */
@interface MSFCelestialObjectsExample : NSObject <MSFExample>
@end

static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

static const double kLon = 7.6586;
static const double kLat = 45.9763;

typedef struct {
    const char *name;
    double ra;
    double dec;
    double magnitude;
    const char *color;
} Star;

// Right ascension in hours, declination in degrees (J2000), magnitude, colour.
static const Star kStars[] = {
    { "Betelgeuse", 5.9195, 7.407, 0.5, "#ffc58f" },
    { "Rigel", 5.2423, -8.2016, 0.13, "#cad8ff" },
    { "Bellatrix", 5.4189, 6.3497, 1.64, "#d5e2ff" },
    { "Mintaka", 5.5334, -0.2991, 2.2, "#d5e2ff" },
    { "Alnilam", 5.6036, -1.2019, 1.69, "#d5e2ff" },
    { "Alnitak", 5.6793, -1.9426, 1.77, "#d5e2ff" },
    { "Saiph", 5.7959, -9.6696, 2.06, "#d5e2ff" },
    { "Meissa", 5.5856, 9.9342, 3.39, "#d5e2ff" },
    { "Sirius", 6.7525, -16.7161, -1.46, "#e6eeff" },
    { "Procyon", 7.655, 5.225, 0.34, "#fff4e8" },
    { "Aldebaran", 4.5987, 16.5093, 0.86, "#ffcf9e" },
    { "Capella", 5.2782, 45.998, 0.08, "#fff1c9" },
    { "Pollux", 7.7553, 28.026, 1.14, "#ffe3b8" },
    { "Castor", 7.5767, 31.888, 1.58, "#e6eeff" },
};
enum { kStarCount = sizeof(kStars) / sizeof(kStars[0]) };

static NSArray<NSString *> *named(void) {
    return @[ @"Betelgeuse", @"Rigel", @"Sirius", @"Procyon", @"Aldebaran", @"Capella", @"Pollux" ];
}

static NSArray<NSString *> *orion(void) {
    return @[ @"Betelgeuse", @"Bellatrix", @"Betelgeuse", @"Alnitak", @"Bellatrix", @"Mintaka", @"Mintaka",
              @"Alnilam", @"Alnilam", @"Alnitak", @"Alnitak", @"Saiph", @"Mintaka", @"Rigel", @"Betelgeuse",
              @"Meissa", @"Meissa", @"Bellatrix" ];
}

// The sun of an April evening: it lights the sky from under the horizon.
static const double kSunRa = 1.5;
static const double kSunDec = 9.5;

static double radians(double degrees) {
    return degrees * M_PI / 180;
}

/** Equatorial to horizontal: the SDK only knows directions, the astronomy is the app's. */
static void direction(double ra, double dec, double siderealTime, double *az, double *alt) {
    double hourAngle = radians(siderealTime - ra * 15);
    double phi = radians(kLat);
    double delta = radians(dec);
    double a = asin(sin(phi) * sin(delta) + cos(phi) * cos(delta) * cos(hourAngle));
    double z = atan2(sin(hourAngle), cos(hourAngle) * sin(phi) - tan(delta) * cos(phi));
    *az = fmod(z * 180 / M_PI + 540, 360);
    *alt = a * 180 / M_PI;
}

@implementation MSFCelestialObjectsExample {
    __weak id<MSFExampleHost> _host;
    MSFMassifObject *_figure;
    MSFMassifObject *_title;
    NSMutableArray<MSFMassifObject *> *_stars;
    NSMutableArray<MSFMassifObject *> *_names;
    MSFSubscription *_click;
    double _siderealTime;
    BOOL _turning;
}

+ (NSString *)exampleId {
    return @"celestial-objects";
}

- (void)startWithHost:(id<MSFExampleHost>)host {
    _host = host;
    _siderealTime = 132;
    MSFMassifMap *map = host.map;
    double scale = UIScreen.mainScreen.scale;

    [map addLayer:@"satellite"
             spec:[[MSFSpec of:@"raster"]
                     set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
                         set:@"databasePath" value:[host cachePath:@"world-imagery.db"]]
                         set:@"capacity" value:@(200 * 1024 * 1024)]
                         set:@"source" value:[[[[MSFSpec of:@"http"]
                             set:@"url" value:@"https://server.arcgisonline.com/ArcGIS/rest/services/"
                                               @"World_Imagery/MapServer/tile/{z}/{y}/{x}"]
                             set:@"maxZoom" value:@18]
                             set:@"HTTPHeaders" value:[[MSFSpec object] set:@"User-Agent" value:kUserAgent]]]]
            error:nil];
    [map style:@"hybrid"
          spec:[[MSFSpec of:@"mbvt"]
                  set:@"project" value:[[[MSFSpec of:@"project"]
                      set:@"assets" value:[[MSFSpec of:@"zip"]
                          set:@"data" value:[[MSFSpec of:@"url"]
                              set:@"url" value:@"assets://styles/massif.zip"]]]
                      set:@"name" value:@"hybrid"]]
         error:nil];
    [map addLayer:@"labels"
             spec:[[[MSFSpec of:@"vector"]
                     set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
                         set:@"databasePath" value:[host cachePath:@"openfreemap.db"]]
                         set:@"capacity" value:@(100 * 1024 * 1024)]
                         set:@"source" value:[[[[MSFSpec of:@"http"]
                             set:@"url" value:@"https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf"]
                             set:@"maxZoom" value:@14]
                             set:@"HTTPHeaders" value:[[MSFSpec object] set:@"User-Agent" value:kUserAgent]]]]
                     set:@"style" value:@"hybrid"]
            error:nil];
    MSFSpec *dem = [[[[MSFSpec of:@"persistent-cache"]
        set:@"databasePath" value:[host cachePath:@"mapterhorn-dem.db"]]
        set:@"capacity" value:@(200 * 1024 * 1024)]
        set:@"source" value:[[[[[MSFSpec of:@"http"]
            set:@"url" value:@"https://tiles.mapterhorn.com/{z}/{x}/{y}.webp"]
            set:@"minZoom" value:@1]
            set:@"maxZoom" value:@16]
            set:@"metaData" value:[[MSFSpec object] set:@"dem_encoding" value:@"terrarium"]]];
    [[map terrainWithSpec:[[MSFSpec of:@"terrain"] set:@"source" value:dem] error:nil]
        apply:[[[MSFSpec object] set:@"viewDistanceFactor" value:@3] set:@"cameraClearance" value:@40]];
    [map skyWithSpec:[[MSFSpec of:@"sky"] set:@"atmosphereLuminance" value:@2.4] error:nil];
    [map fogWithSpec:[[[[[[[MSFSpec of:@"fog"]
        set:@"rangeStart" value:@2]
        set:@"rangeEnd" value:@10]
        set:@"color" value:@0xff2a3450]
        set:@"highColor" value:@0xff1c2a4a]
        set:@"spaceColor" value:@0xff070b18]
        set:@"starIntensity" value:@0.6] error:nil];
    [map lightWithSpec:[[[[[MSFSpec of:@"light"]
        set:@"terrainLightingEnabled" value:@YES]
        set:@"sunIntensity" value:@0]
        set:@"ambientColor" value:@0xff7080b0]
        set:@"ambientIntensity" value:@0.28] error:nil];

    // First in the stack: the terrain then hides whatever sets behind a ridge.
    MSFMassifLayer *sky = [[map addLayer:@"sky" spec:[MSFSpec of:@"celestial"] error:nil] moveTo:0];
    _figure = [self add:sky id:@"orion" spec:[[[[MSFSpec of:@"arc"] set:@"color" value:@"#93c5fd80"]
        set:@"width" value:@(1.5 * scale)] set:@"belowHorizonVisible" value:@YES]];
    _stars = [NSMutableArray array];
    for (int i = 0; i < kStarCount; i++) {
        NSString *name = @(kStars[i].name);
        [_stars addObject:[self add:sky id:[@"star." stringByAppendingString:name]
                               spec:[[[[[[MSFSpec of:@"sprite"]
                                   set:@"screenSize" value:@(MAX(3, 8 - 1.6 * kStars[i].magnitude) * scale)]
                                   set:@"color" value:@(kStars[i].color)]
                                   set:@"softness" value:@0.5]
                                   set:@"clickRadius" value:@1.5]
                                   set:@"metaData" value:[[MSFSpec object] set:@"name" value:name]]]];
    }
    _names = [NSMutableArray array];
    for (NSString *name in named()) {
        [_names addObject:[self label:sky id:[@"name." stringByAppendingString:name] text:name fontSize:12]];
    }
    _title = [self label:sky id:@"name.Orion" text:@"ORION" fontSize:14];
    [[_title call:@"setAnchorPoint" args:@[ @-1, @0 ] error:nil] destroy];
    [[_title call:@"setOffset" args:@[ @10, @0 ] error:nil] destroy];

    [self place];
    // Tilt 90 is straight down; below 0 the camera keeps its place and only looks up.
    [map apply:[[[MSFSpec object] set:@"freeRoamMode" value:@"FREE_ROAM_MODE_LOOK"]
                  set:@"tiltRange" value:@[ @-90, @90 ]]];
    [map.camera moveTo:[MSFPosition positionWithLng:kLon lat:kLat] zoom:12.5 rotation:110 tilt:-18];

    __weak MSFCelestialObjectsExample *weakSelf = self;
    _click = [sky on:@"celestial.clicked" handler:^(MSFMapEvent *event) {
        NSString *name = [event get:@"celestialObject.metaData.name"];
        MSFCelestialObjectsExample *strong = weakSelf;
        if (name && strong) {
            [strong->_host caption:name];
        }
    }];

    [host button:@"Look up" action:^{
        BOOL up = map.camera.currentTilt > -30;
        [[map.camera animate:1.2] tilt:up ? -55 : -18];
    }];
    [host toggle:@"Figures" on:YES action:^(BOOL on) {
        MSFCelestialObjectsExample *strong = weakSelf;
        for (MSFMassifObject *object in [@[ strong->_figure, strong->_title ] arrayByAddingObjectsFromArray:strong->_names]) {
            [object set:@"visible" value:@(on)];
        }
    }];
    [host toggle:@"Turn the sky" on:NO action:^(BOOL on) {
        MSFCelestialObjectsExample *strong = weakSelf;
        strong->_turning = on;
        [strong step];
    }];
    [host caption:@"Orion setting over the Matterhorn. Drag to look around, tap a star."];
}

- (void)stop {
    _turning = NO;
}

- (MSFMassifObject *)add:(MSFMassifLayer *)layer id:(NSString *)objectId spec:(MSFSpec *)spec {
    MSFMassifObject *object = [_host.map object:@"celestial" objectId:objectId spec:spec error:nil];
    [[layer call:@"add" args:@[ @(object.handle) ] error:nil] destroy];
    return object;
}

- (MSFMassifObject *)label:(MSFMassifLayer *)layer id:(NSString *)objectId text:(NSString *)text fontSize:(double)fontSize {
    MSFMassifObject *object = [self add:layer id:objectId spec:[[[[[[MSFSpec of:@"label"]
        set:@"text" value:text]
        set:@"fontSize" value:@(fontSize)]
        set:@"textColor" value:@"#e2e8f0"]
        set:@"haloColor" value:@"#0f172acc"]
        set:@"haloWidth" value:@3]];
    [[object call:@"setOffset" args:@[ @0, @8 ] error:nil] destroy];
    return object;
}

- (void)at:(NSString *)name az:(double *)az alt:(double *)alt {
    for (int i = 0; i < kStarCount; i++) {
        if ([name isEqualToString:@(kStars[i].name)]) {
            direction(kStars[i].ra, kStars[i].dec, _siderealTime, az, alt);
            return;
        }
    }
}

- (void)setDirection:(MSFMassifObject *)object star:(NSString *)name {
    double az, alt;
    [self at:name az:&az alt:&alt];
    [[object call:@"setDirection" args:@[ @(az), @(alt), @0 ] error:nil] destroy];
}

- (void)place {
    for (int i = 0; i < kStarCount; i++) {
        [self setDirection:_stars[i] star:@(kStars[i].name)];
    }
    NSArray<NSString *> *names = named();
    for (NSUInteger i = 0; i < names.count; i++) {
        [self setDirection:_names[i] star:names[i]];
    }
    [self setDirection:_title star:@"Bellatrix"];
    NSMutableArray *segments = [NSMutableArray array];
    for (NSString *name in orion()) {
        double az, alt;
        [self at:name az:&az alt:&alt];
        [segments addObject:@(az)];
        [segments addObject:@(alt)];
    }
    [[_figure call:@"setSegments" args:@[ segments ] error:nil] destroy];
    double sunAz, sunAlt;
    direction(kSunRa, kSunDec, _siderealTime, &sunAz, &sunAlt);
    [_host.map.light apply:[[[MSFSpec object] set:@"sunAzimuth" value:@(sunAz)] set:@"sunAltitude" value:@(sunAlt)]];
}

- (void)step {
    if (!_turning) {
        return;
    }
    _siderealTime += 0.5;
    [self place];
    __weak MSFCelestialObjectsExample *weakSelf = self;
    [_host after:0.1 run:^{
        [weakSelf step];
    }];
}

@end
