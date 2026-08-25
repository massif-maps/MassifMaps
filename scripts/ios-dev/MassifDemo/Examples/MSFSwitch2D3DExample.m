#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * The 2D/3D switch, and every way of driving it: the SDK's own animation, a tilt gesture, and the
 * app's own clock for an exact match to a camera flight.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/terrain/Switch2D3DExample.java.
 */
@interface MSFSwitch2D3DExample : NSObject <MSFExample>
@end

@implementation MSFSwitch2D3DExample {
    __weak id<MSFExampleHost> _host;
    MSFMassifMap *_map;
    BOOL _in3D;
    BOOL _autoByTilt;
    BOOL _matchFlight;
    float _seconds;
}

+ (NSString *)exampleId {
    return @"terrain-2d-3d";
}

static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

/** What the 3D view looks AT. The viewpoint it is seen from is derived - see frameFlatStart. */
static const double kSummitLng = 7.6586, kSummitLat = 45.9763;
static const float kZoom = 12.5f;
/** One rotation for both states: north up flat, looking north tilted. A switch that also spun the
 *  map 180 degrees made it impossible to tell where you had come out. */
static const float kRotation = 0.0f;
/** tilt 90 is straight down in this SDK, so 2D is 90 and a landscape view is a LOW tilt. */
static const float kTilt2D = 90.0f;
static const float kTilt3D = 20.0f;
/** The tilt the auto rule switches at, and its default. */
static const float kAutoTilt = 88.0f;
/** How often the matched ramp samples the flight. */
static const NSTimeInterval kTick = 0.032;

static MSFSpec *dem(id<MSFExampleHost> host) {
    return [[[[MSFSpec of:@"persistent-cache"]
        // Same database as terrain-3d: the two examples warm each other's cache.
        set:@"databasePath" value:[host cachePath:@"mapterhorn-dem.db"]]
        set:@"capacity" value:@(200 * 1024 * 1024)]
        set:@"source" value:[[[[[MSFSpec of:@"http"]
            set:@"url" value:@"https://tiles.mapterhorn.com/{z}/{x}/{y}.webp"]
            set:@"minZoom" value:@1]
            set:@"maxZoom" value:@16]
            // Picks the elevation decoder per TILE. Without it the SDK assumes mapbox encoding, and
            // mapbox-decoding terrarium tiles gives heights in the hundreds of kilometres.
            set:@"metaData" value:[[MSFSpec object] set:@"dem_encoding" value:@"terrarium"]]];
}

- (void)startWithHost:(id<MSFExampleHost>)host {
    _host = host;
    _map = host.map;
    _in3D = NO;
    _autoByTilt = NO;
    _matchFlight = NO;
    _seconds = 2.5f;

    [_map addLayer:@"basemap"
              spec:[[MSFSpec of:@"raster"]
                      set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
                          set:@"databasePath" value:[host cachePath:@"osm-raster.db"]]
                          set:@"capacity" value:@(100 * 1024 * 1024)]
                          set:@"source" value:[[[[MSFSpec of:@"http"]
                              set:@"url" value:@"https://tile.openstreetmap.org/{z}/{x}/{y}.png"]
                              set:@"maxZoom" value:@19]
                              // OSM's tile policy REQUIRES an identifying User-Agent, or every tile
                              // is a 403.
                              set:@"HTTPHeaders" value:[[MSFSpec object]
                                  set:@"User-Agent" value:kUserAgent]]]]
             error:nil];

    MSFPropertyGroup *terrain =
        [_map terrainWithSpec:[[MSFSpec of:@"terrain"] set:@"source" value:dem(host)] error:nil];
    // Configured and left on. The switch is `flattened`, and it opens flat - set BEFORE any layer
    // decodes, so not one tile is built for a 3D the map has not shown. `flattenMode` FULL is the
    // whole way: a flat map decodes and culls as if no terrain were attached. The auto rule is off
    // to start with, so the button below is the only thing switching.
    [terrain apply:[[[[[[[MSFSpec object]
        set:@"enabled" value:@YES]
        set:@"flattened" value:@YES]
        set:@"flattenMode" value:@"TERRAIN_FLATTEN_MODE_FULL"]
        set:@"autoFlattenTilt" value:@0]
        set:@"autoFlattenParallax" value:@0]
        set:@"cameraClearance" value:@40]];
    [self applySeconds:_seconds];

    [_map skyWithSpec:[MSFSpec of:@"sky"] error:nil];
    [_map fogWithSpec:[[[MSFSpec of:@"fog"] set:@"rangeStart" value:@2.2]
                        set:@"rangeEnd" value:@8] error:nil];
    // The sun comes from BEHIND the camera or the face being looked at is the one in shadow. This
    // view is of the SOUTH side, so the light is south.
    [_map lightWithSpec:[[[[MSFSpec of:@"light"]
        set:@"terrainLightingEnabled" value:@YES]
        set:@"sunAzimuth" value:@170]
        set:@"sunAltitude" value:@42] error:nil];

    [self frameFlatStart];

    __weak __typeof(self) weakSelf = self;
    [host button:@"2D / 3D" action:^{
        [weakSelf toggle];
    }];
    [host slider:@"seconds" min:0 max:6 value:_seconds action:^(float value) {
        __typeof(self) self_ = weakSelf;
        self_->_seconds = value;
        [self_ applySeconds:value];
    }];
    [host toggle:@"Match flight" on:NO action:^(BOOL on) {
        __typeof(self) self_ = weakSelf;
        self_->_matchFlight = on;
        [self_->_host caption:on
            ? @"Matched: the terrain reads the flight's own progress, so the two cannot drift."
            : @"Timed: two clocks of the same length. Close, but not the same clock."];
    }];
    [host toggle:@"Full switch" on:YES action:^(BOOL on) {
        __typeof(self) self_ = weakSelf;
        [self_->_map.terrain set:@"flattenMode"
                            value:on ? @"TERRAIN_FLATTEN_MODE_FULL" : @"TERRAIN_FLATTEN_MODE_RENDER"];
        [self_->_host caption:on
            ? @"FULL: flat costs nothing, each switch re-decodes the visible tiles."
            : @"RENDER: switching is free, but flat still carries 3D's triangles."];
    }];
    [host toggle:@"Auto by tilt" on:NO action:^(BOOL on) {
        __typeof(self) self_ = weakSelf;
        self_->_autoByTilt = on;
        [self_->_map.terrain set:@"autoFlattenTilt" value:@(on ? kAutoTilt : 0.0f)];
        [self_->_host caption:on
            ? @"Auto on: tilt with two fingers and it switches itself. The button still leads - "
               "the rule only fires when the tilt CROSSES 88."
            : @"Auto off: only the button switches."];
    }];
    [host caption:[self flatCaption]];
}

/** One number for both animations, which is what makes them the same length. */
- (void)applySeconds:(float)value {
    [_map.terrain apply:[[[MSFSpec object]
        set:@"autoFlattenDuration" value:@(value)]
        // Timed apart from the sinking one: this is the direction that waited for its tiles.
        set:@"autoFlattenRiseDuration" value:@(value)]];
}

/**
 * Opens the flat map exactly where a round trip through 3D lands, which is what makes the first
 * flight identical to every later one.
 *
 * That place cannot be a constant: the camera stands cameraDistance * cos(tilt) from its focus, and
 * cameraDistance comes from the viewport, so it differs per screen. So put the camera where 3D would
 * put it, ask where that left it standing, and drop to top-down there.
 */
- (void)frameFlatStart {
    [_map.camera moveTo:[MSFPosition positionWithLng:kSummitLng lat:kSummitLat]
                   zoom:kZoom rotation:kRotation tilt:kTilt3D];
    [_map.camera moveTo:_map.camera.currentEyePosition zoom:kZoom rotation:kRotation tilt:kTilt2D];
}

- (void)toggle {
    if (_map.camera.isMoving) {
        [_host caption:@"Still flying - let it land first."];
        return;
    }
    // Read the SDK's state rather than count button presses. With auto by tilt on, the RULE owns
    // the state and a local flag drifts out of step with it - and then the button flies to the tilt
    // the map is already at, the rule never crosses its threshold, and nothing moves.
    _in3D = [_map.terrain getBool:@"flattened" defaultValue:YES];
    if (_matchFlight) {
        [self matched];
    } else {
        [self timed];
    }
}

/**
 * The SDK's own animation: ask for the state, and it ramps over autoFlattenDuration. Two timers of
 * the same length - which is close, and is all most apps need.
 */
- (void)timed {
    [self fly];
    // Written even with auto by tilt on: the rule fires on a THRESHOLD CROSSING, not every frame, so
    // it leaves an explicit ask alone and the terrain moves with the flight instead of waiting for
    // the tilt to reach 88.
    [_map.terrain set:@"flattened" value:@(!_in3D)];
    [_host caption:_in3D ? [self riseCaption] : [self flatCaption]];
}

/**
 * The app's own clock: feed the terrain the FLIGHT's progress, so the two cannot drift apart even if
 * the frame rate drops or the flight is interrupted.
 */
- (void)matched {
    if (!_in3D) {
        [self fly];             // sinking has nothing to wait for
        [self rampWithFlight];
        [_host caption:@"Sinking on the flight's own clock."];
        return;
    }
    // Rising does. Ask for 3D so its tiles start loading, and let the flight go only once the switch
    // stops holding the ground flat - driving the ratio up before then would be held anyway, and the
    // animation would start with a jump.
    [_map.terrain set:@"flattened" value:@NO];
    [_host caption:@"Loading the tiles 3D needs before the flight starts."];
    [self waitForTiles];
}

- (void)waitForTiles {
    __weak __typeof(self) weakSelf = self;
    [_host after:kTick run:^{
        __typeof(self) self_ = weakSelf;
        if (!self_) {
            return;
        }
        if ([self_->_map.terrain getBool:@"switching" defaultValue:NO]) {
            [self_ waitForTiles];
            return;
        }
        [self_ fly];
        [self_ rampWithFlight];
        [self_->_host caption:@"Rising on the flight's own clock."];
    }];
}

- (void)fly {
    // Where the camera IS, not what it is looking at: at tilt 20 the focus is kilometres out in
    // front, so re-centring on it would jump the map forward.
    MSFPosition *target = _in3D ? [MSFPosition positionWithLng:kSummitLng lat:kSummitLat]
                                : _map.camera.currentEyePosition;
    [[_map.camera animate:_seconds]
        moveTo:target zoom:kZoom rotation:kRotation tilt:_in3D ? kTilt3D : kTilt2D];
}

/** Writing flattenRatio takes the ramp off the SDK's timer and puts it on the flight's. */
- (void)rampWithFlight {
    __weak __typeof(self) weakSelf = self;
    [_host after:kTick run:^{
        __typeof(self) self_ = weakSelf;
        if (!self_) {
            return;
        }
        if (self_->_map.camera.isMoving) {
            float progress = self_->_map.camera.progress;
            [self_->_map.terrain set:@"flattenRatio" value:@(self_->_in3D ? 1 - progress : progress)];
            [self_ rampWithFlight];
            return;
        }
        [self_->_map.terrain set:@"flattenRatio" value:@(self_->_in3D ? 0 : 1)];
        // Hand the ratio back, or the switch stays MANUAL - which also keeps auto-flattening
        // suspended, and a tilt gesture would then do nothing.
        [self_->_map.terrain set:@"flattened" value:@(!self_->_in3D)];
        [self_->_host caption:self_->_in3D ? [self_ riseCaption] : [self_ flatCaption]];
    }];
}

- (NSString *)flatCaption {
    return _autoByTilt ? @"Flat. Tilt, or tap, to rise into the terrain."
                       : @"Flat, top-down. Tap to rise into the terrain.";
}

- (NSString *)riseCaption {
    return _autoByTilt ? @"3D - the tilt asked for it, not the button."
                       : @"3D. The SDK waited for its tiles before lifting the ground.";
}

@end
