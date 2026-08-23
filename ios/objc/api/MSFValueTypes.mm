#import "MSFValueTypes.h"

@implementation MSFPosition

- (instancetype)initWithLng:(double)lng lat:(double)lat {
    return [self initWithLng:lng lat:lat alt:0];
}

- (instancetype)initWithLng:(double)lng lat:(double)lat alt:(double)alt {
    if ((self = [super init])) {
        _lng = lng;
        _lat = lat;
        _alt = alt;
    }
    return self;
}

+ (instancetype)positionWithLng:(double)lng lat:(double)lat {
    return [[self alloc] initWithLng:lng lat:lat alt:0];
}

- (BOOL)isEqual:(id)other {
    if (![other isKindOfClass:[MSFPosition class]]) {
        return NO;
    }
    MSFPosition *position = other;
    return _lng == position.lng && _lat == position.lat && _alt == position.alt;
}

- (NSUInteger)hash {
    return (NSUInteger)(_lng * 1e6) ^ (NSUInteger)(_lat * 1e6) ^ (NSUInteger)_alt;
}

- (NSString *)description {
    return _alt != 0 ? [NSString stringWithFormat:@"(%g, %g, %g)", _lng, _lat, _alt]
                     : [NSString stringWithFormat:@"(%g, %g)", _lng, _lat];
}

@end

@implementation MSFBounds

- (instancetype)initWithMin:(MSFPosition *)min max:(MSFPosition *)max {
    if ((self = [super init])) {
        _min = min;
        _max = max;
    }
    return self;
}

+ (instancetype)boundsWithMinLng:(double)minLng minLat:(double)minLat
                          maxLng:(double)maxLng maxLat:(double)maxLat {
    return [[self alloc] initWithMin:[MSFPosition positionWithLng:minLng lat:minLat]
                                 max:[MSFPosition positionWithLng:maxLng lat:maxLat]];
}

- (BOOL)isEqual:(id)other {
    if (![other isKindOfClass:[MSFBounds class]]) {
        return NO;
    }
    MSFBounds *bounds = other;
    return [_min isEqual:bounds.min] && [_max isEqual:bounds.max];
}

- (NSUInteger)hash {
    return _min.hash * 31 + _max.hash;
}

- (NSString *)description {
    return [NSString stringWithFormat:@"[%@, %@]", _min, _max];
}

@end

@implementation MSFScreenPoint

- (instancetype)initWithX:(float)x y:(float)y {
    if ((self = [super init])) {
        _x = x;
        _y = y;
    }
    return self;
}

- (BOOL)isEqual:(id)other {
    return [other isKindOfClass:[MSFScreenPoint class]] &&
           _x == ((MSFScreenPoint *)other).x && _y == ((MSFScreenPoint *)other).y;
}

- (NSUInteger)hash {
    return (NSUInteger)_x ^ ((NSUInteger)_y << 16);
}

- (NSString *)description {
    return [NSString stringWithFormat:@"(%g, %g)", _x, _y];
}

@end

@implementation MSFScreenRect

- (instancetype)initWithMin:(MSFScreenPoint *)min max:(MSFScreenPoint *)max {
    if ((self = [super init])) {
        _min = min;
        _max = max;
    }
    return self;
}

+ (instancetype)rectWithLeft:(float)left top:(float)top right:(float)right bottom:(float)bottom {
    return [[self alloc] initWithMin:[[MSFScreenPoint alloc] initWithX:left y:top]
                                 max:[[MSFScreenPoint alloc] initWithX:right y:bottom]];
}

- (BOOL)isEqual:(id)other {
    if (![other isKindOfClass:[MSFScreenRect class]]) {
        return NO;
    }
    MSFScreenRect *rect = other;
    return [_min isEqual:rect.min] && [_max isEqual:rect.max];
}

- (NSUInteger)hash {
    return _min.hash * 31 + _max.hash;
}

- (NSString *)description {
    return [NSString stringWithFormat:@"[%@, %@]", _min, _max];
}

@end
