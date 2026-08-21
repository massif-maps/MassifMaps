#import "MSFMassifInternal.h"
#import "MSFMassifApi.h"
#import "MSFMapPos.h"

@implementation MSFMapEvent {
    int _target;
    int _payload;
    NSString *_name;
}

- (instancetype)initWithTarget:(int)target name:(NSString *)name payload:(int)payload {
    if ((self = [super init])) {
        _target = target;
        _name = [name copy];
        _payload = payload;
    }
    return self;
}

- (int)payload {
    return _payload;
}

- (NSString *)name {
    return _name;
}

- (MSFMassifObject *)source {
    return [[MSFMassifObject alloc] initWithHandle:_target kind:nil objectId:nil];
}

- (BOOL)hasPayload {
    return _payload != 0;
}

- (NSString *)get:(NSString *)path {
    return [MSFValues stringOrNil:_payload path:path];
}

- (double)getDouble:(NSString *)path defaultValue:(double)defaultValue {
    return [MSFMassifApi getFloat:_payload path:path defaultValue:defaultValue];
}

- (MSFMapPos *)getPos:(NSString *)path projection:(NSString *)projection {
    return [MSFValues posFromJson:[MSFMassifApi getPos:_payload path:path projection:projection]];
}

@end

@implementation MSFMapClickEvent

- (MSFMapPos *)position {
    return [MSFValues posFromJson:[MSFMassifApi getPos:self.payload path:@"clickPos" projection:@""]];
}

- (int)clickType {
    return (int)[MSFMassifApi getInt:self.payload path:@"clickInfo.clickType" defaultValue:-1];
}

@end

@implementation MSFMapInteractionEvent

- (BOOL)isPan {
    return [MSFMassifApi getBool:self.payload path:@"panning" defaultValue:NO];
}

- (BOOL)isZoom {
    return [MSFMassifApi getBool:self.payload path:@"zooming" defaultValue:NO];
}

- (BOOL)isRotate {
    return [MSFMassifApi getBool:self.payload path:@"rotating" defaultValue:NO];
}

- (BOOL)isTilt {
    return [MSFMassifApi getBool:self.payload path:@"tilting" defaultValue:NO];
}

@end

@implementation MSFVectorTileClickEvent

- (long long)featureId {
    return [MSFMassifApi getInt:self.payload path:@"featureId" defaultValue:-1];
}

- (NSString *)layerName {
    return [MSFMassifApi getString:self.payload path:@"featureLayerName" defaultValue:@""];
}

- (MSFMapPos *)position {
    return [MSFValues posFromJson:[MSFMassifApi getPos:self.payload path:@"featurePos" projection:@""]];
}

- (MSFMapPos *)clickPosition {
    return [MSFValues posFromJson:[MSFMassifApi getPos:self.payload path:@"clickPos" projection:@""]];
}

- (int)geometryType {
    return (int)[MSFMassifApi getInt:self.payload path:@"feature.geometry.type" defaultValue:-1];
}

- (NSString *)property:(NSString *)key {
    return [MSFValues stringOrNil:self.payload
                             path:[@"feature.properties." stringByAppendingString:key]];
}

- (double)propertyDouble:(NSString *)key defaultValue:(double)defaultValue {
    return [MSFMassifApi getFloat:self.payload
                             path:[@"feature.properties." stringByAppendingString:key]
                     defaultValue:defaultValue];
}

- (NSString *)properties {
    return [MSFMassifApi getString:self.payload path:@"feature.properties" defaultValue:@"{}"];
}

- (NSString *)geoJson {
    return [MSFMassifApi getString:self.payload path:@"feature.geometryGeoJSON" defaultValue:@""];
}

@end

@implementation MSFVectorElementClickEvent

- (MSFMapPos *)position {
    return [MSFValues posFromJson:[MSFMassifApi getPos:self.payload path:@"elementClickPos" projection:@""]];
}

- (MSFMapPos *)clickPosition {
    return [MSFValues posFromJson:[MSFMassifApi getPos:self.payload path:@"clickPos" projection:@""]];
}

- (int)clickType {
    return (int)[MSFMassifApi getInt:self.payload path:@"clickInfo.clickType" defaultValue:-1];
}

@end
