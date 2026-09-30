#import "MSFExample.h"
#import "MSFPeakFinderSky.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * The web peak finder (web/examples/peak-finder.mjs) with the host's buttons in place of its chrome.
 * Shaders and summit CartoCSS are bundle resources, written by scripts/gen-peak-finder-assets.mjs.
 */
@interface MSFPeakFinderExample : NSObject <MSFExample>
@end

static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

/** The ink pass: silhouettes only (operator 2), a heavier skyline. Uniforms left out read zero. */
static NSDictionary<NSString *, NSNumber *> *ink(void) {
    return @{
        @"uOperator": @2, @"uOutlineGain": @12, @"uOutlinePower": @1, @"uOutlineFloor": @0.008,
        @"uOutlineCeiling": @1, @"uOutlineWidth": @1, @"uIntensity": @0.8, @"uHorizonBoost": @0.9,
        @"uHorizonWidth": @2.5, @"uDepthThreshold": @1, @"uCreaseThreshold": @0.12,
        @"uRidgeStrength": @2, @"uRidgeThreshold": @0.05, @"uRidgeGroundSpan": @90,
        @"uDepthTexelSize": @2, @"uGrazingFloor": @0.15, @"uInkDistance": @50000,
        @"uMetersPerUnit": @(40075016.68558 / (1 << 20)), @"uSilhouetteGate": @865,
        @"uHazeDistance": @60000, @"uDistortCenterX": @0.5, @"uDistortCenterY": @0.5,
        @"uDistortScreenTanX": @1, @"uDistortScreenTanY": @1, @"uDistortRenderTanX": @1,
        @"uDistortRenderTanY": @1,
    };
}

/** The surface: ridge ink capped by the light, and a touch of hillshade after the cap. */
static NSDictionary<NSString *, NSNumber *> *surface(void) {
    return @{ @"uAmbient": @0.06, @"uInkCap": @0.3, @"uRidgeInkStrength": @0.3, @"uHillshade": @0.15 };
}

static NSString *resource(NSString *name, NSString *type) {
    NSString *path = [[NSBundle mainBundle] pathForResource:name ofType:type inDirectory:@"peak-finder"];
    return [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil] ?: @"";
}

static double bearing(double lat, double lon, MSFPosition *to) {
    double phi1 = lat * M_PI / 180, phi2 = to.lat * M_PI / 180, dLon = (to.lng - lon) * M_PI / 180;
    double y = sin(dLon) * cos(phi2);
    double x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(dLon);
    return atan2(y, x) * 180 / M_PI;
}

static double distance(double lat, double lon, MSFPosition *to) {
    double dLat = (to.lat - lat) * M_PI / 180, dLon = (to.lng - lon) * M_PI / 180;
    double a = pow(sin(dLat / 2), 2) + cos(lat * M_PI / 180) * cos(to.lat * M_PI / 180) * pow(sin(dLon / 2), 2);
    return 2 * 6371008.8 * asin(sqrt(a));
}

@implementation MSFPeakFinderExample {
    id<MSFExampleHost> _host;
    MSFMassifMap *_map;
    MSFPropertyGroup *_terrain;
    MSFMassifObject *_view;
    MSFMassifSource *_dem;
    MSFPeakFinderSky *_sky;
    MSFSubscription *_peakClick;
    NSString *_summitsCss;
    // The viewpoint: Grenoble, 400 m up, looking east at Belledonne - rotation is minus the heading.
    double _lat, _lon, _eye, _rotation, _tilt, _ground;
    BOOL _hours;
    int _generation;
    NSString *_selectedName;
    NSString *_selectedEle;
    MSFPosition *_selectedPosition;
    NSString *_shown;
}

+ (NSString *)exampleId {
    return @"peak-finder";
}

- (void)startWithHost:(id<MSFExampleHost>)host {
    _host = host;
    _map = host.map;
    _lat = 45.1885;
    _lon = 5.7245;
    _eye = 400;
    _rotation = -100;
    _tilt = -4;
    _shown = @"";
    _summitsCss = resource(@"summits", @"mss");
    MSFMassifMap *map = _map;

    _dem = [map source:@"dem"
                  spec:[[[[MSFSpec of:@"persistent-cache"]
                      set:@"databasePath" value:[host cachePath:@"mapterhorn-dem.db"]]
                      set:@"capacity" value:@(200 * 1024 * 1024)]
                      set:@"source" value:[[[[MSFSpec of:@"http"]
                          set:@"url" value:@"https://tiles.mapterhorn.com/{z}/{x}/{y}.webp"]
                          set:@"maxZoom" value:@16]
                          set:@"metaData" value:[[MSFSpec object] set:@"dem_encoding" value:@"terrarium"]]]
                 error:nil];

    // The terrain, and everything decided when its meshes are built: geo-three's cut (subdivide
    // distance 70, levels to 17) and mesh, no stitching, drawn 173 km out. The picture is the
    // surface shader, so nothing is draped over it.
    MSFSpec *terrainSpec = [[MSFSpec of:@"terrain"] set:@"source" value:@"dem"];
    NSDictionary *terrainValues = @{
        @"autoFlattenTilt": @0, @"autoFlattenParallax": @0, @"meshResolution": @171,
        @"tileEdgeStitchingEnabled": @NO, @"subdivideDistance": @70, @"maxZoom": @17,
        @"viewDistance": @173000, @"meshCacheSize": @640, @"normalSampleDistance": @40,
        @"postProcessDownscale": @1, @"surfaceShaderSource": resource(@"relief-surface", @"glsl"),
        @"backgroundColor": @"#ffffff", @"sharedGroundEnabled": @NO, @"drapeFillsEnabled": @NO,
        @"drapeLinesEnabled": @NO, @"billboardOcclusionEnabled": @YES,
        @"billboardOcclusionTolerance": @0.15, @"maxTileZoomCoarsening": @4,
    };
    for (NSString *key in terrainValues) {
        [terrainSpec set:key value:terrainValues[key]];
    }
    _terrain = [map terrainWithSpec:terrainSpec error:nil];
    [surface() enumerateKeysAndObjectsUsingBlock:^(NSString *name, NSNumber *value, BOOL *stop) {
        [[map.options call:@"terrainOptions.setSurfaceParameter" args:@[ name, value ] error:nil] destroy];
    }];

    // The ink is a post-process over the frame, reading the terrain's depth. The renderer hangs off
    // the map VIEW, which the map registered under its own id.
    MSFMassifObject *effect = [map object:@"effect" objectId:@"relief"
                                     spec:[[[[MSFSpec of:@"postprocess"] set:@"name" value:@"relief"]
                                         set:@"fragmentShader" value:resource(@"relief-ink", @"glsl")]
                                         set:@"terrainDepthRequired" value:@YES]
                                    error:nil];
    [ink() enumerateKeysAndObjectsUsingBlock:^(NSString *name, NSNumber *value, BOOL *stop) {
        [[effect call:@"setFloatParameter" args:@[ name, value ] error:nil] destroy];
    }];
    _view = [MSFMassif find:@"view" objectId:map.options.objectId];
    [_view set:@"mapRenderer.postProcessEffect" value:effect];

    // No sky, no background pattern: the panorama is read against the paper.
    [map apply:[[[[MSFSpec object] set:@"skyColor" value:@"#00000000"] set:@"clearColor" value:@"#ffffff"]
                  set:@"labelPadding" value:@200]];
    [map set:@"backgroundBitmap" value:[NSNull null]];
    [map lightWithSpec:[[[MSFSpec of:@"light"] set:@"sunAzimuth" value:@315] set:@"sunAltitude" value:@45]
                 error:nil];

    // First person: the position IS the eye and a drag turns the view about it. A panorama looks at
    // the horizon, so the tilt range opens above it too (tilt 90 is straight down).
    [map apply:[[[MSFSpec object] set:@"freeRoamMode" value:@"FREE_ROAM_MODE_FIRST_PERSON"]
                  set:@"tiltRange" value:@[ @-90, @90 ]]];
    [self setFov:46];
    [self placeCamera];

    // THE SUMMIT NAMES: OpenMapTiles' mountain_peak, and a style that puts every name in one row
    // above the skyline. The sky's path goes under them, the sun and its times over them.
    [map source:@"peaks"
           spec:[[[[MSFSpec of:@"persistent-cache"]
               set:@"databasePath" value:[host cachePath:@"openfreemap.db"]]
               set:@"capacity" value:@(100 * 1024 * 1024)]
               set:@"source" value:[[[[MSFSpec of:@"http"]
                   set:@"url" value:@"https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf"]
                   set:@"maxZoom" value:@14]
                   set:@"HTTPHeaders" value:[[MSFSpec object] set:@"User-Agent" value:kUserAgent]]]
          error:nil];
    MSFMassifLayer *skyBelow = [[map addLayer:@"sky" spec:[MSFSpec of:@"celestial"] error:nil] moveTo:0];
    MSFMassifLayer *skyAbove = [map addLayer:@"sky.top" spec:[MSFSpec of:@"celestial"] error:nil];
    _sky = [[MSFPeakFinderSky alloc] initWithMap:map below:skyBelow above:skyAbove];
    [self standOnGround];

    [host button:@"North" action:^{
        [self turnTo:0];
    }];
    [host button:@"Look at" action:^{
        if (self->_selectedPosition) {
            [self turnTo:-bearing(self->_lat, self->_lon, self->_selectedPosition)];
        }
    }];
    [host button:@"Fly to" action:^{
        if (self->_selectedPosition) {
            [self flyTo:self->_selectedPosition];
        }
    }];
    [host toggle:@"Hours" on:NO action:^(BOOL on) {
        self->_hours = on;
    }];
    [host slider:@"eye height, m" min:0 max:4000 value:_eye action:^(float metres) {
        self->_eye = metres;
        [self->_terrain set:@"focusLift" value:@(metres)];
    }];
    [host slider:@"field of view" min:5 max:120 value:46 action:^(float degrees) {
        [self setFov:degrees];
    }];
    [self tick];
}

- (void)setFov:(double)degrees {
    [_map set:@"fieldOfViewY" value:@(degrees)];
    // The ridge term's tap spacing: radians per screen pixel.
    UIView *view = _map.view;
    double pixels = MAX(view.bounds.size.height * view.contentScaleFactor, 1);
    [[_map.options call:@"terrainOptions.setSurfaceParameter"
                   args:@[ @"uPixelAngle", @(degrees * M_PI / 180 / pixels) ] error:nil] destroy];
}

/** The eye stands focusLift over the ground under it, which the renderer keeps every frame. */
- (void)placeCamera {
    [[_view call:@"moveCameraTo"
            args:@[ [MSFPosition positionWithLng:_lon lat:_lat], @13, @(_rotation), @(_tilt) ]
           error:nil] destroy];
    [_terrain set:@"focusLift" value:@(_eye)];
}

- (void)turnTo:(double)rotation {
    _rotation = rotation;
    _tilt = _map.camera.currentTilt;
    [self placeCamera];
}

/** Standing on the summit, facing the way it was seen from. */
- (void)flyTo:(MSFPosition *)peak {
    _rotation = -bearing(_lat, _lon, peak);
    _lat = peak.lat;
    _lon = peak.lng;
    [self selectName:nil ele:nil position:nil];
    [self placeCamera];
    [self standOnGround];
}

/**
 * Placed again once the ground is known: an eye placed before the viewpoint's elevation arrived
 * stays where it was put until the camera next moves.
 */
- (void)standOnGround {
    [self withGround:^{
        [self placeCamera];
        [self rebuildPeaks];
    }];
}

/**
 * A new style per viewpoint, since the eye's altitude is baked into the names' rank; the selected
 * summit is a style parameter, set on the live one.
 */
- (void)rebuildPeaks {
    _generation++;
    NSString *styleId = [NSString stringWithFormat:@"peaks.style.%d", _generation];
    NSString *css = [NSString stringWithFormat:@"@eye_elevation: %ld;\n@label_band: %.3f;\n%@",
                     lround(_ground + _eye), [self labelBand], _summitsCss];
    MSFMassifObject *style = [_map style:styleId
                                    spec:[[MSFSpec of:@"mbvt"] set:@"cartocss"
                                        value:[[MSFSpec of:@"cartocss"] set:@"css" value:css]]
                                   error:nil];
    [style set:@"params.selected_peak" value:[self selectedKey]];
    MSFMassifLayer *layer = [_map addLayer:[NSString stringWithFormat:@"peaks.layer.%d", _generation]
                                      spec:[[[[[[MSFSpec of:@"vector"] set:@"source" value:@"peaks"]
                                          set:@"style" value:styleId] set:@"preloading" value:@YES]
                                          set:@"labelRenderOrder" value:@"VECTOR_TILE_RENDER_ORDER_LAST"]
                                          set:@"tileSubstitutionPolicy" value:@"TILE_SUBSTITUTION_POLICY_VISIBLE"]
                                     error:nil];
    [layer moveTo:_map.layerCount - 2];
    __weak MSFPeakFinderExample *weakSelf = self;
    _peakClick = [layer onFeatureClick:^(MSFVectorTileClickEvent *e) {
        NSString *name = [e property:@"name"];
        // The key is `[name] + '|' + [ele]` as the style writes it: a whole number without a decimal.
        double ele = [e propertyDouble:@"ele" defaultValue:NAN];
        if (name && e.position) {
            [weakSelf selectName:name ele:isnan(ele) ? nil : [@(ele) stringValue] position:e.position];
        }
    }];
    if (_generation > 1) {
        [_map removeLayer:[NSString stringWithFormat:@"peaks.layer.%d", _generation - 1]];
        [MSFMassif destroy:@"style" objectId:[NSString stringWithFormat:@"peaks.style.%d", _generation - 1]];
    }
}

/**
 * The row the names hang from, low enough for a name wrapped at 70 px, 13 px text at 45 degrees, to
 * fit under the bar - the Alpimaps app's row (look.mjs labelBand).
 */
- (double)labelBand {
    double height = _map.view.bounds.size.height;
    double width = 70 + 13 * 2.5 + 10;
    double row = width * sin(M_PI / 4) + (13 * 2.6 + 4) * cos(M_PI / 4) + 10;
    return height > 0 ? MIN(0.9, (_host.topInset + row) / height) : 0.2;
}

- (NSString *)selectedKey {
    return _selectedName ? [NSString stringWithFormat:@"%@|%@", _selectedName, _selectedEle ?: @""] : @"";
}

- (void)selectName:(NSString *)name ele:(NSString *)ele position:(MSFPosition *)position {
    _selectedName = name;
    _selectedEle = ele;
    _selectedPosition = position;
    [[MSFMassif style:[NSString stringWithFormat:@"peaks.style.%d", _generation]]
        set:@"params.selected_peak" value:[self selectedKey]];
    _shown = @"";
}

/** The heading, the rise and set, and the selected summit, refreshed as the view turns. */
- (void)tick {
    _rotation = _map.camera.currentRotation;
    _tilt = _map.camera.currentTilt;
    NSString *riseSet = [_sky updateLat:_lat lon:_lon eye:_eye hours:_hours];
    double heading = fmod(fmod(-_rotation, 360) + 360, 360);
    NSString *text = [NSString stringWithFormat:@"%ld° %@  ·  %@", lround(heading) % 360,
                      [MSFPeakFinderSky compass:heading], riseSet];
    if (_selectedPosition) {
        double toPeak = fmod(bearing(_lat, _lon, _selectedPosition) + 360, 360);
        text = [text stringByAppendingFormat:@"\n%@  ·  %@%.1f km  ·  %ld° %@", _selectedName,
                _selectedEle ? [_selectedEle stringByAppendingString:@" m  ·  "] : @"",
                distance(_lat, _lon, _selectedPosition) / 1000, lround(toPeak), [MSFPeakFinderSky compass:toPeak]];
    } else {
        text = [text stringByAppendingString:@"\nDrag to look around, tap a summit name."];
    }
    if (![text isEqualToString:_shown]) {
        _shown = text;
        [_host caption:text];
    }
    [_host after:0.2 run:^{
        [self tick];
    }];
}

/** The ground under the viewpoint, off the DEM tile itself: the names rank by the eye's altitude. */
- (void)withGround:(void (^)(void))then {
    int zoom = 12;
    double tiles = 1 << zoom;
    double x = (_lon + 180) / 360 * tiles;
    double phi = _lat * M_PI / 180;
    double y = (1 - log(tan(phi) + 1 / cos(phi)) / M_PI) / 2 * tiles;
    [_dem loadTileX:(int)x y:(int)y zoom:zoom completion:^(NSData *data) {
        UIImage *image = data ? [UIImage imageWithData:data] : nil;
        if (image.CGImage) {
            size_t width = CGImageGetWidth(image.CGImage), height = CGImageGetHeight(image.CGImage);
            unsigned char pixel[4] = { 0 };
            CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
            CGContextRef context = CGBitmapContextCreate(pixel, 1, 1, 8, 4, space,
                                                         (CGBitmapInfo)kCGImageAlphaNoneSkipLast);
            // One pixel of context, with the image shifted so the wanted texel lands on it.
            CGContextDrawImage(context, CGRectMake(-floor(fmod(x, 1) * width),
                                                   -(height - 1 - floor(fmod(y, 1) * height)), width, height),
                               image.CGImage);
            CGContextRelease(context);
            CGColorSpaceRelease(space);
            self->_ground = pixel[0] * 256 + pixel[1] + pixel[2] / 256.0 - 32768;
        }
        then();
    }];
}

@end
