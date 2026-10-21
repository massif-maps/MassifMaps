#import "DemoCapture.h"
#import "DemoCfg.h"

#import <UIKit/UIKit.h>
#import "MassifMaps.h"

@interface DemoCaptureListener : MSFRendererCaptureListener
@property (nonatomic, copy) void (^completion)(MSFBitmap *bitmap);
@end

@implementation DemoCaptureListener
- (void)onMapRendered:(MSFBitmap *)bitmap {
    if (self.completion) {
        self.completion(bitmap);
    }
}
@end

@interface DemoCapture ()
@property (nonatomic, strong) MSFMapView *mapView;
@property (nonatomic, strong) CADisplayLink *spinLink;
@property (nonatomic, strong) DemoCaptureListener *listener;
@property (nonatomic) CFTimeInterval spinStart;
@property (nonatomic) float baseRotation;
@property (nonatomic) float baseZoom;
@property (nonatomic) float degPerSec;
@property (nonatomic) float duration;
@property (nonatomic) BOOL quitWhenDone;
@property (nonatomic) BOOL shotPending;
@property (nonatomic, strong) dispatch_source_t noopTimer;
@end

static DemoCapture *sInstance;
static dispatch_group_t sPending;

@implementation DemoCapture

+ (void)startIfRequested {
    BOOL wanted = [DemoCfg has:@"shot"] || [DemoCfg has:@"shotTilts"] || [DemoCfg has:@"shotViews"] || [DemoCfg has:@"shotSequence"] || [DemoCfg has:@"rotateDegPerSec"];
    if (!wanted) {
        return;
    }
    sPending = dispatch_group_create();
    sInstance = [[DemoCapture alloc] init];
    [sInstance begin];
}

+ (MSFMapView *)findMapViewIn:(UIView *)view {
    if ([view isKindOfClass:[MSFMapView class]]) {
        return (MSFMapView *)view;
    }
    for (UIView *child in view.subviews) {
        MSFMapView *found = [self findMapViewIn:child];
        if (found) {
            return found;
        }
    }
    return nil;
}

+ (MSFMapView *)currentMapView {
    for (UIWindow *window in [UIApplication sharedApplication].windows) {
        MSFMapView *found = [self findMapViewIn:window];
        if (found) {
            return found;
        }
    }
    return nil;
}

- (void)begin {
    float delay = [DemoCfg floatFor:@"shotDelay" defaultValue:0];
    NSLog(@"DemoCapture: starting in %.0f s", delay);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(delay * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [self fire];
    });
}

- (void)fire {
    self.mapView = [DemoCapture currentMapView];
    if (!self.mapView) {
        NSLog(@"DemoCapture: no MSFMapView in the window, retrying");
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{ [self fire]; });
        return;
    }
    self.degPerSec = [DemoCfg floatFor:@"rotateDegPerSec" defaultValue:0];
    self.duration = [DemoCfg floatFor:@"duration" defaultValue:0];
    self.quitWhenDone = [DemoCfg boolFor:@"quitAfterShot" defaultValue:YES];
    self.baseRotation = [self.mapView getRotation];
    self.baseZoom = [self.mapView getZoom];

    float noopMoveMs = [DemoCfg floatFor:@"noopMoveMs" defaultValue:0];
    if (noopMoveMs > 0) {
        [self startNoopMoves:noopMoveMs duration:[DemoCfg floatFor:@"noopMoveDuration" defaultValue:1]];
    }

    NSString *views = [DemoCfg stringFor:@"shotViews" defaultValue:@""];
    if (views.length) {
        [self runViews:[views componentsSeparatedByString:@","] index:0];
        return;
    }
    NSString *tilts = [DemoCfg stringFor:@"shotTilts" defaultValue:@""];
    if (tilts.length) {
        [self runTilts:[tilts componentsSeparatedByString:@","] index:0];
        return;
    }
    int frames = [DemoCfg intFor:@"shotSequence" defaultValue:0];
    BOOL single = [DemoCfg has:@"shot"];
    if (self.degPerSec != 0) {
        [self startSpin];
    }
    if (frames > 0) {
        [self runSequence:frames];
    } else if (single) {
        [self captureTo:[DemoCfg stringFor:@"shot" defaultValue:@"/tmp/massifdemo-shots/shot.png"]
                 thenDo:^{ [self finish]; }];
    } else if (self.duration > 0) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(self.duration * NSEC_PER_SEC)),
                       dispatch_get_main_queue(), ^{ [self finish]; });
    }
}

/** An app following the user: a moveTo to the position and zoom the camera already has, every `intervalMs`. */
- (void)startNoopMoves:(float)intervalMs duration:(float)durationSeconds {
    MSFMapPos *focus = [self.mapView getFocusPos];
    float zoom = [self.mapView getZoom];
    double stepX = [DemoCfg floatFor:@"noopMoveStep" defaultValue:0];
    __block double x = [focus getX];
    NSLog(@"DemoCapture: a move every %.0f ms, %.1f s long, %.2f east each", intervalMs, durationSeconds, stepX);
    dispatch_source_t timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(intervalMs * NSEC_PER_MSEC)), (uint64_t)(intervalMs * NSEC_PER_MSEC), 0);
    dispatch_source_set_event_handler(timer, ^{
        x += stepX;
        [self.mapView setFocusPos:[[MSFMapPos alloc] initWithX:x y:[focus getY] z:[focus getZ]] durationSeconds:durationSeconds];
        [self.mapView setZoom:zoom durationSeconds:durationSeconds];
    });
    dispatch_resume(timer);
    self.noopTimer = timer;
}

- (void)startSpin {
    self.spinStart = CACurrentMediaTime();
    self.spinLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(spinTick:)];
    [self.spinLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)spinTick:(CADisplayLink *)link {
    float elapsed = (float)(CACurrentMediaTime() - self.spinStart);
    if (self.duration > 0 && elapsed > self.duration) {
        [self.spinLink invalidate];
        self.spinLink = nil;
        return;
    }
    [self.mapView setRotation:self.baseRotation + self.degPerSec * elapsed durationSeconds:0];
}

/**
 * One frame of the map's own render. The callback is on the render thread, so it only keeps the
 * bitmap: PNG encoding (hundreds of ms) happens elsewhere, and `captured` fires as soon as the
 * frame is in hand so a burst is paced by its interval, not by the encoder.
 */
- (void)captureTo:(NSString *)path thenDo:(void (^)(void))captured {
    [[NSFileManager defaultManager] createDirectoryAtPath:[path stringByDeletingLastPathComponent]
                              withIntermediateDirectories:YES attributes:nil error:nil];
    DemoCaptureListener *listener = [[DemoCaptureListener alloc] init];
    __weak DemoCapture *weakSelf = self;
    listener.completion = ^(MSFBitmap *bitmap) {
        dispatch_group_enter(sPending);
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            MSFBinaryData *png = [bitmap compressToPNG];
            NSData *data = [NSData dataWithBytes:[png getData] length:[png size]];
            [data writeToFile:path atomically:YES];
            NSLog(@"DemoCapture: wrote %@ (%ux%u, %lu bytes)", path, [bitmap getWidth], [bitmap getHeight],
                  (unsigned long)data.length);
            dispatch_group_leave(sPending);
        });
        dispatch_async(dispatch_get_main_queue(), ^{
            weakSelf.listener = nil;
            if (captured) {
                captured();
            }
        });
    };
    self.listener = listener;
    MSFMapRenderer *renderer = [self.mapView getMapRenderer];
    [renderer captureRendering:listener waitWhileUpdating:NO];
    [renderer requestRedraw];
}

/** One still per "zoom/tilt" view: <shotDir>/z<zoom>_t<tilt>.png. */
- (void)runViews:(NSArray<NSString *> *)views index:(NSUInteger)index {
    if (index >= views.count) {
        [self finish];
        return;
    }
    NSArray<NSString *> *parts = [views[index] componentsSeparatedByString:@"/"];
    float zoom = parts[0].floatValue;
    float tilt = parts.count > 1 ? parts[1].floatValue : [self.mapView getTilt];
    NSString *dir = [DemoCfg stringFor:@"shotDir" defaultValue:@"/tmp/massifdemo-shots/views"];
    float waitMs = [DemoCfg floatFor:@"shotTiltWaitMs" defaultValue:6000];
    [self.mapView setZoom:zoom durationSeconds:0];
    [self.mapView setTilt:tilt durationSeconds:0];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(waitMs * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
        NSString *path = [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"z%.1f_t%02.0f.png", zoom, tilt]];
        NSLog(@"DemoCapture: view asked z%.1f t%.0f, map reports z%.2f t%.1f", zoom, tilt, [self.mapView getZoom], [self.mapView getTilt]);
        [self captureTo:path thenDo:^{ [self runViews:views index:index + 1]; }];
    });
}

/** One still per tilt (SDK convention: 90 straight down, 0 the horizon): <shotDir>/tilt_<n>.png. */
- (void)runTilts:(NSArray<NSString *> *)tilts index:(NSUInteger)index {
    if (index >= tilts.count) {
        [self finish];
        return;
    }
    float tilt = tilts[index].floatValue;
    NSString *dir = [DemoCfg stringFor:@"shotDir" defaultValue:@"/tmp/massifdemo-shots/tilts"];
    float waitMs = [DemoCfg floatFor:@"shotTiltWaitMs" defaultValue:6000];
    [self.mapView setTilt:tilt durationSeconds:0];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(waitMs * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
        NSString *path = [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"tilt_%02.0f.png", tilt]];
        NSLog(@"DemoCapture: tilt asked %.0f, map reports %.1f", tilt, [self.mapView getTilt]);
        [self captureTo:path thenDo:^{ [self runTilts:tilts index:index + 1]; }];
    });
}

- (void)runSequence:(int)frames {
    NSString *dir = [DemoCfg stringFor:@"shotDir" defaultValue:@"/tmp/massifdemo-shots/seq"];
    float intervalMs = [DemoCfg floatFor:@"shotIntervalMs" defaultValue:80];
    float stepDeg = [DemoCfg floatFor:@"rotateStepDeg" defaultValue:0];
    [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    NSMutableString *csv = [NSMutableString stringWithString:@"frame,t_ms,rotation,zoom,tilt\n"];
    CFTimeInterval start = CACurrentMediaTime();
    [self captureFrame:0 of:frames dir:dir intervalMs:intervalMs stepDeg:stepDeg start:start csv:csv];
}

- (void)captureFrame:(int)index of:(int)frames dir:(NSString *)dir intervalMs:(float)intervalMs
             stepDeg:(float)stepDeg start:(CFTimeInterval)start csv:(NSMutableString *)csv {
    if (index >= frames) {
        [csv writeToFile:[dir stringByAppendingPathComponent:@"frames.csv"] atomically:YES
                encoding:NSUTF8StringEncoding error:nil];
        NSLog(@"DemoCapture: sequence of %d frames done in %@", frames, dir);
        [self finish];
        return;
    }
    float zoomStep = [DemoCfg floatFor:@"zoomStep" defaultValue:0];
    if (zoomStep != 0) {
        [self.mapView setZoom:self.baseZoom + zoomStep * index durationSeconds:0];
    }
    if (stepDeg != 0) {
        [self.mapView setRotation:self.baseRotation + stepDeg * index durationSeconds:0];
    }
    [csv appendFormat:@"%d,%.0f,%.3f,%.3f,%.2f\n", index, (CACurrentMediaTime() - start) * 1000.0,
     [self.mapView getRotation], [self.mapView getZoom], [self.mapView getTilt]];
    NSString *path = [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"frame_%04d.png", index]];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(intervalMs * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
        [self captureTo:path thenDo:^{
            [self captureFrame:index + 1 of:frames dir:dir intervalMs:intervalMs stepDeg:stepDeg start:start csv:csv];
        }];
    });
}

- (void)finish {
    [self.spinLink invalidate];
    self.spinLink = nil;
    if (self.quitWhenDone) {
        // the encoders may still be writing
        dispatch_group_notify(sPending, dispatch_get_main_queue(), ^{
            NSLog(@"DemoCapture: done, exiting");
            exit(0);
        });
    }
}

@end
