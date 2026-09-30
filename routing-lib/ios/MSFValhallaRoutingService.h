//
//  MSFValhallaRoutingService.h
//  routing-lib iOS wrapper
//
//  Objective-C bridge to the C++ ValhallaRoutingService (offline) and
//  ValhallaOnlineRoutingService (online with optional user-supplied HTTP handler).
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// ---------------------------------------------------------------------------
// MSFRoutingRequest — input for calculateRoute
// ---------------------------------------------------------------------------

/**
 * A geographic coordinate (WGS-84).
 */
__attribute__ ((visibility("default"))) @interface MSFLatLon : NSObject
@property (nonatomic, assign) double lat;
@property (nonatomic, assign) double lon;
+ (instancetype)lat:(double)lat lon:(double)lon;
@end


/**
 * Input to a route calculation.
 *
 * Set @c profile to override the service-level costing model for this request.
 * If @c customJSON is supplied its top-level keys are merged into the request.
 */
__attribute__ ((visibility("default"))) @interface MSFRoutingRequest : NSObject
/** Ordered list of MSFLatLon waypoints. */
@property (nonatomic, copy) NSArray<MSFLatLon *> *points;
/** Optional costing model override (e.g. @"auto", @"pedestrian", @"bicycle"). */
@property (nonatomic, copy, nullable) NSString *profile;
/** Optional extra Valhalla request fields serialised as a JSON object string. */
@property (nonatomic, copy, nullable) NSString *customJSON;

- (instancetype)initWithPoints:(NSArray<MSFLatLon *> *)points;
@end


/**
 * Input to a route-matching (map-snapping) operation.
 *
 * Set @c profile to override the service-level costing model for this request.
 */
__attribute__ ((visibility("default"))) @interface MSFRouteMatchingRequest : NSObject
/** Ordered GPS trace points. */
@property (nonatomic, copy) NSArray<MSFLatLon *> *points;
/** GPS accuracy in metres (0 = use Valhalla default). */
@property (nonatomic, assign) float accuracy;
/** Optional costing model override (e.g. @"auto", @"pedestrian", @"bicycle"). */
@property (nonatomic, copy, nullable) NSString *profile;
/** Optional extra Valhalla request fields as a JSON object string. */
@property (nonatomic, copy, nullable) NSString *customJSON;

- (instancetype)initWithPoints:(NSArray<MSFLatLon *> *)points accuracy:(float)accuracy;
@end


// ---------------------------------------------------------------------------
// MSFValhallaRoutingService — offline service backed by MBTiles
// ---------------------------------------------------------------------------

/**
 * Offline Valhalla routing service.
 *
 * Add one or more MBTiles database paths via -addMBTilesPath:.
 * Databases are opened lazily on the first request and closed when idle.
 *
 * All routing results are returned as raw Valhalla JSON strings; parsing
 * is the responsibility of the caller.
 *
 * All routing methods are synchronous and must be called from a background thread.
 */
__attribute__ ((visibility("default"))) @interface MSFValhallaRoutingService : NSObject

/**
 * Initialise with an optional array of MBTiles file paths.
 */
- (instancetype)initWithMBTilesPaths:(nullable NSArray<NSString *> *)paths;

/** Add an MBTiles data source by file path. */
- (void)addMBTilesPath:(NSString *)path;

// -- Profile ----------------------------------------------------------------

/**
 * Default costing model injected as "costing" into every callRaw: body that
 * does not already contain that key. Default: @"pedestrian".
 */
@property (nonatomic, copy) NSString *profile;

// -- Configuration ----------------------------------------------------------

/**
 * Get a Valhalla configuration value by dot-delimited key path.
 * Returns nil if the key does not exist.
 */
- (nullable NSString *)configurationParameterForKey:(NSString *)key;

/**
 * Set a Valhalla configuration value by dot-delimited key path.
 * @param jsonValue  JSON-encoded value (e.g. @"0.5", @"\"residential\"", @"true").
 */
- (void)setConfigurationParameter:(NSString *)jsonValue forKey:(NSString *)key;

// -- Locales ----------------------------------------------------------------

/**
 * Register a Valhalla locale so that narrative instructions are generated
 * in the given language.
 */
- (void)addLocaleWithKey:(NSString *)key json:(NSString *)json;

/**
 * Parse a Valhalla response shape
 */
- (NSString *)parseShape:(NSString *)shape;

// -- Routing API ------------------------------------------------------------

/**
 * Calculate a route. Returns raw Valhalla JSON, or nil on error.
 *
 * Equivalent to calling @c callRaw:@"route" with the serialised request.
 * The service profile is injected as "costing" if the request does not set one.
 */
- (nullable NSString *)calculateRoute:(MSFRoutingRequest *)request
                                error:(NSError * _Nullable __autoreleasing *)error;

/**
 * Match a GPS trace to the road network. Returns raw Valhalla JSON, or nil on error.
 *
 * Equivalent to calling @c callRaw:@"trace_attributes" with the serialised request.
 */
- (nullable NSString *)matchRoute:(MSFRouteMatchingRequest *)request
                            error:(NSError * _Nullable __autoreleasing *)error;

/**
 * Call any Valhalla API endpoint directly.
 *
 * The service profile is injected as "costing" if @c jsonBody does not already
 * contain that key.
 *
 * @param endpoint   e.g. @"route", @"trace_attributes", @"isochrone", @"matrix" …
 * @param jsonBody   Pre-built Valhalla request JSON.
 * @return           Raw Valhalla JSON response, or nil on error.
 */
- (nullable NSString *)callRaw:(NSString *)endpoint
                      jsonBody:(NSString *)jsonBody
                         error:(NSError * _Nullable __autoreleasing *)error;

@end


// ---------------------------------------------------------------------------
// MSFValhallaOnlineRoutingService — online service via HTTP
// ---------------------------------------------------------------------------

/**
 * Synchronous HTTP POST handler block.
 *
 * @param url       Full request URL.
 * @param postBody  JSON request body.
 * @param error     Set this on failure; returning nil triggers the error path.
 * @return          HTTP response body string, or nil on failure.
 */
typedef NSString * _Nullable (^MSFHTTPPostHandler)(NSString *url,
                                                   NSString *postBody,
                                                   NSError * _Nullable __autoreleasing *error);

/**
 * Online Valhalla routing service.
 *
 * Two initializers are provided:
 *
 * - When the library is built with ROUTING_WITH_HTTP_CLIENT=ON, use
 *   @c initWithBaseURL: — HTTP is handled internally.
 *
 * - Use @c initWithBaseURL:handler: to supply your own HTTP stack (URLSession,
 *   Alamofire, etc.). Works regardless of the ROUTING_WITH_HTTP_CLIENT flag.
 *
 * All routing methods are synchronous; call them from a background thread.
 */
__attribute__ ((visibility("default"))) @interface MSFValhallaOnlineRoutingService : NSObject

/**
 * Initializer for use when the library is built with ROUTING_WITH_HTTP_CLIENT=ON.
 * @param baseURL  Base URL, e.g. @"https://valhalla1.openstreetmap.de".
 */
- (instancetype)initWithBaseURL:(NSString *)baseURL;

/**
 * Initializer that accepts an explicit HTTP POST handler.
 * @param baseURL  Base URL, e.g. @"https://valhalla1.openstreetmap.de".
 * @param handler  Synchronous HTTP POST handler block (may be nil when
 *                 built with ROUTING_WITH_HTTP_CLIENT=ON).
 */
- (instancetype)initWithBaseURL:(NSString *)baseURL
                        handler:(MSFHTTPPostHandler _Nullable)handler;

/** Base URL of the Valhalla service. */
@property (nonatomic, copy) NSString *baseURL;

/**
 * Default costing model injected as "costing" into every callRaw: body that
 * does not already contain that key. Default: @"pedestrian".
 */
@property (nonatomic, copy) NSString *profile;

/**
 * Calculate a route. Equivalent to @c callRaw:@"route" with the serialised request.
 */
- (nullable NSString *)calculateRoute:(MSFRoutingRequest *)request
                                error:(NSError * _Nullable __autoreleasing *)error;

/**
 * Match a GPS trace. Equivalent to @c callRaw:@"trace_attributes".
 */
- (nullable NSString *)matchRoute:(MSFRouteMatchingRequest *)request
                            error:(NSError * _Nullable __autoreleasing *)error;

/**
 * Call any Valhalla API endpoint directly.
 *
 * The service profile is injected as "costing" if @c jsonBody does not already
 * contain that key.
 */
- (nullable NSString *)callRaw:(NSString *)endpoint
                      jsonBody:(NSString *)jsonBody
                         error:(NSError * _Nullable __autoreleasing *)error;

@end

NS_ASSUME_NONNULL_END
