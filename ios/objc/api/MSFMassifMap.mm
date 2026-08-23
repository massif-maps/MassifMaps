#import "MSFMassifInternal.h"
#import "MSFMassifApi.h"
#import "MSFMassifInterop.h"
#import "ui/MapView.h"
#import "MSFLayer.h"
#import "MSFLayers.h"
#import "MSFMapEventListener.h"

#include "api/Context.h"

static NSString * const kMapKind = @"map";
static NSString * const kViewKind = @"view";

@implementation MSFMapCamera {
    MSFMassifObject *_view;
    float _duration;
    float _climb;
}

- (instancetype)initWithView:(id)view {
    if ((self = [super init])) {
        _view = view;
    }
    return self;
}

- (instancetype)animate:(float)seconds {
    _duration = seconds;
    return self;
}

- (instancetype)climb:(float)height {
    _climb = height;
    return self;
}

- (float)take {
    float seconds = _duration;
    _duration = 0;
    return seconds;
}

- (instancetype)position:(MSFPosition *)pos {
    return [self moveTo:pos zoom:self.currentZoom rotation:self.currentRotation
                   tilt:self.currentTilt];
}

- (instancetype)zoom:(float)zoom {
    return [self moveTo:self.currentPosition zoom:zoom rotation:self.currentRotation
                   tilt:self.currentTilt];
}

- (instancetype)rotation:(float)degrees {
    return [self moveTo:self.currentPosition zoom:self.currentZoom rotation:degrees
                   tilt:self.currentTilt];
}

- (instancetype)tilt:(float)degrees {
    return [self moveTo:self.currentPosition zoom:self.currentZoom
               rotation:self.currentRotation tilt:degrees];
}

- (instancetype)moveTo:(MSFPosition *)pos zoom:(float)zoom rotation:(float)rotation tilt:(float)tilt {
    float seconds = [self take];
    float height = _climb;
    _climb = 0;
    if (seconds > 0) {
        [[_view call:@"flyTo" args:@[ pos, @(zoom), @(rotation), @(tilt), @(height), @(seconds) ]
                error:nil] destroy];
    } else {
        // Not flyTo with a duration of 0: that means "pick a duration from the path", and a flight
        // needs a frame to set itself up against. This is the immediate move, and it works before
        // the map has drawn - which is when a screen usually points its camera.
        [[_view call:@"moveTo" args:@[ pos, @(zoom), @(rotation), @(tilt) ] error:nil] destroy];
    }
    return self;
}

- (instancetype)moveTo:(MSFPosition *)pos zoom:(float)zoom {
    return [self moveTo:pos zoom:zoom rotation:self.currentRotation tilt:self.currentTilt];
}

- (instancetype)fitBounds:(MSFBounds *)bounds
               screenRect:(MSFScreenRect *)screenRect
              integerZoom:(BOOL)integerZoom {
    return [self fitBounds:bounds screenRect:screenRect integerZoom:integerZoom
             resetRotation:NO resetTilt:NO];
}

- (instancetype)fitBounds:(MSFBounds *)bounds
               screenRect:(MSFScreenRect *)screenRect
              integerZoom:(BOOL)integerZoom
            resetRotation:(BOOL)resetRotation
                resetTilt:(BOOL)resetTilt {
    [[_view call:@"fitBounds"
            args:@[ bounds, screenRect, @(integerZoom), @(resetRotation), @(resetTilt),
                    @([self take]) ]
           error:nil] destroy];
    return self;
}

- (instancetype)fitBounds:(MSFBounds *)bounds width:(float)width height:(float)height {
    return [self fitBounds:bounds
                screenRect:[MSFScreenRect rectWithLeft:0 top:0 right:width bottom:height]
               integerZoom:NO];
}

- (MSFPosition *)currentPosition {
    return [_view getPos:@"focusPos"];
}

- (float)currentZoom {
    return (float)[_view getDouble:@"zoom" defaultValue:0];
}

- (float)currentRotation {
    return (float)[_view getDouble:@"rotation" defaultValue:0];
}

- (float)currentTilt {
    return (float)[_view getDouble:@"tilt" defaultValue:0];
}

- (MSFPosition *)screenToMapX:(float)x y:(float)y {
    MSFMassifObject *result = [_view call:@"screenToMap" args:@[ @(x), @(y) ] error:nil];
    MSFPosition *pos = [MSFValues posFromJson:result.json];
    [result destroy];
    return pos;
}

- (MSFScreenPoint *)mapToScreen:(MSFPosition *)pos {
    MSFMassifObject *result = [_view call:@"mapToScreen" args:@[ pos ] error:nil];
    MSFScreenPoint *point = [MSFValues screenPointFromJson:result.json];
    [result destroy];
    return point;
}

- (BOOL)isMoving {
    return [_view getBool:@"flightActive" defaultValue:NO];
}

- (float)progress {
    return (float)[_view getDouble:@"flightProgress" defaultValue:0];
}

- (instancetype)stop {
    [[_view call:@"stopFlight" args:@[] error:nil] destroy];
    return self;
}

@end

@implementation MSFMassifMap {
    MSFMapView *_view;
    MSFMassifObject *_options;
    MSFMapCamera *_camera;
    BOOL _bridged;
    /** Kind and id of everything this map built, so detach releases it. */
    NSMutableArray<NSArray<NSString *> *> *_owned;
    MSFMassifElements *_elements;
}

+ (instancetype)attach:(MSFMapView *)view {
    return [self attach:view objectId:@"main"];
}

/**
 * Sends queued handlers to the main queue, once per process.
 *
 * Without it a subscription that asked for main-queue delivery runs INLINE on the thread that
 * produced the event - a GL or tile thread - and the facade warns once. Every handler in this API
 * is documented as main-queue, so the sugar owes the hop.
 *
 * No director needed here, unlike Java: this is Objective-C++ and can hand the context a plain C
 * function.
 */
static void MSFInstallUiDispatcher(void) {
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        massif::api::Context::GetDefault()->setUiDispatcher(
            [](void*, void (*function)(void*), void* argument) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    function(argument);
                });
            },
            nullptr);
    });
}

+ (instancetype)attach:(MSFMapView *)view objectId:(NSString *)objectId {
    if (!view) {
        return nil;
    }
    MSFInstallUiDispatcher();
    int handle = [MSFMassifApi findObject:kMapKind objectId:objectId];
    if (handle == 0) {
        handle = [MSFMassifInterop adopt:kMapKind objectId:objectId options:[view getOptions]];
        if (handle == 0) {
            return nil;
        }
    }
    // The map view is adopted too, under its own kind: it is what carries the CAMERA, and going
    // through the facade is what gives moveTo the map's projection.
    int viewHandle = [MSFMassifApi findObject:kViewKind objectId:objectId];
    if (viewHandle == 0) {
        viewHandle = [MSFMassifInterop adopt:kViewKind objectId:objectId
                                        view:[view getBaseMapView]];
        if (viewHandle == 0) {
            return nil;
        }
    }
    MSFMassifMap *map = [[MSFMassifMap alloc] init];
    map->_view = view;
    map->_options = [[MSFMassifObject alloc] initWithHandle:handle kind:kMapKind objectId:objectId];
    map->_camera = [[MSFMapCamera alloc]
        initWithView:[[MSFMassifObject alloc] initWithHandle:viewHandle kind:kViewKind
                                                   objectId:objectId]];
    map.eventProjection = @"";
    return map;
}

- (MSFMapView *)view {
    return _view;
}

- (MSFMassifObject *)options {
    return _options;
}

- (MSFMapCamera *)camera {
    return _camera;
}

- (MSFPropertyGroup *)fog {
    return [_options group:@"fogOptions"];
}

- (MSFPropertyGroup *)sky {
    return [_options group:@"skyOptions"];
}

- (MSFPropertyGroup *)terrain {
    return [_options group:@"terrainOptions"];
}

- (MSFPropertyGroup *)light {
    return [_options group:@"lightOptions"];
}

// --- layers -----------------------------------------------------------------------------------

- (MSFMassifLayer *)add:(MSFMassifLayer *)layer {
    MSFLayer *target = layer.layer;
    if (target) {
        [[_view getLayers] add:target];
    }
    return [self attached:layer];
}

- (MSFMassifLayer *)add:(MSFMassifLayer *)layer atIndex:(int)index {
    MSFLayer *target = layer.layer;
    if (target) {
        [[_view getLayers] insert:index layer:target];
    }
    return [self attached:layer];
}

- (MSFMassifLayer *)addLayer:(NSString *)objectId spec:(MSFSpec *)spec error:(NSError **)error {
    MSFMassifLayer *layer = [MSFMassif layer:objectId spec:spec error:error];
    if (!layer) {
        return nil;
    }
    [self own:@"layer" objectId:objectId];
    return [self add:layer];
}

- (void)own:(NSString *)kind objectId:(NSString *)objectId {
    if (!_owned) {
        _owned = [NSMutableArray array];
    }
    [_owned addObject:@[ kind, objectId ]];
}

// --- the Options sub-objects -------------------------------------------------------------------

- (MSFPropertyGroup *)optionGroup:(NSString *)property
                             type:(NSString *)type
                             spec:(MSFSpec *)spec
                            error:(NSError **)error {
    NSString *objectId = [NSString stringWithFormat:@"%@.%@", _options.objectId ?: @"map", type];
    MSFMassifObject *built = [self object:@"options" objectId:objectId
                                     spec:spec ?: [MSFSpec of:type] error:error];
    if (!built || ![_options set:property value:built]) {
        return nil;
    }
    return [_options group:property];
}

- (MSFPropertyGroup *)terrainWithSpec:(MSFSpec *)spec error:(NSError **)error {
    return [self optionGroup:@"terrainOptions" type:@"terrain" spec:spec error:error];
}

- (MSFPropertyGroup *)fogWithSpec:(MSFSpec *)spec error:(NSError **)error {
    return [self optionGroup:@"fogOptions" type:@"fog" spec:spec error:error];
}

- (MSFPropertyGroup *)skyWithSpec:(MSFSpec *)spec error:(NSError **)error {
    return [self optionGroup:@"skyOptions" type:@"sky" spec:spec error:error];
}

- (MSFPropertyGroup *)lightWithSpec:(MSFSpec *)spec error:(NSError **)error {
    return [self optionGroup:@"lightOptions" type:@"light" spec:spec error:error];
}

// --- objects this map owns ---------------------------------------------------------------------

- (MSFMassifSource *)source:(NSString *)objectId spec:(MSFSpec *)spec error:(NSError **)error {
    MSFMassifSource *source = [MSFMassif source:objectId spec:spec error:error];
    if (source) {
        [self own:@"source" objectId:objectId];
    }
    return source;
}

- (MSFMassifObject *)style:(NSString *)objectId spec:(MSFSpec *)spec error:(NSError **)error {
    MSFMassifObject *style = [MSFMassif style:objectId spec:spec error:error];
    if (style) {
        [self own:@"style" objectId:objectId];
    }
    return style;
}

- (MSFMassifObject *)object:(NSString *)kind
                   objectId:(NSString *)objectId
                       spec:(MSFSpec *)spec
                      error:(NSError **)error {
    MSFMassifObject *object = [MSFMassif object:kind objectId:objectId spec:spec error:error];
    if (object) {
        [self own:kind objectId:objectId];
    }
    return object;
}

// --- markers and popups ------------------------------------------------------------------------

- (MSFMassifElements *)elements {
    if (!_elements) {
        NSString *objectId = [NSString stringWithFormat:@"%@.elements", _options.objectId ?: @"map"];
        _elements = [[MSFMassifElements alloc] initWithMap:self objectId:objectId sourceSpec:nil];
    }
    return _elements;
}

- (MSFMassifObject *)addMarker:(MSFSpec *)spec error:(NSError **)error {
    return [self.elements add:spec error:error];
}

- (MSFMassifObject *)addPopup:(MSFSpec *)spec error:(NSError **)error {
    return [self.elements add:spec error:error];
}

- (MSFMassifLayer *)adoptLayer:(NSString *)objectId atIndex:(int)index {
    MSFMassifLayer *existing = [self layer:objectId];
    if (existing) {
        return existing;
    }
    if (index < 0 || index >= self.layerCount) {
        return nil;
    }
    int handle = [MSFMassifInterop adopt:@"layer"
                            objectId:objectId
                               layer:[[_view getLayers] get:index]];
    return handle == 0 ? nil
        : [[MSFMassifLayer alloc] initWithHandle:handle objectId:objectId map:self];
}

- (MSFMassifLayer *)layer:(NSString *)objectId {
    int handle = [MSFMassifApi findObject:@"layer" objectId:objectId];
    return handle == 0 ? nil
        : [[MSFMassifLayer alloc] initWithHandle:handle objectId:objectId map:self];
}

- (instancetype)remove:(MSFMassifLayer *)layer {
    MSFLayer *target = layer.layer;
    if (target) {
        [[_view getLayers] remove:target];
    }
    return self;
}

- (int)layerCount {
    return [[_view getLayers] count];
}

- (MSFLayer *)rawLayerAt:(int)index {
    return [[_view getLayers] get:index];
}

- (MSFMassifLayer *)attached:(MSFMassifLayer *)layer {
    return layer.objectId
        ? [[MSFMassifLayer alloc] initWithHandle:layer.handle objectId:layer.objectId map:self]
        : layer;
}

// --- events -----------------------------------------------------------------------------------

- (MSFPosition *)screenToMapX:(float)x y:(float)y {
    return [_camera screenToMapX:x y:y];
}

- (MSFScreenPoint *)mapToScreen:(MSFPosition *)pos {
    return [_camera mapToScreen:pos];
}

- (MSFSubscription *)onClick:(MSFMapClickHandler)handler {
    return [self on:@"map.clicked" kind:MSFEventKindClick coalesce:NO block:handler];
}

- (MSFSubscription *)onMove:(MSFMapEventHandler)handler {
    return [self on:@"map.moved" kind:MSFEventKindPlain coalesce:NO block:handler];
}

- (MSFSubscription *)onIdle:(MSFMapEventHandler)handler {
    return [self on:@"map.idle" kind:MSFEventKindPlain coalesce:NO block:handler];
}

- (MSFSubscription *)onStable:(MSFMapEventHandler)handler {
    return [self on:@"map.stable" kind:MSFEventKindPlain coalesce:NO block:handler];
}

- (MSFSubscription *)onInteraction:(MSFMapInteractionHandler)handler {
    // Coalesced: this fires per frame while a finger is down, so a pending one is replaced rather
    // than queued and a slow handler cannot flood the loop.
    return [self on:@"map.interaction" kind:MSFEventKindInteraction coalesce:YES block:handler];
}

- (MSFSubscription *)on:(NSString *)event
                   kind:(MSFEventKind)kind
               coalesce:(BOOL)coalesce
                  block:(id)block {
    [self bridge];
    return [_options subscribe:event
                          kind:kind
                      delivery:MSFDeliveryMain
                      coalesce:coalesce
                    projection:self.eventProjection
                         block:block
                     consuming:NO];
}

/**
 * Installs the map's event bridge on first subscription, chaining to whatever listener the app
 * already had - a single listener slot means adopting the facade would otherwise silently
 * disconnect its existing handlers.
 */
- (void)bridge {
    if (_bridged) {
        return;
    }
    MSFMapEventListener *chained = [_view getMapEventListener];
    [_view setMapEventListener:[MSFMassifInterop createEventBridge:_options.handle chained:chained]];
    _bridged = YES;
}

- (instancetype)offAll {
    [_options offAll];
    return self;
}

/**
 * Releasing what it BUILT is what makes ids reusable: a screen that opens, builds "basemap" and
 * closes can be opened again with a different spec under the same name. Layers merely added with
 * -add: are the caller's and are left alone.
 */
- (void)detach {
    [_options offAll];
    for (NSArray<NSString *> *object in _owned) {
        if ([object[0] isEqualToString:@"layer"]) {
            MSFMassifLayer *layer = [self layer:object[1]];
            if (layer.layer) {
                [[_view getLayers] remove:layer.layer];
            }
        }
        [MSFMassifApi unregisterObject:object[0] objectId:object[1]];
    }
    [_owned removeAllObjects];
    _elements = nil;
    if (_options.objectId) {
        [MSFMassifApi unregisterObject:kMapKind objectId:_options.objectId];
    }
}

@end
