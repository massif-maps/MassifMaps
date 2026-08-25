#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * Everything the sky and the fog can do, on one map: scattering, a day cycle, stars, and peaks
 * standing clear of a valley haze.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/terrain/AtmosphereExample.java.
 */
@interface MSFAtmosphereExample : NSObject <MSFExample>
@end

@implementation MSFAtmosphereExample {
    __weak id<MSFExampleHost> _host;
    NSInteger _moment;
    BOOL _cycling;
    BOOL _atmosphere;
    BOOL _customSky;
    double _hour;
}

+ (NSString *)exampleId {
    return @"atmosphere";
}

/** Identifies the app to the tile servers, which both of them ask for. */
static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

/**
 * One row per hour of interest: label, hour, and the colours the atmosphere is not responsible
 * for - the FOG's own tint and the sky exposure. Everything else (sun position, the light on the
 * ground, the colour the haze is lit to) follows from the hour.
 */
static NSArray *moments(void) {
    //         label     hour   fog colour    high colour   space colour  stars  exposure
    return @[ @[ @"Dawn",  @6.5,  @0xffd8b48c, @0xffe08a5a, @0x66202a4a, @0.25, @1.6 ],
              @[ @"Noon",  @13.0, @0xffb8c6d8, @0x00000000, @0x00000000, @0.00, @1.0 ],
              @[ @"Dusk",  @19.5, @0xffc98a63, @0xffe06a3a, @0x88141c38, @0.35, @1.8 ],
              @[ @"Night", @23.0, @0xff1a2338, @0xff101a34, @0xff05070f, @0.90, @3.2 ] ];
}

/** Open DEM tiles, terrarium-encoded, cached on disk in front of the server. */
static MSFSpec *dem(id<MSFExampleHost> host) {
    return [[[[MSFSpec of:@"persistent-cache"]
        set:@"databasePath" value:[host cachePath:@"mapterhorn-dem.db"]]
        set:@"capacity" value:@(200 * 1024 * 1024)]
        set:@"source" value:[[[[[MSFSpec of:@"http"]
            set:@"url" value:@"https://tiles.mapterhorn.com/{z}/{x}/{y}.webp"]
            set:@"minZoom" value:@1]
            set:@"maxZoom" value:@16]
            // What picks the elevation decoder, per tile - see MSFTerrain3DExample for why this is
            // the whole map rather than a dotted path.
            set:@"metaData" value:[[MSFSpec object] set:@"dem_encoding" value:@"terrarium"]]];
}

- (void)startWithHost:(id<MSFExampleHost>)host {
    _host = host;
    _moment = 2; // start at dusk: it is what shows the scattering off best
    _hour = 19.5;
    _atmosphere = YES;
    MSFMassifMap *map = host.map;

    [map addLayer:@"satellite"
             spec:[[MSFSpec of:@"raster"]
                     // Cached on disk in front of the server: imagery tiles are big, and a demo
                     // that gets panned around re-fetches the same ones on every run.
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
                  set:@"project" value:[[MSFSpec of:@"project"]
                      set:@"assets" value:[[MSFSpec of:@"zip"]
                          set:@"data" value:[[MSFSpec of:@"url"]
                              set:@"url" value:@"assets://styles/hybrid.zip"]]]]
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

    MSFPropertyGroup *terrain =
        [map terrainWithSpec:[[MSFSpec of:@"terrain"] set:@"source" value:dem(host)] error:nil];
    [terrain apply:[[[[MSFSpec object]
        set:@"exaggeration" value:@1.25]
        set:@"viewDistanceFactor" value:@1.6]
        set:@"cameraClearance" value:@40]];

    // The sky. ATMOSPHERE is the default, so this spec only names what differs from it - the
    // exposure the moment wants is set below, with everything else the hour drives.
    [map skyWithSpec:[[MSFSpec of:@"sky"] set:@"sunDiscEnabled" value:@YES] error:nil];

    // The fog. Its range is in multiples of the camera-to-focus distance, so one pair holds at
    // every zoom. horizonBlend is what carries the haze up into the sky - and the GROUND is scaled
    // by the same term, which is why the two meet along the skyline with no seam.
    [map fogWithSpec:[[[[[[MSFSpec of:@"fog"]
        set:@"rangeStart" value:@1.4]
        set:@"rangeEnd" value:@7.0]
        set:@"horizonBlend" value:@0.22]
        // The summits stand clear of the haze filling the valley (mapbox vertical-range).
        set:@"verticalRangeStart" value:@1800]
        set:@"verticalRangeEnd" value:@3200] error:nil];

    [map lightWithSpec:[[[[MSFSpec of:@"light"]
        set:@"terrainLightingEnabled" value:@YES]
        set:@"shadowStrength" value:@0.35]
        set:@"shadowSoftness" value:@1.5] error:nil];

    [self applyMoment];
    [map.camera moveTo:[MSFPosition positionWithLng:7.6586 lat:45.9763]
                  zoom:11.5 rotation:180 tilt:33];

    __weak MSFAtmosphereExample *weakSelf = self;
    [host button:@"Time" action:^{
        MSFAtmosphereExample *strong = weakSelf;
        strong->_moment = (strong->_moment + 1) % (NSInteger)moments().count;
        strong->_hour = [moments()[strong->_moment][1] doubleValue];
        [strong applyMoment];
    }];
    [host toggle:@"Run the day" on:NO action:^(BOOL on) {
        MSFAtmosphereExample *strong = weakSelf;
        strong->_cycling = on;
        if (on) {
            [strong step];
        }
    }];
    // The A/B for both the look and the cost: GRADIENT is the two-colour ramp the SDK drew before
    // the atmosphere, and it ignores every Atmosphere* property.
    [host toggle:@"Atmosphere" on:YES action:^(BOOL on) {
        MSFAtmosphereExample *strong = weakSelf;
        strong->_atmosphere = on;
        [strong applySky];
    }];
    [host toggle:@"Comets & clouds" on:NO action:^(BOOL on) {
        MSFAtmosphereExample *strong = weakSelf;
        strong->_customSky = on;
        [strong applySky];
        [strong applyMoment];
    }];
    [host toggle:@"Peaks above the fog" on:YES action:^(BOOL on) {
        // Equal values disable the vertical fade, so the haze fills the whole view again.
        [map.fog set:@"verticalRangeStart" value:on ? @1800 : @0];
        [map.fog set:@"verticalRangeEnd" value:on ? @3200 : @0];
    }];
    [host toggle:@"Fog" on:YES action:^(BOOL on) {
        // Enabled is a real switch: nothing has to be driven through zero and back.
        [map.fog set:@"enabled" value:@(on)];
    }];
}

- (void)stop {
    _cycling = NO;
}

/** Advances the clock while the toggle is on. The host cancels the callback for us. */
- (void)step {
    if (!_cycling) {
        return;
    }
    _hour = fmod(_hour + 0.25, 24.0);
    [self applyHour];
    __weak MSFAtmosphereExample *weakSelf = self;
    [_host after:0.1 run:^{
        [weakSelf step];
    }];
}

/** The picked moment: its hour, plus the colours the model does not derive. */
- (void)applyMoment {
    NSArray *m = moments()[_moment];
    [_host.map.fog apply:[[[[[MSFSpec object]
        set:@"color" value:m[2]]
        set:@"highColor" value:m[3]]
        set:@"spaceColor" value:m[4]]
        set:@"starIntensity" value:m[5]]];
    [_host.map.sky set:@"atmosphereLuminance" value:m[6]];
    [self applyHour];
    [_host caption:_customSky ? @"custom sky: clouds by day, comets once the sun is down"
                              : [NSString stringWithFormat:@"%@ - %@", m[0], captionFor(_moment)]];
}

/**
 * Everything the hour drives. Sun position from a deliberately crude model: this example is about
 * the sky, not about ephemerides, and the demo bench has the real one.
 *
 * The fog is NOT tinted here - resolveFog lights the configured colour with the same sun the ground
 * gets, so a fog tuned for daylight darkens through the night on its own.
 */
- (void)applyHour {
    double altitude = 62.0 * sin(M_PI * (_hour - 6.0) / 12.0);
    double azimuth = 90.0 + (_hour - 6.0) * 15.0;
    // Below the horizon there is no sun to light anything with, and the ambient is what keeps the
    // map readable at all.
    double sunUp = MAX(0.0, MIN(1.0, altitude / 8.0));
    [_host.map.light apply:[[[[[MSFSpec object]
        set:@"sunAzimuth" value:@(azimuth)]
        set:@"sunAltitude" value:@(altitude)]
        set:@"sunIntensity" value:@(0.15 + 0.85 * sunUp)]
        set:@"ambientIntensity" value:@(0.45 - 0.15 * sunUp)]
        set:@"shadowStrength" value:@(0.35 * sunUp)]];
}

/**
 * A custom sky: the SDK's own scattering, with a cloud deck lit by the sun and, once the sun is
 * down, comets crossing it.
 *
 * The contract is one function, vec4 skyColor(vec3 rayDir), returning the NON-premultiplied colour.
 * Everything it reads is already declared by the wrapper - redeclaring any of it is a compile error
 * - and it must NOT fog itself: the SDK applies the frame's own haze to whatever this returns, so
 * the sky still meets the ground at the skyline and a custom FOG shader still reaches it.
 */
static NSString *customSkyShader(void) {
    return [@[
        @"float hash21(vec2 p) {",
        @"  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);",
        @"}",
        @"float valueNoise(vec2 p) {",
        @"  vec2 i = floor(p), f = fract(p);",
        @"  f = f * f * (3.0 - 2.0 * f);",
        @"  return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x),",
        @"             mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x), f.y);",
        @"}",
        @"",
        @"// A flat cloud deck the ray is projected onto: cheap, and the perspective is right.",
        @"// The octaves are ROTATED against each other - stacking axis-aligned value noise",
        @"// leaves the grid visible, which reads as soft squares once the sun lights them.",
        @"float clouds(vec3 dir) {",
        @"  if (dir.z <= 0.02) return 0.0;",
        @"  vec2 p = dir.xy / dir.z * 1.7 + vec2(u_time * 0.004, 0.0);",
        @"  mat2 rot = mat2(0.80, -0.60, 0.60, 0.80);",
        @"  float f = 0.50 * valueNoise(p);",
        @"  p = rot * p * 2.1; f += 0.25 * valueNoise(p);",
        @"  p = rot * p * 2.3; f += 0.15 * valueNoise(p);",
        @"  p = rot * p * 2.7; f += 0.10 * valueNoise(p);",
        @"  // Thinned towards the horizon, where the deck would be edge-on and solid.",
        @"  return smoothstep(0.46, 0.74, f) * smoothstep(0.02, 0.25, dir.z);",
        @"}",
        @"",
        @"// Three comets, laid out in (azimuth, elevation) so a streak keeps its width near the",
        @"// horizon - a flat projection smears it into a band there. Each one crosses on its",
        @"// own stagger, head first, with the tail BEHIND it.",
        @"float comets(vec3 dir) {",
        @"  vec2 sky = vec2(atan(dir.y, dir.x), asin(clamp(dir.z, -1.0, 1.0)));",
        @"  float total = 0.0;",
        @"  for (int i = 0; i < 3; i++) {",
        @"    float fi = float(i);",
        @"    float t = fract(u_time * 0.07 + fi * 0.37);",
        @"    vec2 from = vec2(-2.6 + fi * 0.7, 1.25);",
        @"    vec2 to = vec2(1.4 + fi * 0.5, 0.18);",
        @"    vec2 head = mix(from, to, t);",
        @"    vec2 axis = normalize(to - from);",
        @"    vec2 d = sky - head;",
        @"    float along = dot(d, axis);",
        @"    float across = length(d - axis * along);",
        @"    float tail = exp(-across * across / 0.00012)",
        @"                 * smoothstep(-0.5, 0.0, along) * step(along, 0.0);",
        @"    float glow = exp(-dot(d, d) / 0.00006);",
        @"    // Faded in and out over the pass, so nothing pops at the edge of the frame.",
        @"    total += (tail * 0.55 + glow) * smoothstep(0.0, 0.12, t) * smoothstep(1.0, 0.86, t);",
        @"  }",
        @"  return total;",
        @"}",
        @"",
        @"vec4 skyColor(vec3 rayDir) {",
        @"  float elevation = asin(clamp(rayDir.z, -1.0, 1.0));",
        @"  // Below the horizon is the wedge between the last terrain tile and the mathematical",
        @"  // horizon. The wrapper has the right answer for it, coverage included.",
        @"  if (elevation < 0.0) return groundBelowHorizon(rayDir);",
        @"",
        @"  // The SDK's own scattering, tonemapped the way the built-in sky tonemaps it.",
        @"  vec3 scattered = atmosphere(rayDir, u_sunDir) * (8.0 / u_atmosphere.y);",
        @"  vec3 col = tonemap(scattered) / tonemap(vec3(11.2));",
        @"  col = atmosphereTint(col, elevation);",
        @"",
        @"  // How much of a day it is, from the sun's own altitude - no clock is passed in.",
        @"  float day = clamp(u_sunDir.z * 4.0 + 0.15, 0.0, 1.0);",
        @"",
        @"  // Clouds take the sun's colour where it strikes them, grey where it does not.",
        @"  float cover = clouds(rayDir);",
        @"  vec3 lit = mix(vec3(0.55, 0.58, 0.66), u_sunColor.rgb * 1.05, day);",
        @"  col = mix(col, lit, cover * (0.30 + 0.55 * day));",
        @"",
        @"  // Comets and stars only once the sun is down, and ADDED rather than mixed: they are",
        @"  // lights, not surfaces.",
        @"  col += vec3(0.80, 0.88, 1.0) * comets(rayDir) * (1.0 - day) * (1.0 - cover);",
        @"  col += vec3(starAmount(rayDir, elevation)) * (1.0 - cover);",
        @"",
        @"  return sunDisc(vec4(col, 1.0), rayDir);",
        @"}"
    ] componentsJoinedByString:@"\n"];
}

/**
 * Owns both sky switches, because they are not independent: the custom shader calls atmosphere(),
 * which the SDK only compiles in under SKY_TYPE_ATMOSPHERE. Leaving the two toggles to set the type
 * separately would let a user reach GRADIENT with the custom source still attached, and a shader
 * that names a function nobody declared does not fail loudly - it falls back to the built-in sky
 * and logs, which reads as "my shader does nothing".
 */
- (void)applySky {
    MSFPropertyGroup *sky = _host.map.sky;
    if (_customSky) {
        [sky apply:[[[MSFSpec object]
            set:@"type" value:@"SKY_TYPE_ATMOSPHERE"]
            set:@"shaderSource" value:customSkyShader()]];
    } else {
        [sky apply:[[[MSFSpec object]
            set:@"shaderSource" value:@""]
            set:@"type" value:_atmosphere ? @"SKY_TYPE_ATMOSPHERE" : @"SKY_TYPE_GRADIENT"]];
    }
}

static NSString *captionFor(NSInteger index) {
    switch (index) {
        case 0: return @"the sun just up, the haze warm and low";
        case 1: return @"high sun, thin blue sky, almost no haze";
        case 3: return @"the sun gone, stars beyond the atmosphere";
        default: return @"a low sun reddens the whole sky, not just the disc";
    }
}

@end
