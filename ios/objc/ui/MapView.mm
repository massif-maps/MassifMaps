#import "MapView.h"
#import "MSFBaseMapView.h"
#import "MSFPolymorphicClasses.h"
#import "ui/MapRedrawRequestListener.h"
#import "ui/BaseMapView.h"
#include "utils/Const.h"
#include "utils/IOSUtils.h"
#include "utils/Log.h"

#import  <UIKit/UIKit.h>

@interface MSFMapView() { }

@property (strong, nonatomic) MSFBaseMapView* baseMapView;
@property (assign, nonatomic) BOOL active;
@property (assign, nonatomic) BOOL surfaceCreated;
@property (assign, nonatomic) float scale;
@property (assign, nonatomic) CGSize activeDrawableSize;

@property (strong, nonatomic) UITouch* pointer1;
@property (strong, nonatomic) UITouch* pointer2;

@end

static const int NATIVE_ACTION_POINTER_1_DOWN = 0;
static const int NATIVE_ACTION_POINTER_2_DOWN = 1;
static const int NATIVE_ACTION_MOVE = 2;
static const int NATIVE_ACTION_CANCEL = 3;
static const int NATIVE_ACTION_POINTER_1_UP = 4;
static const int NATIVE_ACTION_POINTER_2_UP = 5;
static const int NATIVE_NO_COORDINATE = -1;

@implementation MSFMapView

+(void)initialize {
    if (self == [MSFMapView class]) {
        massif::IOSUtils::InitializeLog();

        // Because iOS uses static library, we must explicitly refer to all polymorphic classes created via reflection; otherwise linking may leave them from the build
        initMSFPolymorphicClasses();
    }
}

-(id)init {
    self = [super init];
    [self initBase];
    return self;
}

-(id)initWithCoder:(NSCoder*)aDecoder {
    self = [super initWithCoder:aDecoder];
    [self initBase];
    return self;
}

-(id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    [self initBase];
    return self;
}

-(void)initBase {
    _active = NO;
    _surfaceCreated = NO;
    _activeDrawableSize = CGSizeMake(0, 0);
    _scale = [[UIScreen mainScreen] scale];
    // In case of MetalANGLE build, use the original scale value
#ifndef _MASSIF_USE_METALANGLE
    if ([[UIScreen mainScreen] respondsToSelector:@selector(nativeScale)]) {
        _scale = [[UIScreen mainScreen] nativeScale];
    }
#endif
    self.contentScaleFactor = _scale;

    _baseMapView = [[MSFBaseMapView alloc] init];
    [[_baseMapView getOptions] setDPI:massif::Const::UNSCALED_DPI * _scale];

    MSFMapRedrawRequestListener* redrawRequestListener = [[MSFMapRedrawRequestListener alloc] initWithView:self];
    [_baseMapView setRedrawRequestListener:redrawRequestListener];
    
    if (self.window != nil) {
        [self initContext];
        _active = YES;

        [self setNeedsDisplay];
    }

    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(appWillResignActive) name:UIApplicationWillResignActiveNotification object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(appDidBecomeActive) name:UIApplicationDidBecomeActiveNotification object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(appDidEnterBackground) name:UIApplicationDidEnterBackgroundNotification object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(appWillEnterForeground) name:UIApplicationWillEnterForegroundNotification object:nil];
}

-(void)initContext {
    // OpenGL ES 3.0 is required - there is no ES 2.0 fallback. Every device above the iOS 13
    // deployment floor is A7 or newer and provides one.
#ifdef _MASSIF_USE_METALANGLE
    MSFGLContext* context = [[MSFGLContext alloc] initWithAPI:kMGLRenderingAPIOpenGLES3];
#else
    MSFGLContext* context = [[MSFGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
#endif
    if (!context) {
        massif::Log::Fatal("MapView::initContext: Failed to create an OpenGL ES 3.0 context");
    }

    self.context = context;
    self.multipleTouchEnabled = YES;
#ifdef _MASSIF_USE_METALANGLE
    self.drawableColorFormat = MGLDrawableColorFormatRGBA8888;
    self.drawableDepthFormat = MGLDrawableDepthFormat24;
    self.drawableMultisample = MGLDrawableMultisampleNone;
    self.drawableStencilFormat = MGLDrawableStencilFormat8;
#else
    self.drawableColorFormat = GLKViewDrawableColorFormatRGBA8888;
    self.drawableDepthFormat = GLKViewDrawableDepthFormat24;
    self.drawableMultisample = GLKViewDrawableMultisampleNone;
    self.drawableStencilFormat = GLKViewDrawableStencilFormat8;
#endif
}

-(void)setTranslucent:(BOOL)translucent {
    // The GL view itself, and the layer it draws into: both have to stop claiming they cover
    // their pixels, or the compositor skips whatever is underneath. The drawable is already
    // RGBA8888 (see initContext), so the alpha the renderer writes is the one that is composited.
    self.opaque = !translucent;
    self.layer.opaque = !translucent;
    if (translucent) {
        self.backgroundColor = [UIColor clearColor];
    }
}

-(void)willMoveToWindow:(UIWindow *)newWindow {
    [super willMoveToWindow:newWindow];

    if (newWindow == nil) {
        massif::Log::Info("MapView::willMoveToWindow: null");
    } else {
        massif::Log::Info("MapView::willMoveToWindow: nonnull");

        @synchronized (self) {
            if (!_active) {
                [self initContext];
                if ([MSFGLContext currentContext] == nil) {
                    [MSFGLContext setCurrentContext:self.context];
                }
                _active = YES;
                _surfaceCreated = NO;
            }
        }
    }
}

-(void)didMoveToWindow {
    [super didMoveToWindow];

    if (self.window == nil) {
        massif::Log::Info("MapView::didMoveToWindow: null");

        @synchronized (self) {
            if (_active) {
                [_baseMapView onSurfaceDestroyed];
                if ([MSFGLContext currentContext] == self.context) {
                    [MSFGLContext setCurrentContext:nil];
                }
                _active = NO;
                _surfaceCreated = NO;
            }
        }
    } else {
        massif::Log::Info("MapView::didMoveToWindow: nonnull");

        [self setNeedsDisplay];
    }
}

-(void)layoutSubviews {
    [super layoutSubviews];

    [self setNeedsDisplay];
}

-(void)drawRect:(CGRect)rect {
    @synchronized (self) {
        if (_active) {
#ifdef _MASSIF_USE_METALANGLE
            MSFGLContext* context = [MSFGLContext currentContext];
            [MSFGLContext setCurrentContext:self.context forLayer:self.glLayer];
#endif

            if (!_surfaceCreated) {
                [_baseMapView onSurfaceCreated];
                _activeDrawableSize = CGSizeMake(0, 0);
                _surfaceCreated = YES;
            }

#ifdef _MASSIF_USE_METALANGLE
            CGFloat drawableWidth = self.drawableSize.width;
            CGFloat drawableHeight = self.drawableSize.height;
#else
            CGFloat drawableWidth = self.drawableWidth;
            CGFloat drawableHeight = self.drawableHeight;
#endif
            if (_activeDrawableSize.width != drawableWidth || _activeDrawableSize.height != drawableHeight) {
                _activeDrawableSize = CGSizeMake(drawableWidth, drawableHeight);
                [_baseMapView onSurfaceChanged:(int)_activeDrawableSize.width height:(int)_activeDrawableSize.height];
            }

            [_baseMapView onDrawFrame];

#ifdef _MASSIF_USE_METALANGLE
            [self.context present:self.glLayer];
            if (context != self.context) {
                [MSFGLContext setCurrentContext:context];
            }
#endif
        }
    }
}

-(void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self name:UIApplicationWillResignActiveNotification object:nil];
    [[NSNotificationCenter defaultCenter] removeObserver:self name:UIApplicationDidBecomeActiveNotification object:nil];
    [[NSNotificationCenter defaultCenter] removeObserver:self name:UIApplicationDidEnterBackgroundNotification object:nil];
    [[NSNotificationCenter defaultCenter] removeObserver:self name:UIApplicationWillEnterForegroundNotification object:nil];

    @synchronized (self) {
        if (self.context) {
            [_baseMapView onSurfaceDestroyed];
            [_baseMapView setRedrawRequestListener:nil];

            if ([MSFGLContext currentContext] == self.context) {
                [MSFGLContext setCurrentContext:nil];
            }

            _baseMapView = nil;
            _surfaceCreated = NO;
            _activeDrawableSize = CGSizeMake(0, 0);
        }
    }
}

-(void)appWillResignActive {
    massif::Log::Info("MapView::appWillResignActive");

    @synchronized (self) {
        if (_active) {
            MSFGLContext* context = [MSFGLContext currentContext];
            if (context != self.context) {
                [MSFGLContext setCurrentContext:self.context];
            }

            [_baseMapView finishRendering];

            if (context != self.context) {
                [MSFGLContext setCurrentContext:context];
            }
        }
        _active = NO;
    }
}

-(void)appDidBecomeActive {
    massif::Log::Info("MapView::appDidBecomeActive");

    @synchronized (self) {
        _active = YES;
    }

    [self setNeedsDisplay];
}

-(void)appDidEnterBackground {
    massif::Log::Info("MapView::appDidEnterBackground");

    @synchronized (self) {
        _active = NO;
    }
}

-(void)appWillEnterForeground {
    massif::Log::Info("MapView::appWillEnterForeground");

    @synchronized (self) {
        _active = YES;
    }

    [self setNeedsDisplay];
}

-(void)transformScreenCoord:(CGPoint*)screenCoord {
    screenCoord->x *= _scale;
    screenCoord->y *= _scale;
}

-(void)touchesBegan:(NSSet*)touches withEvent:(UIEvent*)event {
    for (UITouch* pointer in [touches allObjects]) {
        if (!_pointer1) {
            _pointer1 = pointer;
            CGPoint screenPos = [_pointer1 locationInView:self];
            [self transformScreenCoord:&screenPos];
            [_baseMapView onInputEvent:NATIVE_ACTION_POINTER_1_DOWN x1:screenPos.x y1:screenPos.y x2:NATIVE_NO_COORDINATE y2:NATIVE_NO_COORDINATE];
            continue;
        }
        
        if (!_pointer2) {
            _pointer2 = pointer;
            CGPoint screenPos1 = [_pointer1 locationInView:self];
            CGPoint screenPos2 = [_pointer2 locationInView:self];
            [self transformScreenCoord:&screenPos1];
            [self transformScreenCoord:&screenPos2];
            [_baseMapView onInputEvent:NATIVE_ACTION_POINTER_2_DOWN x1:screenPos1.x y1:screenPos1.y x2:screenPos2.x y2:screenPos2.y];
            break;
        }
    }
}

-(void)touchesMoved:(NSSet*)touches withEvent:(UIEvent*)event {
    if (_pointer1) {
        CGPoint screenPos1 = [_pointer1 locationInView:self];
        [self transformScreenCoord:&screenPos1];
        if (_pointer2) {
            CGPoint screenPos2 = [_pointer2 locationInView:self];
            [self transformScreenCoord:&screenPos2];
            [_baseMapView onInputEvent:NATIVE_ACTION_MOVE x1:screenPos1.x y1:screenPos1.y x2:screenPos2.x y2:screenPos2.y];
        } else {
            [_baseMapView onInputEvent:NATIVE_ACTION_MOVE x1:screenPos1.x y1:screenPos1.y x2:NATIVE_NO_COORDINATE y2:NATIVE_NO_COORDINATE];
        }
    }
}

-(void)touchesCancelled:(NSSet*)touches withEvent:(UIEvent*)event {
    [_baseMapView onInputEvent:NATIVE_ACTION_CANCEL x1:NATIVE_NO_COORDINATE y1:NATIVE_NO_COORDINATE x2:NATIVE_NO_COORDINATE y2:NATIVE_NO_COORDINATE];
    _pointer1 = nil;
    _pointer2 = nil;
}

-(void)touchesEnded:(NSSet*)touches withEvent:(UIEvent*)event {
    if (_pointer2 && [touches containsObject:_pointer2]) {
        // Dual pointer, second pointer goes up first
        CGPoint screenPos1 = [_pointer1 locationInView:self];
        CGPoint screenPos2 = [_pointer2 locationInView:self];
        [self transformScreenCoord:&screenPos1];
        [self transformScreenCoord:&screenPos2];
        [_baseMapView onInputEvent:NATIVE_ACTION_POINTER_2_UP x1:screenPos1.x y1:screenPos1.y x2:screenPos2.x y2:screenPos2.y];
        _pointer2 = nil;
    }
    
    if (_pointer1 && [touches containsObject:_pointer1]) {
        // Single pointer, pointer goes up
        CGPoint screenPos1 = [_pointer1 locationInView:self];
        [self transformScreenCoord:&screenPos1];
        if (_pointer2) {
            CGPoint screenPos2 = [_pointer2 locationInView:self];
            [self transformScreenCoord:&screenPos2];
            [_baseMapView onInputEvent:NATIVE_ACTION_POINTER_1_UP x1:screenPos1.x y1:screenPos1.y x2:screenPos2.x y2:screenPos2.y];
            _pointer1 = _pointer2;
            _pointer2 = nil;
        } else {
            [_baseMapView onInputEvent:NATIVE_ACTION_POINTER_1_UP x1:screenPos1.x y1:screenPos1.y x2:NATIVE_NO_COORDINATE y2:NATIVE_NO_COORDINATE];
            _pointer1 = nil;
        }
    }
}

-(MSFLayers*)getLayers {
    return [_baseMapView getLayers];
}

-(MSFOptions*)getOptions {
    return [_baseMapView getOptions];
}

-(MSFMapRenderer*)getMapRenderer {
    return [_baseMapView getMapRenderer];
}

-(MSFMapPos*)getFocusPos {
    return [_baseMapView getFocusPos];
}

-(float)getRotation {
    return [_baseMapView getRotation];
}

-(float)getTilt {
    return [_baseMapView getTilt];
}

-(float)getZoom {
    return [_baseMapView getZoom];
}

-(void)pan:(MSFMapVec*)deltaPos durationSeconds:(float)durationSeconds {
    [_baseMapView pan:deltaPos durationSeconds:durationSeconds];
}

-(void)setFocusPos:(MSFMapPos*)pos durationSeconds:(float)durationSeconds {
    [_baseMapView setFocusPos:pos durationSeconds:durationSeconds];
}

-(void)rotate:(float)deltaAngle durationSeconds:(float)durationSeconds {
    [_baseMapView rotate:deltaAngle durationSeconds:durationSeconds];
}

-(void)moveTo:(MSFMapPos*)pos zoom:(float)zoom {
    [_baseMapView moveTo:pos zoom:zoom];
}

-(void)moveTo:(MSFMapPos*)pos zoom:(float)zoom rotation:(float)rotation tilt:(float)tilt {
    [_baseMapView moveTo:pos zoom:zoom rotation:rotation tilt:tilt];
}

-(void)flyTo:(MSFMapPos*)pos zoom:(float)zoom durationSeconds:(float)durationSeconds {
    [_baseMapView flyTo:pos zoom:zoom durationSeconds:durationSeconds];
}

-(void)flyTo:(MSFMapPos*)pos zoom:(float)zoom rotation:(float)rotation tilt:(float)tilt durationSeconds:(float)durationSeconds {
    [_baseMapView flyTo:pos zoom:zoom rotation:rotation tilt:tilt durationSeconds:durationSeconds];
}

-(void)flyTo:(MSFMapPos*)pos zoom:(float)zoom rotation:(float)rotation tilt:(float)tilt climbHeight:(float)climbHeight durationSeconds:(float)durationSeconds {
    [_baseMapView flyTo:pos zoom:zoom rotation:rotation tilt:tilt climbHeight:climbHeight durationSeconds:durationSeconds];
}

-(void)stopFlight {
    [_baseMapView stopFlight];
}

-(BOOL)isFlightActive {
    return [_baseMapView isFlightActive];
}

-(float)getFlightProgress {
    return [_baseMapView getFlightProgress];
}

-(void)rotate:(float)deltaAngle targetPos:(MSFMapPos*)targetPos durationSeconds:(float)durationSeconds {
    [_baseMapView rotate:deltaAngle targetPos:targetPos durationSeconds:durationSeconds];
}

-(void)setRotation:(float)angle durationSeconds:(float)durationSeconds {
    [_baseMapView setRotation:angle durationSeconds:durationSeconds];
}

-(void)setRotation:(float)angle targetPos:(MSFMapPos*)targetPos durationSeconds:(float)durationSeconds {
    [_baseMapView setRotation:angle targetPos:targetPos durationSeconds:durationSeconds];
}

-(void)tilt:(float)deltaTilt durationSeconds:(float)durationSeconds {
    [_baseMapView tilt:deltaTilt durationSeconds:durationSeconds];
}

-(void)setTilt:(float)tilt durationSeconds:(float)durationSeconds {
    [_baseMapView setTilt:tilt durationSeconds:durationSeconds];
}

-(void)zoom:(float)deltaZoom durationSeconds:(float)durationSeconds {
    [_baseMapView zoom:deltaZoom durationSeconds:durationSeconds];
}

-(void)zoom:(float)deltaZoom targetPos:(MSFMapPos*)targetPos durationSeconds:(float)durationSeconds {
    [_baseMapView zoom:deltaZoom targetPos:targetPos durationSeconds:durationSeconds];
}

-(void)setZoom:(float)zoom durationSeconds:(float)durationSeconds {
    [_baseMapView setZoom:zoom durationSeconds:durationSeconds];
}

-(void)setZoom:(float)zoom targetPos:(MSFMapPos*)targetPos durationSeconds:(float)durationSeconds {
    [_baseMapView setZoom:zoom targetPos:targetPos durationSeconds:durationSeconds];
}

-(void)moveToFitBounds:(MSFMapBounds*)mapBounds screenBounds:(MSFScreenBounds*)screenBounds integerZoom:(BOOL)integerZoom durationSeconds:(float)durationSeconds {
    [_baseMapView moveToFitBounds:mapBounds screenBounds:screenBounds integerZoom:integerZoom durationSeconds:durationSeconds];
}

-(void)moveToFitBounds:(MSFMapBounds*)mapBounds screenBounds:(MSFScreenBounds*)screenBounds integerZoom:(BOOL)integerZoom resetRotation:(BOOL)resetRotation resetTilt:(BOOL)resetTilt durationSeconds:(float)durationSeconds {
    [_baseMapView moveToFitBounds:mapBounds screenBounds:screenBounds integerZoom:integerZoom resetRotation:resetRotation resetTilt:resetTilt durationSeconds:durationSeconds];
}

-(MSFMapEventListener*) getMapEventListener {
    return [_baseMapView getMapEventListener];
}

-(void)setMapEventListener:(MSFMapEventListener*)mapEventListener {
    [_baseMapView setMapEventListener:mapEventListener];
}

-(MSFMapPos*)screenToMap:(MSFScreenPos*)screenPos {
    return [_baseMapView screenToMap:screenPos];
}

-(MSFScreenPos*)mapToScreen:(MSFMapPos*)mapPos {
    return [_baseMapView mapToScreen:mapPos];
}

-(void)cancelAllTasks {
    [_baseMapView cancelAllTasks];
}

-(void)clearPreloadingCaches {
    [_baseMapView clearPreloadingCaches];
}

-(void)clearAllCaches {
    [_baseMapView clearAllCaches];
}

@end
