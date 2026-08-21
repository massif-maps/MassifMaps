#import "MSFMassifInternal.h"
#import "MSFMassifApi.h"
#import "MSFMapPos.h"
#import "ui/MapView.h"
#import "MSFLayer.h"
#import "MSFLayers.h"
#import "MSFMapEventListener.h"

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
    [_view flyTo:pos zoom:zoom rotation:rotation tilt:tilt durationSeconds:[self take]];
    return self;
}

- (instancetype)moveTo:(MSFMapPos *)pos zoom:(float)zoom {
    [_view flyTo:pos zoom:zoom durationSeconds:[self take]];
    return self;
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
}

+ (instancetype)attach:(MSFMapView *)view {
    return [self attach:view objectId:@"main"];
}

+ (instancetype)attach:(MSFMapView *)view objectId:(NSString *)objectId {
    if (!view) {
        return nil;
    }
    int handle = [MSFMassifApi findObject:kMapKind objectId:objectId];
    if (handle == 0) {
        handle = [MSFMassifApi registerOptions:kMapKind objectId:objectId options:[view getOptions]];
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
    return layer ? [self add:layer] : nil;
}

- (MSFMassifLayer *)adoptLayer:(NSString *)objectId atIndex:(int)index {
    MSFMassifLayer *existing = [self layer:objectId];
    if (existing) {
        return existing;
    }
    if (index < 0 || index >= self.layerCount) {
        return nil;
    }
    int handle = [MSFMassifApi registerLayer:@"layer"
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

- (void)detach {
    [_options offAll];
    if (_options.objectId) {
        [MSFMassifApi unregisterObject:kMapKind objectId:_options.objectId];
    }
}

@end
