#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * Switching between a flat map and a 3D view as ONE animation: the camera flies while the terrain
 * rises under it.
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
    BOOL _animating;
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
static const float kFlightSeconds = 2.5f;
/** How often the terrain height is stepped to follow the flight. */
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
    _animating = NO;

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
    // Built flat: terrain off, and the height at 0 so the first rise starts from nothing. The
    // automatic flattening has to be out of the way, or it flattens the map again the moment the
    // tilt passes 88.
    [terrain apply:[[[[[[MSFSpec object]
        set:@"enabled" value:@NO]
        set:@"exaggeration" value:@0]
        set:@"autoFlattenTilt" value:@0]
        set:@"autoFlattenParallax" value:@0]
        set:@"cameraClearance" value:@40]];

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
    [host caption:@"Flat, top-down. Tap to rise into the terrain."];
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
    if (_animating) {
        return;
    }
    _animating = YES;
    if (_in3D) {
        [self flattenTo2D];
    } else {
        [self riseTo3D];
    }
}

- (void)riseTo3D {
    // The flight goes FIRST. Turning the terrain on clears every tile cache, and that re-decode
    // landing on the flight's frame zero starved it of frames - the first switch jumped while every
    // later one animated, because only the first one is cold. Started a tick later it lands during
    // the rise instead, where the exaggeration is still near 0 and a flat-decoded tile renders
    // exactly like the 2D map. The flight is never made to wait
    // (https://github.com/massif-maps/MassifMaps/issues/177 removes the re-decode entirely).
    [[_map.camera animate:kFlightSeconds]
        moveTo:[MSFPosition positionWithLng:kSummitLng lat:kSummitLat]
          zoom:kZoom rotation:kRotation tilt:kTilt3D];
    __weak __typeof(self) enableSelf = self;
    [_host after:kTick run:^{
        [enableSelf->_map.terrain set:@"enabled" value:@YES];
    }];
    // The terrain follows the FLIGHT rather than a clock of its own, so the two cannot drift apart
    // if the flight is interrupted or the frame rate drops.
    [self ramp:YES];
    [_host caption:@"Rising. The terrain follows the flight, not a separate clock."];
}

- (void)flattenTo2D {
    // Where the camera IS, not what it is looking at: at tilt 20 the focus is kilometres out in
    // front, so re-centring on it would jump the map forward. This is the viewpoint.
    MSFPosition *eye = _map.camera.currentEyePosition;
    [[_map.camera animate:kFlightSeconds] moveTo:eye zoom:kZoom rotation:kRotation tilt:kTilt2D];
    [self ramp:NO];
    [_host caption:@"Back down, centred on where the camera was standing."];
}

/** Steps the terrain height with the flight's own progress, and settles when it lands. */
- (void)ramp:(BOOL)rising {
    __weak __typeof(self) weakSelf = self;
    [_host after:kTick run:^{
        __typeof(self) self_ = weakSelf;
        if (!self_) {
            return;
        }
        if (self_->_map.camera.isMoving) {
            float progress = self_->_map.camera.progress;
            [self_->_map.terrain set:@"exaggeration" value:@(rising ? progress : 1 - progress)];
            [self_ ramp:rising];
            return;
        }
        [self_->_map.terrain set:@"exaggeration" value:@(rising ? 1 : 0)];
        if (!rising) {
            // Only now, with the map already flat: flipping the flag re-decodes every tile, and at
            // exaggeration 0 there is nothing of that to see.
            [self_->_map.terrain set:@"enabled" value:@NO];
        }
        self_->_in3D = rising;
        self_->_animating = NO;
        [self_->_host caption:rising ? @"3D. Tap to go back to the flat map."
                                     : @"Flat, top-down. Tap to rise into the terrain."];
    }];
}

@end
