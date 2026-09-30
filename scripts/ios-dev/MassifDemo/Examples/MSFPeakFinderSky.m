#import "MSFPeakFinderSky.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

#import <UIKit/UIKit.h>

// Under LOW the sun is below any skyline, over HIGH above it: only between is the terrain measured.
static const double kLow = -4;
static const double kHigh = 40;
enum { kSamples = 1440 / 2 + 1, kStep = 4 };

static double radians(double degrees) {
    return degrees * M_PI / 180;
}

/** NOAA's low-accuracy solar position - the SDK's own (LightOptions::setSunPositionFromTime). */
static void sunAt(double time, double lat, double lon, double *az, double *alt) {
    double n = time / 86400000 + 2440587.5 - 2451545.0;
    double meanLong = fmod(280.460 + 0.9856474 * n, 360);
    double meanAnom = radians(fmod(357.528 + 0.9856003 * n, 360));
    double eclipticLong = radians(meanLong + 1.915 * sin(meanAnom) + 0.020 * sin(2 * meanAnom));
    double obliquity = radians(23.439 - 0.0000004 * n);
    double rightAsc = atan2(cos(obliquity) * sin(eclipticLong), cos(eclipticLong));
    double decl = asin(sin(obliquity) * sin(eclipticLong));
    double gmst = fmod(fmod(18.697374558 + 24.06570982441908 * n, 24) + 24, 24);
    double hourAngle = radians(gmst * 15 + lon) - rightAsc;
    double phi = radians(lat);
    double a = asin(sin(phi) * sin(decl) + cos(phi) * cos(decl) * cos(hourAngle));
    double z = atan2(sin(hourAngle), cos(hourAngle) * sin(phi) - tan(decl) * cos(phi));
    *az = fmod(z * 180 / M_PI + 540, 360);
    *alt = a * 180 / M_PI;
}

/** Where the air lifts the sun to (Saemundsson), so it is compared with an apparent skyline. */
static double apparent(double alt) {
    return alt + (alt > -2 ? 1.02 / tan(radians(alt + 10.3 / (alt + 5.11))) / 60 : 0);
}

typedef struct {
    double time;
    double az;
    double alt;
} Sample;

@implementation MSFPeakFinderSky {
    MSFMassifMap *_map;
    MSFMassifObject *_path;
    MSFMassifObject *_marks;
    MSFMassifObject *_glow;
    MSFMassifObject *_sun;
    MSFMassifObject *_riseLabel;
    MSFMassifObject *_setLabel;
    NSMutableArray<MSFMassifObject *> *_hourLabels;
    NSDateFormatter *_clock;
    Sample _samples[kSamples];
    NSString *_pathKey;
    NSString *_planKey;
    NSTimeInterval _plannedAt;
    NSString *_report;
}

- (instancetype)initWithMap:(MSFMassifMap *)map below:(MSFMassifLayer *)below above:(MSFMassifLayer *)above {
    if ((self = [super init])) {
        _map = map;
        _pathKey = @"";
        _planKey = @"";
        _report = @"";
        _clock = [[NSDateFormatter alloc] init];
        _clock.dateFormat = @"HH:mm";
        double scale = UIScreen.mainScreen.scale;
        // Widths are device pixels. Drawn under the horizon too: the terrain in front hides that part.
        _path = [self add:below id:@"sky.path" spec:[[[[MSFSpec of:@"arc"] set:@"color" value:@"#f59e0bd0"]
            set:@"width" value:@(3 * scale)] set:@"belowHorizonVisible" value:@YES]];
        _marks = [self add:below id:@"sky.marks" spec:[[[[MSFSpec of:@"arc"] set:@"color" value:@"#b45309"]
            set:@"width" value:@(2 * scale)] set:@"belowHorizonVisible" value:@YES]];
        _glow = [self add:above id:@"sky.glow" spec:[[[[MSFSpec of:@"sprite"] set:@"screenSize" value:@(56 * scale)]
            set:@"color" value:@"#fbbf2466"] set:@"softness" value:@1]];
        _sun = [self add:above id:@"sky.sun" spec:[[[[MSFSpec of:@"sprite"] set:@"screenSize" value:@(20 * scale)]
            set:@"color" value:@"#f59e0b"] set:@"softness" value:@0.15]];
        _hourLabels = [NSMutableArray array];
        for (int hour = 0; hour < 24; hour++) {
            MSFSpec *halo = [[[[[[MSFSpec of:@"label"] set:@"fontName" value:@"sans-serif Medium"]
                set:@"fontSize" value:@13] set:@"textColor" value:@"#b45309"]
                set:@"haloColor" value:@"#fffffff2"] set:@"haloWidth" value:@4];
            [_hourLabels addObject:[self label:above id:[NSString stringWithFormat:@"sky.hour.%d", hour]
                                          spec:halo lift:4]];
        }
        _riseLabel = [self label:above id:@"sky.rise" spec:[MSFPeakFinderSky plate] lift:14];
        _setLabel = [self label:above id:@"sky.set" spec:[MSFPeakFinderSky plate] lift:14];
        // The rise and the set name the ridge they cross, so the ridge must not hide them.
        [_riseLabel set:@"occludedByMap" value:@NO];
        [_setLabel set:@"occludedByMap" value:@NO];
    }
    return self;
}

+ (MSFSpec *)plate {
    return [[[[[[[[MSFSpec of:@"label"] set:@"fontName" value:@"sans-serif Bold"] set:@"fontSize" value:@15]
        set:@"textColor" value:@"#92400e"] set:@"backgroundColor" value:@"#ffffffe6"]
        set:@"backgroundRadius" value:@7] set:@"paddingX" value:@7] set:@"paddingY" value:@3];
}

- (MSFMassifObject *)add:(MSFMassifLayer *)layer id:(NSString *)objectId spec:(MSFSpec *)spec {
    MSFMassifObject *object = [_map object:@"celestial" objectId:objectId spec:spec error:nil];
    [[layer call:@"add" args:@[ @(object.handle) ] error:nil] destroy];
    return object;
}

- (MSFMassifObject *)label:(MSFMassifLayer *)layer id:(NSString *)objectId spec:(MSFSpec *)spec lift:(double)lift {
    MSFMassifObject *object = [self add:layer id:objectId spec:[spec set:@"visible" value:@NO]];
    [[object call:@"setOffset" args:@[ @0, @(lift) ] error:nil] destroy];
    return object;
}

- (NSString *)updateLat:(double)lat lon:(double)lon eye:(double)eye hours:(BOOL)hours {
    NSDate *now = [NSDate date];
    double day = [[NSCalendar currentCalendar] startOfDayForDate:now].timeIntervalSince1970 * 1000;
    double az, alt;
    sunAt(now.timeIntervalSince1970 * 1000, lat, lon, &az, &alt);
    for (MSFMassifObject *object in @[ _sun, _glow ]) {
        [[object call:@"setDirection" args:@[ @(az), @(apparent(alt)), @0 ] error:nil] destroy];
        [object set:@"visible" value:@(alt > -1.5)];
    }
    NSString *key = [NSString stringWithFormat:@"%.0f|%.5f|%.5f|%.1f|%d", day, lat, lon, eye, hours];
    NSTimeInterval clock = now.timeIntervalSince1970;
    if (![key isEqualToString:_planKey] || clock - _plannedAt > 1) {
        _planKey = key;
        _plannedAt = clock;
        [self planPathDay:day lat:lat lon:lon];
        [self planCrossingsLat:lat lon:lon eye:eye hours:hours];
    }
    return _report;
}

/** The whole circle the sun runs through that day, every 2 minutes. */
- (void)planPathDay:(double)day lat:(double)lat lon:(double)lon {
    NSString *key = [NSString stringWithFormat:@"%.0f|%.3f|%.3f", day, lat, lon];
    if ([key isEqualToString:_pathKey]) {
        return;
    }
    _pathKey = key;
    NSMutableArray *directions = [NSMutableArray arrayWithCapacity:kSamples * 2];
    for (int i = 0; i < kSamples; i++) {
        double az, alt;
        _samples[i].time = day + i * 2 * 60000.0;
        sunAt(_samples[i].time, lat, lon, &az, &alt);
        _samples[i].az = az;
        _samples[i].alt = apparent(alt);
        [directions addObject:@(az)];
        [directions addObject:@(_samples[i].alt)];
    }
    [[_path call:@"setDirections" args:@[ directions ] error:nil] destroy];
}

/** One apparent skyline altitude per sample's azimuth; empty while the terrain has no elevation yet. */
- (NSData *)skylineLat:(double)lat lon:(double)lon eye:(double)eye samples:(const int *)indices count:(int)count {
    if (count == 0) {
        return [NSData data];
    }
    NSMutableArray *azimuths = [NSMutableArray arrayWithCapacity:count];
    for (int k = 0; k < count; k++) {
        [azimuths addObject:@(_samples[indices[k]].az)];
    }
    MSFMassifObject *horizon = [_map.options call:@"terrainOptions.calculateHorizon"
                                            args:@[ [MSFPosition positionWithLng:lon lat:lat], @(eye), azimuths, @200000 ]
                                           error:nil];
    NSData *result = horizon.doubles ?: [NSData data];
    [horizon destroy];
    return result;
}

static double height(NSData *skyline, int k) {
    const double *values = skyline.bytes;
    return (NSUInteger)k < skyline.length / sizeof(double) && values[k] > -90 ? values[k] : 0;
}

static double skylineAt(double time, const double *times, const double *heights, int count) {
    if (count == 0) {
        return 0;
    }
    int after = 0;
    while (after < count && times[after] < time) {
        after++;
    }
    if (after == count) {
        after = count - 1;
    }
    int before = MAX(0, times[after] == time ? after : after - 1);
    double t0 = times[before], t1 = times[after], h0 = heights[before];
    return t1 == t0 ? h0 : h0 + (heights[after] - h0) * (time - t0) / (t1 - t0);
}

/** Rise and set over the terrain: the skyline coarsely where the sun is low, finely where it crosses. */
- (void)planCrossingsLat:(double)lat lon:(double)lon eye:(double)eye hours:(BOOL)hours {
    int coarse[kSamples];
    int coarseCount = 0;
    for (int i = 0; i < kSamples; i += 4) {
        if (_samples[i].alt > kLow && _samples[i].alt < kHigh) {
            coarse[coarseCount++] = i;
        }
    }
    NSData *coarseSkyline = [self skylineLat:lat lon:lon eye:eye samples:coarse count:coarseCount];
    double knownTimes[kSamples], knownHeights[kSamples];
    for (int k = 0; k < coarseCount; k++) {
        knownTimes[k] = _samples[coarse[k]].time;
        knownHeights[k] = height(coarseSkyline, k);
    }
    const double *times = knownTimes;
    const double *heights = knownHeights;
    Sample *samples = _samples;
    double (^margin)(int) = ^double(int i) {
        double alt = samples[i].alt;
        return alt <= kLow ? -1 : alt >= kHigh ? 1 : alt - skylineAt(samples[i].time, times, heights, coarseCount);
    };

    Sample rise = { 0 }, set = { 0 };
    BOOL hasRise = NO, hasSet = NO;
    for (int index = kStep; index < kSamples; index += kStep) {
        BOOL up = margin(index - kStep) >= 0;
        if (up == (margin(index) >= 0)) {
            continue;
        }
        int fine[kStep + 1];
        for (int k = 0; k <= kStep; k++) {
            fine[k] = index - kStep + k;
        }
        NSData *fineSkyline = [self skylineLat:lat lon:lon eye:eye samples:fine count:kStep + 1];
        double fineMargin[kStep + 1];
        for (int k = 0; k <= kStep; k++) {
            fineMargin[k] = _samples[fine[k]].alt - height(fineSkyline, k);
        }
        int k = 1;
        while (k < kStep && (fineMargin[k] >= 0) == up) {
            k++;
        }
        double denominator = fineMargin[k - 1] - fineMargin[k];
        double fraction = MAX(0, MIN(1, fineMargin[k - 1] / (denominator != 0 ? denominator : 1)));
        Sample a = _samples[fine[k - 1]], b = _samples[fine[k]];
        Sample crossing = { a.time + fraction * (b.time - a.time), a.az + fraction * (b.az - a.az),
                            a.alt + fraction * (b.alt - a.alt) };
        if (!up) {
            if (!hasRise) {
                rise = crossing;
                hasRise = YES;
            }
        } else {
            set = crossing;
            hasSet = YES;
        }
    }
    NSString *riseText = hasRise ? [@"↑ " stringByAppendingString:[self clock:rise.time]] : @"";
    NSString *setText = hasSet ? [@"↓ " stringByAppendingString:[self clock:set.time]] : @"";
    _report = !hasRise && !hasSet ? @"no sun over the terrain"
        : [NSString stringWithFormat:@"%@%@%@", riseText, hasRise && hasSet ? @"  ·  " : @"", setText];
    [self show:_riseLabel text:riseText at:rise];
    [self show:_setLabel text:setText at:set];

    // On the hour, a short stroke across the path and its time, clear of a rise or a set.
    NSMutableArray *ticks = [NSMutableArray array];
    for (int hour = 0; hour < 24; hour++) {
        Sample sample = _samples[hour * 30];
        Sample next = _samples[hour * 30 + 1];
        BOOL clear = (!hasRise || fabs(rise.time - sample.time) > 40 * 60000)
                     && (!hasSet || fabs(set.time - sample.time) > 40 * 60000);
        if (!hours || !clear || margin(hour * 30) < 0.5) {
            [self show:_hourLabels[hour] text:@"" at:sample];
            continue;
        }
        [self show:_hourLabels[hour] text:[self clock:sample.time] at:sample];
        double c = cos(radians(sample.alt));
        double dAz = (next.az - sample.az) * c;
        double dAlt = next.alt - sample.alt;
        double length = hypot(dAz, dAlt);
        length = length != 0 ? length : 1;
        double nAz = -dAlt / length / c * 0.35;
        double nAlt = dAz / length * 0.35;
        [ticks addObjectsFromArray:@[ @(sample.az - nAz), @(sample.alt - nAlt), @(sample.az + nAz), @(sample.alt + nAlt) ]];
    }
    [[_marks call:@"setSegments" args:@[ ticks ] error:nil] destroy];
}

- (void)show:(MSFMassifObject *)object text:(NSString *)text at:(Sample)at {
    if (text.length == 0) {
        [object set:@"visible" value:@NO];
        return;
    }
    [object set:@"text" value:text];
    [[object call:@"setDirection" args:@[ @(at.az), @(at.alt), @0 ] error:nil] destroy];
    [object set:@"visible" value:@YES];
}

- (NSString *)clock:(double)time {
    return [_clock stringFromDate:[NSDate dateWithTimeIntervalSince1970:time / 1000]];
}

+ (NSString *)compass:(double)heading {
    static NSArray<NSString *> *points;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        points = @[ @"N", @"NNE", @"NE", @"ENE", @"E", @"ESE", @"SE", @"SSE",
                    @"S", @"SSW", @"SW", @"WSW", @"W", @"WNW", @"NW", @"NNW" ];
    });
    return points[(NSUInteger)lround(fmod(fmod(heading, 360) + 360, 360) / 22.5) % 16];
}

@end
