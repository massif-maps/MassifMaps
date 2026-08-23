#import "MSFMassifInternal.h"
#import "MSFMassifApi.h"
#import "MSFMassifInterop.h"
#import "MSFTileDataSource.h"
#import "MSFLayer.h"
#import "MSFAssetPackage.h"

NSString * const MSFMassifErrorDomain = @"MSFMassifErrorDomain";

@implementation MSFValues

+ (MSFPosition *)posFromJson:(NSString *)json {
    if (json.length == 0) {
        return nil;
    }
    NSError *error = nil;
    id parsed = [NSJSONSerialization JSONObjectWithData:[json dataUsingEncoding:NSUTF8StringEncoding]
                                                options:NSJSONReadingFragmentsAllowed
                                                  error:&error];
    if (![parsed isKindOfClass:[NSArray class]]) {
        return nil;
    }
    // Typed before -count is sent: on a bare `id` the selector is ambiguous the moment this
    // translation unit sees two classes that declare one.
    NSArray *array = (NSArray *)parsed;
    if (array.count < 2) {
        return nil;
    }
    double z = array.count > 2 ? [array[2] doubleValue] : 0;
    return [[MSFPosition alloc] initWithLng:[array[0] doubleValue] lat:[array[1] doubleValue] alt:z];
}

+ (MSFBounds *)boundsFromJson:(NSString *)json {
    if (json.length == 0) {
        return nil;
    }
    id parsed = [NSJSONSerialization JSONObjectWithData:[json dataUsingEncoding:NSUTF8StringEncoding]
                                                options:NSJSONReadingFragmentsAllowed
                                                  error:nil];
    if (![parsed isKindOfClass:[NSArray class]] || ((NSArray *)parsed).count != 2) {
        return nil;
    }
    NSArray *array = (NSArray *)parsed;
    MSFPosition *min = [self posFromJson:[self jsonOf:array[0]]];
    MSFPosition *max = [self posFromJson:[self jsonOf:array[1]]];
    return min && max ? [[MSFBounds alloc] initWithMin:min max:max] : nil;
}

+ (MSFScreenPoint *)screenPointFromJson:(NSString *)json {
    MSFPosition *pos = [self posFromJson:json];
    return pos ? [[MSFScreenPoint alloc] initWithX:(float)pos.lng y:(float)pos.lat] : nil;
}

/** Re-serialises one element, so the pair codec reuses the position one instead of copying it. */
+ (NSString *)jsonOf:(id)value {
    NSData *data = [NSJSONSerialization dataWithJSONObject:value options:0 error:nil];
    return data ? [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] : @"";
}

+ (NSString *)jsonFromPos:(MSFPosition *)pos {
    return [NSString stringWithFormat:@"[%.17g,%.17g,%.17g]", pos.lng, pos.lat, pos.alt];
}

+ (NSString *)argsJson:(NSArray *)args {
    if (args.count == 0) {
        return @"";
    }
    NSMutableArray *converted = [NSMutableArray arrayWithCapacity:args.count];
    for (id arg in args) {
        if ([arg isKindOfClass:[MSFPosition class]]) {
            MSFPosition *pos = arg;
            [converted addObject:@[ @(pos.lng), @(pos.lat), @(pos.alt) ]];
        } else if ([arg isKindOfClass:[MSFBounds class]]) {
            MSFBounds *bounds = arg;
            [converted addObject:@[ @[ @(bounds.min.lng), @(bounds.min.lat) ],
                                    @[ @(bounds.max.lng), @(bounds.max.lat) ] ]];
        } else if ([arg isKindOfClass:[MSFScreenRect class]]) {
            MSFScreenRect *rect = arg;
            [converted addObject:@[ @[ @(rect.min.x), @(rect.min.y) ],
                                    @[ @(rect.max.x), @(rect.max.y) ] ]];
        } else {
            [converted addObject:arg];
        }
    }
    NSData *data = [NSJSONSerialization dataWithJSONObject:converted options:0 error:nil];
    return data ? [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] : @"";
}

// Stands in for "no default": Swig's std::string typemap rejects nil, which is what a nullable
// getter wants to pass. Contains a NUL, so no real value equals it.
static NSString * const kAbsent = @"\0massif:absent";

+ (NSString *)stringOrNil:(int)handle path:(NSString *)path {
    NSString *value = [MSFMassifApi getString:handle path:path defaultValue:kAbsent];
    return [kAbsent isEqualToString:value] ? nil : value;
}

+ (NSString *)string:(int)handle path:(NSString *)path defaultValue:(NSString *)defaultValue {
    NSString *value = [self stringOrNil:handle path:path];
    return value ?: defaultValue;
}

+ (NSError *)errorWithResult:(int)result message:(NSString *)message {
    return [NSError errorWithDomain:MSFMassifErrorDomain
                               code:result
                           userInfo:@{ NSLocalizedDescriptionKey: message }];
}

@end

@implementation MSFSpec {
    NSMutableDictionary *_values;
}

+ (instancetype)of:(NSString *)type {
    MSFSpec *spec = [[MSFSpec alloc] init];
    return [spec set:@"type" value:type];
}

+ (instancetype)object {
    MSFSpec *spec = [[MSFSpec alloc] init];
    return spec;
}

- (instancetype)init {
    if ((self = [super init])) {
        _values = [NSMutableDictionary dictionary];
    }
    return self;
}

- (NSMutableDictionary *)values {
    return _values;
}

- (instancetype)set:(NSString *)key value:(id)value {
    _values[key] = [value isKindOfClass:[MSFSpec class]] ? ((MSFSpec *)value).values : value;
    return self;
}

- (NSString *)json {
    NSData *data = [NSJSONSerialization dataWithJSONObject:_values options:0 error:nil];
    return data ? [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] : @"{}";
}

- (NSString *)description {
    return self.json;
}

@end

@implementation MSFSubscription {
    int _id;
    id _listener;
}

- (instancetype)initWithId:(int)subscriptionId listener:(id)listener {
    if ((self = [super init])) {
        _id = subscriptionId;
        // The C++ side keeps the director as a raw pointer, so this is the only strong reference.
        _listener = listener;
    }
    return self;
}

- (BOOL)isActive {
    return _id != 0;
}

- (void)invalidate {
    if (_id != 0) {
        [MSFMassifApi off:_id];
        _id = 0;
        _listener = nil;
    }
}

// A subscription stored in a property dies with its owner, rather than outliving the object whose
// state its handler reads.
- (void)dealloc {
    [self invalidate];
}

@end

@implementation MSFMassif

+ (int)create:(NSString *)kind objectId:(NSString *)objectId spec:(MSFSpec *)spec error:(NSError **)error {
    if (!spec) {
        if (error) {
            *error = [MSFValues errorWithResult:9 message:@"Null spec"];
        }
        return 0;
    }
    @try {
        return [MSFMassifApi create:kind objectId:objectId json:spec.json];
    } @catch (NSException *exception) {
        if (error) {
            *error = [MSFValues errorWithResult:9
                message:[NSString stringWithFormat:@"Cannot create %@ '%@': %@",
                         kind, objectId, exception.reason]];
        }
        return 0;
    }
}

+ (MSFMassifSource *)source:(NSString *)objectId spec:(MSFSpec *)spec error:(NSError **)error {
    int handle = [self create:@"source" objectId:objectId spec:spec error:error];
    return handle == 0 ? nil
        : [[MSFMassifSource alloc] initWithHandle:handle kind:@"source" objectId:objectId];
}

+ (MSFMassifSource *)source:(NSString *)objectId {
    int handle = [MSFMassifApi findObject:@"source" objectId:objectId];
    return handle == 0 ? nil
        : [[MSFMassifSource alloc] initWithHandle:handle kind:@"source" objectId:objectId];
}

+ (MSFMassifObject *)style:(NSString *)objectId spec:(MSFSpec *)spec error:(NSError **)error {
    int handle = [self create:@"style" objectId:objectId spec:spec error:error];
    return handle == 0 ? nil
        : [[MSFMassifObject alloc] initWithHandle:handle kind:@"style" objectId:objectId];
}

+ (MSFMassifObject *)style:(NSString *)objectId {
    int handle = [MSFMassifApi findObject:@"style" objectId:objectId];
    return handle == 0 ? nil
        : [[MSFMassifObject alloc] initWithHandle:handle kind:@"style" objectId:objectId];
}

+ (MSFMassifLayer *)layer:(NSString *)objectId spec:(MSFSpec *)spec error:(NSError **)error {
    int handle = [self create:@"layer" objectId:objectId spec:spec error:error];
    return handle == 0 ? nil
        : [[MSFMassifLayer alloc] initWithHandle:handle objectId:objectId map:nil];
}

+ (MSFMassifLayer *)layer:(NSString *)objectId {
    int handle = [MSFMassifApi findObject:@"layer" objectId:objectId];
    return handle == 0 ? nil
        : [[MSFMassifLayer alloc] initWithHandle:handle objectId:objectId map:nil];
}

+ (MSFMassifLayer *)adoptLayer:(NSString *)objectId layer:(MSFLayer *)layer {
    int handle = [MSFMassifInterop adopt:@"layer" objectId:objectId layer:layer];
    return handle == 0 ? nil
        : [[MSFMassifLayer alloc] initWithHandle:handle objectId:objectId map:nil];
}

+ (MSFMassifSource *)adoptSource:(NSString *)objectId source:(MSFTileDataSource *)source {
    int handle = [MSFMassifInterop adopt:@"source" objectId:objectId source:source];
    return handle == 0 ? nil
        : [[MSFMassifSource alloc] initWithHandle:handle kind:@"source" objectId:objectId];
}

+ (MSFMassifObject *)adoptAssets:(NSString *)objectId assets:(MSFAssetPackage *)assets {
    int handle = [MSFMassifInterop adopt:@"assets" objectId:objectId assets:assets];
    return handle == 0 ? nil
        : [[MSFMassifObject alloc] initWithHandle:handle kind:@"assets" objectId:objectId];
}

+ (MSFMassifObject *)object:(NSString *)kind
                   objectId:(NSString *)objectId
                       spec:(MSFSpec *)spec
                      error:(NSError **)error {
    int handle = [self create:kind objectId:objectId spec:spec error:error];
    return handle ? [[MSFMassifObject alloc] initWithHandle:handle kind:kind objectId:objectId] : nil;
}

+ (BOOL)has:(NSString *)kind objectId:(NSString *)objectId {
    return [MSFMassifApi findObject:kind objectId:objectId] != 0;
}

+ (BOOL)destroy:(NSString *)kind objectId:(NSString *)objectId {
    return [MSFMassifApi unregisterObject:kind objectId:objectId];
}

+ (MSFTileDataSource *)rawSource:(NSString *)objectId {
    return [MSFMassifInterop getSource:objectId];
}

@end
