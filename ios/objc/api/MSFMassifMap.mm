#import "MSFMassifInternal.h"
#import "MSFMassifApi.h"
#import "MSFMapPos.h"
#import "ui/MapView.h"
#import "MSFLayer.h"
#import "MSFLayers.h"
#import "MSFMapEventListener.h"
#import "MSFMapBounds.h"
#import "MSFScreenPos.h"
#import "MSFScreenBounds.h"

#include "api/Context.h"

static NSString * const kMapKind = @"map";

@implementation MSFMapCamera {
    __weak MSFMapView *_view;
    float _duration;
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

- (float)take {
    float seconds = _duration;
    _duration = 0;
    return seconds;
}

- (instancetype)position:(MSFMapPos *)pos {
    [_view setFocusPos:pos durationSeconds:[self take]];
    return self;
}

- (instancetype)zoom:(float)zoom {
    [_view setZoom:zoom durationSeconds:[self take]];
    return self;
}

- (instancetype)rotation:(float)degrees {
    [_view setRotation:degrees durationSeconds:[self take]];
    return self;
}

- (instancetype)tilt:(float)degrees {
    [_view setTilt:degrees durationSeconds:[self take]];
    return self;
}

- (instancetype)moveTo:(MSFMapPos *)pos zoom:(float)zoom rotation:(float)rotation tilt:(float)tilt {
    float seconds = [self take];
    if (seconds > 0) {
        [_view flyTo:pos zoom:zoom rotation:rotation tilt:tilt durationSeconds:seconds];
    } else {
        // Not flyTo with a duration of 0: that means "pick a duration from the path", and a flight
        // needs a frame to set itself up against. This is the immediate move, and it works before
        // the map has drawn - which is when a screen usually points its camera.
        [_view moveTo:pos zoom:zoom rotation:rotation tilt:tilt];
    }
    return self;
}

- (instancetype)moveTo:(MSFMapPos *)pos zoom:(float)zoom {
    float seconds = [self take];
    if (seconds > 0) {
        [_view flyTo:pos zoom:zoom durationSeconds:seconds];
    } else {
        [_view moveTo:pos zoom:zoom];
    }
    return self;
}

- (instancetype)fitBounds:(MSFMapBounds *)bounds
             screenBounds:(MSFScreenBounds *)screenBounds
              integerZoom:(BOOL)integerZoom {
    [_view moveToFitBounds:bounds
              screenBounds:screenBounds
               integerZoom:integerZoom
           durationSeconds:[self take]];
    return self;
}

- (instancetype)fitBounds:(MSFMapBounds *)bounds {
    CGSize size = _view.bounds.size;
    MSFScreenBounds *whole =
        [[MSFScreenBounds alloc] initWithMin:[[MSFScreenPos alloc] initWithX:0 y:0]
                                         max:[[MSFScreenPos alloc] initWithX:size.width
                                                                           y:size.height]];
    return [self fitBounds:bounds screenBounds:whole integerZoom:NO];
}

- (MSFMapPos *)currentPosition {
    return [_view getFocusPos];
}

- (float)currentZoom {
    return [_view getZoom];
}

- (float)currentRotation {
    return [_view getRotation];
}

- (float)currentTilt {
    return [_view getTilt];
}

- (BOOL)isMoving {
    return [_view isFlightActive];
}

- (instancetype)stop {
    [_view stopFlight];
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
        handle = [MSFMassifApi adopt:kMapKind objectId:objectId options:[view getOptions]];
        if (handle == 0) {
            return nil;
        }
    }
    MSFMassifMap *map = [[MSFMassifMap alloc] init];
    map->_view = view;
    map->_options = [[MSFMassifObject alloc] initWithHandle:handle kind:kMapKind objectId:objectId];
    map->_camera = [[MSFMapCamera alloc] initWithView:view];
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
    int handle = [MSFMassifApi adopt:@"layer"
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

- (MSFMapPos *)screenToMapX:(float)x y:(float)y {
    return [_view screenToMap:[[MSFScreenPos alloc] initWithX:x y:y]];
}

- (MSFScreenPos *)mapToScreen:(MSFMapPos *)pos {
    return [_view mapToScreen:pos];
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
    [_view setMapEventListener:[MSFMassifApi createEventBridge:_options.handle chained:chained]];
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
