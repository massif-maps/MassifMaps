#import "MSFMassifInternal.h"
#import "MSFMassifElements.h"
#import "MSFMassifApi.h"

@implementation MSFMassifElements {
    __weak MSFMassifMap *_map;
    NSString *_objectId;
    MSFSpec *_sourceSpec;
    MSFMassifObject *_source;
    MSFMassifLayer *_layer;
    int _counter;
}

- (instancetype)initWithMap:(MSFMassifMap *)map
                   objectId:(NSString *)objectId
                 sourceSpec:(MSFSpec *)sourceSpec {
    if ((self = [super init])) {
        _map = map;
        _objectId = [objectId copy];
        // lon/lat, matching the rest of this API. A map that works in metres passes its own.
        _sourceSpec = sourceSpec ?: [[MSFSpec of:@"local"]
            set:@"projection" value:[MSFSpec of:@"EPSG:4326"]];
    }
    return self;
}

/** Built on first use, so a map that never adds an element pays nothing. */
- (BOOL)buildWithError:(NSError **)error {
    if (_layer) {
        return YES;
    }
    MSFMassifMap *map = _map;
    if (!map) {
        return NO;
    }
    _source = [map object:@"source" objectId:_objectId spec:_sourceSpec error:error];
    if (!_source) {
        return NO;
    }
    _layer = [map addLayer:_objectId
                      spec:[[MSFSpec of:@"elements"] set:@"source" value:_objectId]
                     error:error];
    return _layer != nil;
}

- (MSFMassifObject *)add:(MSFSpec *)spec error:(NSError **)error {
    if (![self buildWithError:error]) {
        return nil;
    }
    NSString *elementId = [NSString stringWithFormat:@"%@.el%d", _objectId, ++_counter];
    MSFMassifObject *element = [_map object:@"element" objectId:elementId spec:spec error:error];
    if (!element) {
        return nil;
    }
    MSFMassifObject *result = [_source call:@"add" args:@[ @(element.handle) ] error:error];
    (void)result;
    return element;
}

- (MSFMassifObject *)style:(NSString *)styleId spec:(MSFSpec *)spec error:(NSError **)error {
    NSString *fullId = [NSString stringWithFormat:@"%@.%@", _objectId, styleId];
    return [_map object:@"elementstyle" objectId:fullId spec:spec error:error];
}

- (BOOL)remove:(MSFMassifObject *)element {
    if (!_source || !element) {
        return NO;
    }
    MSFMassifObject *result = [_source call:@"remove" args:@[ @(element.handle) ] error:nil];
    return result && [result getBool:@"" defaultValue:NO];
}

- (instancetype)clear {
    if (_source) {
        [_source call:@"clear" args:nil error:nil];
    }
    return self;
}

- (MSFSubscription *)onClick:(MSFVectorElementClickHandler)handler {
    return [self buildWithError:nil] ? [_layer onElementClick:handler] : nil;
}

- (MSFSubscription *)consumeClick:(MSFVectorElementClickFilter)handler {
    return [self buildWithError:nil] ? [_layer consumeElementClick:handler] : nil;
}

- (MSFMassifLayer *)layer {
    [self buildWithError:nil];
    return _layer;
}

- (MSFMassifObject *)source {
    [self buildWithError:nil];
    return _source;
}

@end
