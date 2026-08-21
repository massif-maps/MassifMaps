#import "MSFMassifInternal.h"
#import "MSFMassifApi.h"
#import "MSFEventListener.h"
#import "MSFBinaryData.h"
#import "MSFMapPos.h"
#import "MSFMapBounds.h"
#import "MSFLayer.h"
#import "MSFVectorTileLayer.h"
#import "MSFVectorTileEventListener.h"

/**
 * The director the facade calls, turning one subscription into one block call.
 *
 * Retained by the block registry below for as long as the subscription lives - the SDK holds the
 * listener weakly through a raw pointer, so nothing else keeps it alive.
 */
@interface MSFBlockListener : MSFEventListener
@property (nonatomic, copy) id block;
@property (nonatomic, assign) MSFEventKind kind;
@property (nonatomic, assign) BOOL consuming;
@end

@implementation MSFBlockListener

- (BOOL)onEvent:(int)target event:(NSString *)event payload:(int)payload {
    MSFMapEvent *typed = nil;
    switch (self.kind) {
    case MSFEventKindClick:
        typed = [[MSFMapClickEvent alloc] initWithTarget:target name:event payload:payload];
        break;
    case MSFEventKindInteraction:
        typed = [[MSFMapInteractionEvent alloc] initWithTarget:target name:event payload:payload];
        break;
    case MSFEventKindTileClick:
        typed = [[MSFVectorTileClickEvent alloc] initWithTarget:target name:event payload:payload];
        break;
    case MSFEventKindElementClick:
        typed = [[MSFVectorElementClickEvent alloc] initWithTarget:target name:event payload:payload];
        break;
    default:
        typed = [[MSFMapEvent alloc] initWithTarget:target name:event payload:payload];
        break;
    }
    if (self.consuming) {
        MSFVectorTileClickFilter filter = self.block;
        return filter((MSFVectorTileClickEvent *)typed);
    }
    void (^handler)(id) = self.block;
    handler(typed);
    return NO;
}

@end

@implementation MSFMassifObject {
    int _handle;
    NSString *_kind;
    NSString *_objectId;
}

- (instancetype)initWithHandle:(int)handle kind:(NSString *)kind objectId:(NSString *)objectId {
    if ((self = [super init])) {
        _handle = handle;
        _kind = [kind copy];
        _objectId = [objectId copy];
    }
    return self;
}

- (int)handle {
    return _handle;
}

- (NSString *)objectId {
    return _objectId;
}

- (BOOL)isValid {
    return _handle != 0 && [MSFMassifApi isValid:_handle];
}

// --- properties -------------------------------------------------------------------------------

- (BOOL)set:(NSString *)path value:(id)value {
    int result;
    if ([value isKindOfClass:[MSFMapPos class]]) {
        result = [MSFMassifApi setString:_handle path:path value:[MSFValues jsonFromPos:value]];
    } else if ([value isKindOfClass:[MSFMapBounds class]]) {
        MSFMapBounds *bounds = value;
        result = [MSFMassifApi setString:_handle path:path
            value:[NSString stringWithFormat:@"[%@,%@]",
                   [MSFValues jsonFromPos:[bounds getMin]],
                   [MSFValues jsonFromPos:[bounds getMax]]]];
    } else if ([value isKindOfClass:[NSNumber class]]) {
        NSNumber *number = value;
        const char *type = number.objCType;
        if (strcmp(type, @encode(BOOL)) == 0 || strcmp(type, @encode(char)) == 0) {
            result = [MSFMassifApi setBool:_handle path:path value:number.boolValue];
        } else if (strcmp(type, @encode(double)) == 0 || strcmp(type, @encode(float)) == 0) {
            result = [MSFMassifApi setFloat:_handle path:path value:number.doubleValue];
        } else {
            result = [MSFMassifApi setInt:_handle path:path value:number.longLongValue];
        }
    } else {
        result = [MSFMassifApi setString:_handle path:path value:[value description]];
    }
    return result == 0;
}

- (double)getDouble:(NSString *)path defaultValue:(double)defaultValue {
    return [MSFMassifApi getFloat:_handle path:path defaultValue:defaultValue];
}

- (long long)getLong:(NSString *)path defaultValue:(long long)defaultValue {
    return [MSFMassifApi getInt:_handle path:path defaultValue:defaultValue];
}

- (BOOL)getBool:(NSString *)path defaultValue:(BOOL)defaultValue {
    return [MSFMassifApi getBool:_handle path:path defaultValue:defaultValue];
}

- (NSString *)getString:(NSString *)path defaultValue:(NSString *)defaultValue {
    return [MSFValues string:_handle path:path defaultValue:defaultValue];
}

- (MSFMapPos *)getPos:(NSString *)path {
    return [MSFValues posFromJson:[MSFMassifApi getPos:_handle path:path projection:@""]];
}

- (MSFMapPos *)getPos:(NSString *)path projection:(NSString *)projection {
    return [MSFValues posFromJson:[MSFMassifApi getPos:_handle path:path projection:projection]];
}

- (MSFMapBounds *)getBounds:(NSString *)path {
    NSString *json = [MSFMassifApi getPos:_handle path:path projection:@""];
    NSData *data = [json dataUsingEncoding:NSUTF8StringEncoding];
    id parsed = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    if (![parsed isKindOfClass:[NSArray class]] || [parsed count] != 2) {
        return nil;
    }
    NSArray *pair = parsed;
    NSData *minData = [NSJSONSerialization dataWithJSONObject:pair[0] options:0 error:nil];
    NSData *maxData = [NSJSONSerialization dataWithJSONObject:pair[1] options:0 error:nil];
    MSFMapPos *min = [MSFValues posFromJson:[[NSString alloc] initWithData:minData encoding:NSUTF8StringEncoding]];
    MSFMapPos *max = [MSFValues posFromJson:[[NSString alloc] initWithData:maxData encoding:NSUTF8StringEncoding]];
    return min && max ? [[MSFMapBounds alloc] initWithMin:min max:max] : nil;
}

- (MSFPropertyGroup *)group:(NSString *)prefix {
    return [[MSFPropertyGroup alloc] initWithObject:self prefix:prefix];
}

// --- methods ----------------------------------------------------------------------------------

- (MSFMassifObject *)call:(NSString *)method args:(NSArray *)args error:(NSError **)error {
    @try {
        int result = [MSFMassifApi call:_handle method:method argsJson:[MSFValues argsJson:args]];
        return result == 0 ? nil
            : [[MSFMassifObject alloc] initWithHandle:result kind:nil objectId:nil];
    } @catch (NSException *exception) {
        if (error) {
            *error = [MSFValues errorWithResult:12
                message:[NSString stringWithFormat:@"call '%@' failed: %@", method, exception.reason]];
        }
        return nil;
    }
}

- (int)callAsync:(NSString *)method
            args:(NSArray *)args
      completion:(void (^)(MSFMassifObject * _Nullable))completion {
    // One event name per call, so two calls to the same method cannot cross, and a holder for the
    // subscription so the handler can remove ITSELF - off(subscription) is what releases the
    // listener, which offEvent cannot do because it does not know the ids it removed.
    static int counter = 0;
    NSString *event = [NSString stringWithFormat:@"%@.done#%d", method, ++counter];
    __block MSFSubscription *self_ = nil;
    self_ = [self subscribe:event
                       kind:MSFEventKindPlain
                   delivery:MSFDeliveryMain
                   coalesce:NO
                 projection:nil
                      block:^(MSFMapEvent *e) {
                          [self_ invalidate];
                          completion(e.hasPayload
                              ? [[MSFMassifObject alloc] initWithHandle:e.payload kind:nil objectId:nil]
                              : nil);
                      }
                  consuming:NO];
    @try {
        return [MSFMassifApi callAsync:_handle
                                method:method
                              argsJson:[MSFValues argsJson:args]
                                 event:event];
    } @catch (NSException *exception) {
        [self_ invalidate];
        return 0;
    }
}

- (BOOL)cancelCall:(int)call {
    return [MSFMassifApi cancelCall:call];
}

- (int)cancelAllCalls {
    return [MSFMassifApi cancelCalls:_handle];
}

- (NSData *)data:(NSString *)path {
    MSFBinaryData *blob = [MSFMassifApi getData:_handle path:path];
    if (!blob) {
        return nil;
    }
    return [NSData dataWithBytes:[blob getData] length:[blob size]];
}

- (NSData *)doubles {
    return [MSFMassifApi getDoubles:_handle];
}

- (NSString *)json {
    return [MSFMassifApi getString:_handle path:@"" defaultValue:@""];
}

// --- events -----------------------------------------------------------------------------------

- (MSFSubscription *)on:(NSString *)event handler:(MSFMapEventHandler)handler {
    return [self subscribe:event
                      kind:MSFEventKindPlain
                  delivery:MSFDeliveryMain
                  coalesce:NO
                projection:nil
                     block:handler
                 consuming:NO];
}

- (MSFSubscription *)subscribe:(NSString *)event
                          kind:(MSFEventKind)kind
                      delivery:(MSFDelivery)delivery
                      coalesce:(BOOL)coalesce
                    projection:(NSString *)projection
                         block:(id)block
                     consuming:(BOOL)consuming {
    if (!block) {
        return nil;
    }
    MSFBlockListener *listener = [[MSFBlockListener alloc] init];
    listener.block = block;
    listener.kind = kind;
    listener.consuming = consuming;

    int subscription = [MSFMassifApi on:_handle
                                  event:event
                               listener:listener
                               delivery:(int)delivery
                               coalesce:coalesce
                             projection:projection ?: @""];
    return subscription == 0 ? nil : [[MSFSubscription alloc] initWithId:subscription];
}

- (int)off:(NSString *)event {
    return [MSFMassifApi offEvent:_handle event:event];
}

- (int)offAll {
    return [MSFMassifApi offAll:_handle];
}

- (BOOL)destroy {
    return [MSFMassifApi destroy:_handle];
}

- (BOOL)isEqual:(id)other {
    return [other isKindOfClass:[MSFMassifObject class]] && ((MSFMassifObject *)other).handle == _handle;
}

- (NSUInteger)hash {
    return (NSUInteger)_handle;
}

- (NSString *)description {
    return [NSString stringWithFormat:@"%@(%d)", NSStringFromClass([self class]), _handle];
}

@end

@implementation MSFPropertyGroup {
    MSFMassifObject *_target;
    NSString *_prefix;
}

- (instancetype)initWithObject:(MSFMassifObject *)target prefix:(NSString *)prefix {
    if ((self = [super init])) {
        _target = target;
        _prefix = prefix.length == 0 || [prefix hasSuffix:@"."]
            ? [prefix copy] : [prefix stringByAppendingString:@"."];
    }
    return self;
}

- (MSFMassifObject *)object {
    return _target;
}

- (BOOL)set:(NSString *)name value:(id)value {
    return [_target set:[_prefix stringByAppendingString:name] value:value];
}

- (double)getDouble:(NSString *)name defaultValue:(double)defaultValue {
    return [_target getDouble:[_prefix stringByAppendingString:name] defaultValue:defaultValue];
}

- (long long)getLong:(NSString *)name defaultValue:(long long)defaultValue {
    return [_target getLong:[_prefix stringByAppendingString:name] defaultValue:defaultValue];
}

- (BOOL)getBool:(NSString *)name defaultValue:(BOOL)defaultValue {
    return [_target getBool:[_prefix stringByAppendingString:name] defaultValue:defaultValue];
}

- (NSString *)getString:(NSString *)name defaultValue:(NSString *)defaultValue {
    return [_target getString:[_prefix stringByAppendingString:name] defaultValue:defaultValue];
}

- (MSFMapPos *)getPos:(NSString *)name {
    return [_target getPos:[_prefix stringByAppendingString:name]];
}

@end
