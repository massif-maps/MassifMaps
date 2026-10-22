#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * A weather radar loop: one tile per time step, two steps mixed in a single draw.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/sources/AnimatedRadarExample.java.
 */
@interface MSFAnimatedRadarExample : NSObject <MSFExample>
@end

@implementation MSFAnimatedRadarExample {
    id<MSFExampleHost> _host;
    MSFMassifMap *_map;
    NSArray<NSNumber *> *_times;
    NSTimer *_clock;
    BOOL _playing;
    float _stepsPerSecond;
    float _position;
    CFTimeInterval _lastTime;
}

+ (NSString *)exampleId {
    return @"animated-radar";
}

/** LibreWXR serves RainViewer's API: the steps of the loop, then a tile URL per step. */
static NSString * const kSteps = @"https://api.librewxr.net/public/weather-maps.json";
static NSString * const kTiles = @"https://api.librewxr.net/v2/radar/{time}/256/{z}/{x}/{y}/2/1_1.png";

/** A tile server wants to know who is asking: a real app identifies itself. */
static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

- (void)startWithHost:(id<MSFExampleHost>)host {
    _host = host;
    _map = host.map;
    _playing = YES;
    _stepsPerSecond = 2;

    // The ground, set up BEFORE any layer decodes and left flat: the 3D switch below is the `flattened`
    // flag, so a map that never goes 3D builds not one tile for it. Mapterhorn's open DEM.
    MSFSpec *demHttp = [MSFSpec of:@"http"];
    [demHttp set:@"url" value:@"https://tiles.mapterhorn.com/{z}/{x}/{y}.webp"];
    [demHttp set:@"minZoom" value:@1];
    [demHttp set:@"maxZoom" value:@16];
    // What picks the elevation decoder, per tile; on the HTTP source, not the cache in front of it.
    [demHttp set:@"metaData" value:[[MSFSpec object] set:@"dem_encoding" value:@"terrarium"]];
    MSFSpec *dem = [MSFSpec of:@"persistent-cache"];
    [dem set:@"databasePath" value:[host cachePath:@"mapterhorn-dem.db"]];
    [dem set:@"capacity" value:@(200 * 1024 * 1024)];
    [dem set:@"source" value:demHttp];
    [[_map terrainWithSpec:[[MSFSpec of:@"terrain"] set:@"source" value:dem] error:nil]
        apply:[[[[[[MSFSpec object] set:@"flattened" value:@YES]
            set:@"exaggeration" value:@1.5]
            set:@"flattenMode" value:@"TERRAIN_FLATTEN_MODE_FULL"]
            set:@"viewDistanceFactor" value:@1.6]
            set:@"cameraClearance" value:@40]];
    [_map skyWithSpec:[MSFSpec of:@"sky"] error:nil];
    [_map fogWithSpec:[[[MSFSpec of:@"fog"] set:@"rangeStart" value:@2.2] set:@"rangeEnd" value:@8] error:nil];
    [_map lightWithSpec:[[[[[MSFSpec of:@"light"] set:@"terrainLightingEnabled" value:@NO]
        set:@"sunAzimuth" value:@315] set:@"sunAltitude" value:@42] set:@"shadowStrength" value:@0] error:nil];

    // The Massif streets style (styles/massif/carto, bundled by the build as styles/massif.zip) over
    // OpenFreeMap's vector tiles, cached on disk: a free service a demo would re-fetch every run.
    [_map addLayer:@"basemap"
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
    [_map.camera moveTo:[MSFPosition positionWithLng:4.5 lat:47.0] zoom:5.5];
    [host caption:@"Loading the radar steps..."];

    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:kSteps]];
    [request setValue:kUserAgent forHTTPHeaderField:@"User-Agent"];
    [[[NSURLSession sharedSession] dataTaskWithRequest:request completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
        NSDictionary *radar = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil][@"radar"] : nil;
        NSMutableArray<NSNumber *> *times = [NSMutableArray array];
        for (NSString *key in @[@"past", @"nowcast"]) {
            for (NSDictionary *step in radar[key]) {
                [times addObject:step[@"time"]];
            }
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!times.count) {
                [host caption:[NSString stringWithFormat:@"The radar is not reachable: %@", error.localizedDescription ?: @"no steps"]];
                return;
            }
            [self addRadar:times];
        });
    }] resume];
}

- (void)addRadar:(NSArray<NSNumber *> *)times {
    _times = times;
    NSMutableArray<NSString *> *timestamps = [NSMutableArray array];
    for (NSNumber *time in times) {
        [timestamps addObject:time.stringValue];
    }

    // The memory cache keeps a step fetched once: it is the second raster of one tile and the first
    // of the next, and its default 6 MB holds a third of a loop. prefetchFrames = every step, so the
    // loop does not stall on its first pass.
    MSFSpec *radar = [MSFSpec of:@"animated-raster"];
    // The server renders a tile on demand and takes seconds over it, so a tile fetched once is kept - for
    // the 5 minutes its Cache-Control gives: a forecast step is revised, and must not be shown later as
    // an observation.
    MSFSpec *radarHttp = [MSFSpec of:@"http"];
    [radarHttp set:@"url" value:kTiles];
    [radarHttp set:@"maxZoom" value:@12];
    [radarHttp set:@"maxAgeHeaderCheck" value:@YES];
    MSFSpec *radarDisk = [MSFSpec of:@"persistent-cache"];
    [radarDisk set:@"databasePath" value:[_host cachePath:@"librewxr-radar.db"]];
    [radarDisk set:@"capacity" value:@(100 * 1024 * 1024)];
    [radarDisk set:@"source" value:radarHttp];
    MSFSpec *radarMemory = [MSFSpec of:@"memory-cache"];
    [radarMemory set:@"capacity" value:@(64 * 1024 * 1024)];
    [radarMemory set:@"source" value:radarDisk];
    [radar set:@"source" value:radarMemory];
    // Radar is coarse data: one level fewer is a quarter of the tiles to wait for.
    [radar set:@"zoomLevelBias" value:@(-1)];
    [radar set:@"frameTimestamps" value:timestamps];
    [radar set:@"prefetchFrames" value:@(timestamps.count)];
    [radar set:@"opacity" value:@0.85];
    [_map addLayer:@"radar" spec:radar error:nil];

    __weak MSFAnimatedRadarExample *weakSelf = self;
    [_host toggle:@"Play" on:YES action:^(BOOL on) { [weakSelf setPlaying:on]; }];
    [_host toggle:@"3D" on:NO action:^(BOOL on) { [weakSelf set3D:on]; }];
    [_host slider:@"Time" min:0 max:1 value:0 action:^(float value) { [weakSelf scrubTo:value]; }];
    [_host slider:@"Steps a second" min:0.5 max:6 value:_stepsPerSecond action:^(float value) { [weakSelf setSpeed:value]; }];

    _lastTime = CACurrentMediaTime();
    _clock = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60 repeats:YES block:^(NSTimer *timer) {
        [weakSelf tick];
    }];
}

/** Where each state looks: all of Europe flat, the Alps from the north in 3D. tilt 90 is straight down in this SDK. */
- (void)set3D:(BOOL)on {
    [_map.terrain set:@"flattened" value:@(!on)];
    [_map.light set:@"terrainLightingEnabled" value:@(on)];
    [[_map.camera animate:1.5] moveTo:[MSFPosition positionWithLng:on ? 7.2 : 4.5 lat:on ? 46.2 : 47.0]
                                 zoom:on ? 9 : 5.5 rotation:on ? 180 : 0 tilt:on ? 35 : 90];
}

- (void)setPlaying:(BOOL)playing {
    _playing = playing;
}

- (void)setSpeed:(float)stepsPerSecond {
    _stepsPerSecond = stepsPerSecond;
}

- (void)scrubTo:(float)fraction {
    _position = fraction * (_times.count - 1);
    [self show];
}

/** The clock: advances the position by the time since the last frame, at the chosen rate. */
- (void)tick {
    CFTimeInterval now = CACurrentMediaTime();
    float seconds = (float)(now - _lastTime);
    _lastTime = now;
    if (_playing && _times.count) {
        _position = fmodf(_position + seconds * _stepsPerSecond, (float)_times.count);
        [_host setSlider:@"Time" value:_times.count > 1 ? _position / (_times.count - 1) : 0];
        [self show];
    }
}

- (void)show {
    [[_map layer:@"radar"] set:@"animationPosition" value:@(_position)];
    NSUInteger step = MIN(_times.count - 1, (NSUInteger)_position);
    NSTimeInterval seconds = _times[step].doubleValue;
    NSDateFormatter *format = [[NSDateFormatter alloc] init];
    format.dateFormat = @"HH:mm";
    format.timeZone = [NSTimeZone timeZoneForSecondsFromGMT:0];
    BOOL forecast = seconds > [NSDate date].timeIntervalSince1970;
    [_host caption:[NSString stringWithFormat:@"%@ UTC%@", [format stringFromDate:[NSDate dateWithTimeIntervalSince1970:seconds]],
                    forecast ? @"  forecast" : @""]];
}

- (void)stop {
    [_clock invalidate];
    _clock = nil;
}

@end
