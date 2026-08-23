#import "MSFMassifInternal.h"
#import "MSFMassifApi.h"
#import "MSFMassifInterop.h"
#import "MSFBinaryData.h"
#import "MSFValueTypes.h"
#import "MSFLayer.h"
#import "MSFLayers.h"
#import "MSFVectorLayer.h"
#import "MSFVectorElementEventListener.h"
#import "ui/MapView.h"
#import "MSFVectorTileLayer.h"
#import "MSFVectorTileEventListener.h"

@implementation MSFMassifSource

- (NSData *)loadTileX:(int)x y:(int)y zoom:(int)zoom {
    MSFMassifObject *tile = [self call:@"loadTile" args:@[ @[ @(x), @(y), @(zoom) ] ] error:nil];
    if (!tile) {
        return nil;
    }
    NSData *data = [tile data:@"data"];
    [tile destroy];
    return data;
}

- (void)loadTileX:(int)x
                y:(int)y
             zoom:(int)zoom
       completion:(void (^)(NSData * _Nullable))completion {
    // Returns void so Swift imports it as `async`. The generic callAsync:args:completion: is the
    // form that hands back a call id for cancellation.
    [self callAsync:@"loadTile"
               args:@[ @[ @(x), @(y), @(zoom) ] ]
         completion:^(MSFMassifObject *result) {
             completion(result ? [result data:@"data"] : nil);
         }];
}

- (int)createLayer:(NSString *)name {
    MSFMassifObject *result = [self call:@"createLayer" args:@[ name ] error:nil];
    return result ? (int)[result getLong:@"" defaultValue:-1] : -1;
}

- (BOOL)setLayerGeoJSON:(int)layer geoJson:(NSString *)geoJson {
    return [self call:@"setLayerGeoJSON" args:@[ @(layer), geoJson ] error:nil] != nil;
}

@end

@implementation MSFMassifLayer {
    __weak MSFMassifMap *_map;
    BOOL _bridged;
    BOOL _elementBridged;
}

- (instancetype)initWithHandle:(int)handle objectId:(NSString *)objectId map:(MSFMassifMap *)map {
    if ((self = [super initWithHandle:handle kind:@"layer" objectId:objectId])) {
        _map = map;
    }
    return self;
}

- (instancetype)opacity:(float)opacity {
    [self set:@"opacity" value:@(opacity)];
    return self;
}

- (instancetype)visible:(BOOL)visible {
    [self set:@"visible" value:@(visible)];
    return self;
}

- (instancetype)moveTo:(int)index {
    MSFLayer *target = self.layer;
    if (target && _map) {
        [[_map.view getLayers] insert:index layer:target];
    }
    return self;
}

- (instancetype)detach {
    MSFLayer *target = self.layer;
    if (target && _map) {
        [[_map.view getLayers] remove:target];
    }
    return self;
}

- (MSFLayer *)layer {
    return self.objectId ? [MSFMassifInterop getLayer:self.objectId] : nil;
}

- (MSFSubscription *)onFeatureClick:(MSFVectorTileClickHandler)handler {
    return [self featureClick:handler consuming:NO];
}

- (MSFSubscription *)consumeFeatureClick:(MSFVectorTileClickFilter)handler {
    return [self featureClick:handler consuming:YES];
}

- (MSFSubscription *)featureClick:(id)block consuming:(BOOL)consuming {
    MSFLayer *target = self.layer;
    if (![target isKindOfClass:[MSFVectorTileLayer class]]) {
        return nil;
    }
    MSFVectorTileLayer *vector = (MSFVectorTileLayer *)target;
    if (!_bridged) {
        // Chains to whatever listener was already installed, so an app that also uses the object
        // API keeps working - there is only one listener slot.
        MSFVectorTileEventListener *chained = [vector getVectorTileEventListener];
        [vector setVectorTileEventListener:
            [MSFMassifInterop createVectorTileEventBridge:self.handle chained:chained]];
        _bridged = YES;
    }
    return [self subscribe:@"vectortile.clicked"
                      kind:MSFEventKindTileClick
                  delivery:consuming ? MSFDeliveryOrigin : MSFDeliveryMain
                  coalesce:NO
                projection:_map.eventProjection
                     block:block
                 consuming:consuming];
}

- (MSFSubscription *)onElementClick:(MSFVectorElementClickHandler)handler {
    return [self elementClick:handler consuming:NO];
}

- (MSFSubscription *)consumeElementClick:(MSFVectorElementClickFilter)handler {
    return [self elementClick:handler consuming:YES];
}

- (MSFSubscription *)elementClick:(id)block consuming:(BOOL)consuming {
    MSFLayer *target = self.layer;
    if (![target isKindOfClass:[MSFVectorLayer class]]) {
        return nil;
    }
    MSFVectorLayer *vector = (MSFVectorLayer *)target;
    if (!_elementBridged) {
        MSFVectorElementEventListener *chained = [vector getVectorElementEventListener];
        [vector setVectorElementEventListener:
            [MSFMassifInterop createVectorElementEventBridge:self.handle chained:chained]];
        _elementBridged = YES;
    }
    return [self subscribe:@"vectorelement.clicked"
                      kind:MSFEventKindElementClick
                  delivery:consuming ? MSFDeliveryOrigin : MSFDeliveryMain
                  coalesce:NO
                projection:_map.eventProjection
                     block:block
                 consuming:consuming];
}

- (NSData *)elevations:(NSArray<MSFPosition *> *)positions {
    NSMutableArray *argument = [NSMutableArray arrayWithCapacity:positions.count];
    for (MSFPosition *pos in positions) {
        [argument addObject:@[ @(pos.lng), @(pos.lat) ]];
    }
    MSFMassifObject *result = [self call:@"getElevations" args:@[ argument ] error:nil];
    if (!result) {
        return nil;
    }
    NSData *values = result.doubles;
    [result destroy];
    return values;
}

@end
