#import "DemoMap.h"
#import "DemoCameraPreview.h"
#import "DemoCelestial.h"
#import "DemoConfig.h"
#import "DemoOrientation.h"
#import "DemoSky.h"
#import "DemoStars.h"
#import "DemoStyles.h"
#import "DemoTests.h"
#import "DemoToast.h"
#import "DemoAstro.h"
#import <UIKit/UIKit.h>
// Fork additions, not listed in the umbrella header.
#import "MSFCelestialLayer.h"
#import "MSFCompositeVectorTileLayer.h"
#import "MSFContourTileDataSource.h"
#import "MSFCustomRasterTileLayer.h"
#import "MSFGeoJSONVectorTileDataSource.h"
#import "MSFManeuverArrowBuilder.h"
#import "MSFPostProcessEffect.h"

/** Bottom -> top draw order. Toggling a layer never reorders the others. */
static const DemoFeature LAYER_ORDER[] = {
    // The sky goes FIRST, so the map and the terrain draw over it and a ridge hides what is behind
    // it - which is what a body in the sky should do.
    DemoFeatureCelestial, DemoFeatureStars,
    DemoFeatureBase, DemoFeatureSatellite, DemoFeatureHillshade, DemoFeatureHypso,
    DemoFeatureContour, DemoFeatureContourTiles, DemoFeatureRouteTest, DemoFeatureManeuvers,
    DemoFeatureElements,
    // Last: the summit names go over everything the map draws.
    DemoFeaturePeaks,
};

/** Reports a tapped summit, the way the Android demo's VectorTileEventListener does. */
@interface DemoPeaksListener : MSFVectorTileEventListener
@end

@implementation DemoPeaksListener

- (BOOL)onVectorTileClicked:(MSFVectorTileClickInfo *)clickInfo {
    MSFVariant *properties = [[clickInfo getFeature] getProperties];
    [DemoToast show:[NSString stringWithFormat:@"%@ %@ m",
                     [[properties getObjectElement:@"name"] getString],
                     [[properties getObjectElement:@"ele"] description]]];
    return YES;
}

@end

@implementation DemoMap {
    NSMutableDictionary<NSNumber *, MSFLayer *> *_layers;

    // --- map objects the panel reaches through the properties ---
    MSFHillshadeRasterTileLayer *_hillshadeLayer;   // stand-alone hillshade layer, when built
    MSFVectorTileLayer *_baseLayer;                 // base map layer, whatever the mode
    MSFCompositeVectorTileLayer *_compositeLayer;   // same object as _baseLayer in COMPOSITE mode
    MSFMBVectorTileDecoder *_baseDecoder;
    MSFContourTileDataSource *_contourSource;       // shared by the layer and the composite slot
    MSFPostProcessEffect *_reliefEffect;

    // --- shared, lazily created tile sources ---
    MSFTileDataSource *_cachedDem;
    MSFTileDataSource *_cachedVector;
    MSFTileDataSource *_cachedRaster;
    MSFTileDataSource *_cachedContourTiles;
    MSFGeoJSONVectorTileDataSource *_cachedManeuvers;
    int _maneuverLayerIndex;
    MSFManeuverArrowBuilder *_maneuverBuilder;
    NSMutableDictionary<NSNumber *, MSFFeatureCollection *> *_maneuverArrows;
    BOOL _seededManeuvers;

    DemoOrientation *_orientation;
    DemoCameraPreview *_cameraPreview;
    DemoPeaksListener *_peaksListener;

    // What the peak-finder mode switched off, so leaving it puts the map back as it was.
    BOOL _savedLayerBase, _savedLayerHillshade, _savedLayerContour, _savedLayerContourTiles;
    BOOL _savedLayerSatellite, _savedLayerHypso;
    float _savedTilt;
    float _savedOcclusionTolerance;
    float _savedViewDistance;

    // What star sky / AR replaced, so leaving them puts the appearance back.
    MSFColor *_savedClearColor;
    MSFColor *_savedSkyColor;
    MSFBitmap *_savedBackgroundBitmap;
    BOOL _appearanceSaved;

    BOOL _paramOn;
}

- (instancetype)initWithMapView:(MSFMapView *)mapView {
    if ((self = [super init])) {
        _mapView = mapView;
        _layers = [NSMutableDictionary dictionary];
        _maneuverArrows = [NSMutableDictionary dictionary];
        _maneuverBuilder = [[MSFManeuverArrowBuilder alloc] init];
        _maneuverLayerIndex = -1;
        _savedViewDistance = 1;
        _paramOn = YES;
        _compositeStatus = @"";
        _celestial = [[DemoCelestial alloc] init];
        _stars = [[DemoStars alloc] init];
    }
    return self;
}

// =================================================================================================
// BUILD
// =================================================================================================

- (void)build {
    // Options first: layers created afterwards pick up the terrain/light state immediately.
    [self applyOptions];
    [self applyDebugConfig];
    [self applyTerrainOptions];
    [self applyLightOptions];
    [self applySkyOptions];

    [self rebuildLayers];
    [self applyCamera];

    float delay = [DemoConfig floatFor:@"peakfinderDelay"] / 1000.0f;
    if ([DemoConfig boolFor:@"reliefOutline"]) {
        // Attaching a post-process effect before the GL surface exists leaves the offscreen colour
        // buffer unwritten and the screen black, so wait for the first frames.
        [self after:delay run:^{ [self setReliefOutlineEnabled:YES]; }];
    }
    if ([DemoConfig boolFor:@"peakfinder"]) {
        [self after:delay run:^{
            [self setPeakFinderMode:YES];
            if ([DemoConfig boolFor:@"ar"]) {
                [self setArMode:YES]; // over the camera, once the view it composites is there
            }
        }];
    }
    if ([DemoConfig boolFor:@"daycycle"]) {
        [self applyDayCycle:[DemoConfig floatFor:@"dayCycleHour"]];
    }
    if ([DemoConfig boolFor:@"starSky"]) {
        // Already built without the map layers (isEnabled), so there is nothing to fade out.
        [self saveMapAppearance];
        [self enterStarSky];
    }
    if ([DemoConfig stringFor:@"popupFonts"].length) {
        [DemoTests run:@"popupFonts" demo:self]; // after applyCamera, so the popups land in view
    }
    [self startScriptedAnimation];
}

/** Elevation under a WGS84 position; blocks on tile loading, so call it off the main thread. */
- (double)getElevation:(MSFMapPos *)wgs84Pos {
    return _terrainOptions ? [_terrainOptions getElevation:wgs84Pos] : 0;
}

/** The counterpart of Android's mapView.requestRender(): iOS redraws when the renderer asks. */
- (void)requestRender {
    [[self.mapView getMapRenderer] requestRedraw];
}

- (void)after:(float)seconds run:(void (^)(void))block {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(seconds * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), block);
}

// =================================================================================================
// LAYERS
// =================================================================================================

- (BOOL)isEnabled:(DemoFeature)feature {
    // Star sky mode: every map layer is left OUT of the layer list rather than hidden, so no tile
    // is fetched, decoded or drawn - the mode costs what an empty map costs.
    if ([DemoConfig boolFor:@"starSky"] && feature != DemoFeatureCelestial && feature != DemoFeatureStars) {
        return NO;
    }
    return [DemoConfig boolFor:[self configKey:feature]];
}

- (void)setEnabled:(DemoFeature)feature enabled:(BOOL)enabled {
    [DemoConfig setValue:@(enabled) forKey:[self configKey:feature]];
    [self rebuildLayers];
}

- (NSString *)configKey:(DemoFeature)feature {
    switch (feature) {
        case DemoFeatureCelestial:    return @"celestial";
        case DemoFeatureStars:        return @"stars";
        case DemoFeatureBase:         return @"map";
        case DemoFeatureSatellite:    return @"satLayer";
        case DemoFeatureHillshade:    return @"hillshade";
        case DemoFeatureHypso:        return @"hypso";
        case DemoFeatureContour:      return @"contourLayer";
        case DemoFeatureContourTiles: return @"contourTiles";
        case DemoFeatureRouteTest:    return @"routeTest";
        case DemoFeatureManeuvers:    return @"maneuvers";
        case DemoFeatureElements:     return @"elements";
        case DemoFeaturePeaks:        return @"peaks";
    }
    return @"";
}

- (void)invalidate:(DemoFeature)feature {
    [_layers removeObjectForKey:@(feature)];
}

/**
 * Rebuilds the layer list from the config. Layer objects are cached, so switching one off and on
 * again does not refetch anything; use -invalidate when a layer's own settings changed in a way
 * that needs a new object (style source, base mode).
 */
- (void)rebuildLayers {
    MSFLayerVector *vector = [[MSFLayerVector alloc] init];
    for (size_t i = 0; i < sizeof(LAYER_ORDER) / sizeof(LAYER_ORDER[0]); i++) {
        DemoFeature feature = LAYER_ORDER[i];
        if (![self isEnabled:feature]) {
            continue;
        }
        MSFLayer *layer = _layers[@(feature)];
        if (!layer) {
            layer = [self createLayer:feature];
            if (!layer) {
                continue; // missing data file etc - already logged
            }
            _layers[@(feature)] = layer;
        }
        [vector add:layer];
    }
    [[self.mapView getLayers] setAll:vector];
    // The sky objects are built with their layer, which happens here, so place them now that they
    // exist.
    [self updateSky];
    [self requestRender];
}

- (void)updateSky {
    [self.celestial update];
    double n = [DemoAstro daysSinceJ2000WithYear:[DemoConfig intFor:@"sunYear"]
                                           month:[DemoConfig intFor:@"sunMonth"]
                                             day:[DemoConfig intFor:@"sunDay"]
                                            hour:[DemoConfig currentHourUtc]];
    [self.stars updateWithN:n lat:[DemoConfig doubleFor:@"lat"] lon:[DemoConfig doubleFor:@"lon"]];
    [self requestRender];
}

- (MSFLayer *)createLayer:(DemoFeature)feature {
    switch (feature) {
        case DemoFeatureCelestial:    return [self.celestial createLayer:self.mapView];
        case DemoFeatureStars:        return [self.stars createLayer:self.mapView];
        case DemoFeatureBase:         return [self createBaseLayer];
        case DemoFeatureSatellite:    return [[MSFRasterTileLayer alloc] initWithDataSource:[self rasterSource]];
        case DemoFeatureHillshade:    return [self createHillshadeLayer];
        case DemoFeatureHypso:        return [self createHypsoLayer];
        case DemoFeatureContour:      return [self createContourLayer];
        case DemoFeatureContourTiles: return [self createContourTilesLayer];
        case DemoFeatureRouteTest:    return [DemoTests createRouteTestLayer:self];
        case DemoFeatureManeuvers:    return [self createManeuversLayer];
        case DemoFeatureElements:     return [self createElementsLayer];
        case DemoFeaturePeaks:        return [self createPeaksLayer];
    }
    return nil;
}

// --- base map ------------------------------------------------------------------------------------

/**
 * The base map: either a plain VectorTileLayer or a CompositeVectorTileLayer that weaves hillshade
 * / satellite / contour sources into the style's own layer order.
 */
- (MSFLayer *)createBaseLayer {
    _baseDecoder = [DemoStyles createDecoder];
    unsigned long cacheBytes = (unsigned long)[DemoConfig intFor:@"baseCacheMB"] * 1024 * 1024;

    if (![[DemoConfig stringFor:@"base"] isEqualToString:@"composite"]) {
        _compositeLayer = nil;
        _baseLayer = [[MSFVectorTileLayer alloc] initWithDataSource:[self vectorSource] decoder:_baseDecoder];
        [_baseLayer setTileCacheCapacity:cacheBytes];
        return _baseLayer;
    }

    MSFCompositeVectorTileLayer *layer =
        [[MSFCompositeVectorTileLayer alloc] initWithDataSource:[self vectorSource] decoder:_baseDecoder];
    [layer setLabelRenderOrder:MSF_VECTOR_TILE_RENDER_ORDER_LAST];
    [layer setSinglePassRenderingEnabled:[DemoConfig boolFor:@"singlePass"]];
    [layer setTileCacheCapacity:cacheBytes];
    _compositeLayer = layer;
    _baseLayer = layer;
    [self syncCompositeSources];
    if ([[DemoConfig stringFor:@"style"] isEqualToString:@"project"]
            && [DemoConfig floatFor:@"paramInterval"] > 0) {
        [self startParamToggleLoop];
    }
    return layer;
}

- (void)rebuildBaseLayer {
    [self invalidate:DemoFeatureBase];
    [self rebuildLayers];
}

- (void)syncCompositeSources {
    if (!_compositeLayer) {
        return;
    }
    // hillshade: the elevation decoder is resolved from the source's 'dem_encoding' meta data.
    if ([DemoConfig boolFor:@"hs"]) {
        [_compositeLayer addExternalDataSource:@"hillshade"
                                    dataSource:[self demSource]
                                          type:MSF_COMPOSITE_SOURCE_TYPE_HILLSHADE
                              elevationDecoder:[self elevationDecoder]];
        if ([DemoConfig floatFor:@"hsBias"] != 0) {
            [_compositeLayer setExternalDataSourceZoomLevelBias:@"hillshade"
                                                           bias:[DemoConfig floatFor:@"hsBias"]];
        }
    } else {
        [_compositeLayer removeExternalDataSource:@"hillshade"];
    }
    // satellite: a raster source drawn at the '#satellite' slot with the style's opacity.
    if ([DemoConfig boolFor:@"sat"]) {
        [_compositeLayer addExternalDataSource:@"satellite"
                                    dataSource:[self rasterSource]
                                          type:MSF_COMPOSITE_SOURCE_TYPE_RASTER];
    } else {
        [_compositeLayer removeExternalDataSource:@"satellite"];
    }
    // contour: merged INTO the master source, styled by the '#contour' rules.
    if ([DemoConfig boolFor:@"contour"]) {
        [_compositeLayer addVectorDataSource:@"contour" dataSource:[self contourSource]];
    } else {
        [_compositeLayer removeExternalDataSource:@"contour"];
    }
    [self checkCompositeSlots];
    [self requestRender];
}

/**
 * WHY A COMPOSITE SLOT SILENTLY DOES NOTHING - the check to run first.
 *
 * A slot is the position of a style layer with the source's name. If the style does not DECLARE a
 * layer called 'hillshade' / 'satellite' / 'contour', the source has nowhere to be drawn and the
 * SDK only warns in the log. A COMPILED Mapnik XML style cannot declare these slots at all: the XML
 * symbolizer set has no hillshade/raster config symbolizer, only CartoCSS has.
 */
- (void)checkCompositeSlots {
    if (!_compositeLayer || !_baseDecoder) {
        _compositeStatus = @"";
        return;
    }
    MSFStringVector *styleLayers = [_baseDecoder getStyleLayerNames];
    NSMutableArray<NSString *> *declared = [NSMutableArray array];
    for (int i = 0; i < (int)[styleLayers size]; i++) {
        [declared addObject:[styleLayers get:i]];
    }

    MSFStringVector *registered = [_compositeLayer getExternalDataSourceNames];
    NSMutableString *status = [NSMutableString string];
    BOOL missing = NO;
    for (int i = 0; i < (int)[registered size]; i++) {
        NSString *name = [registered get:i];
        BOOL ok = [declared containsObject:name];
        missing |= !ok;
        [status appendFormat:@"%@%@ %@", status.length ? @", " : @"", name, ok ? @"OK" : @"MISSING in style"];
    }
    _compositeStatus = status.length ? [@"slots: " stringByAppendingString:status] : @"slots: none";
    NSLog(@"MassifDemo: %@ | style layers: %@", _compositeStatus, declared);
    if (missing) {
        NSLog(@"MassifDemo: a slot is missing: the style declares no layer with that name. Use a "
              @"CartoCSS project style, or the inline style to test.");
    }
}

// --- hillshade -----------------------------------------------------------------------------------

- (MSFLayer *)createHillshadeLayer {
    MSFHillshadeRasterTileLayer *layer =
        [[MSFHillshadeRasterTileLayer alloc] initWithDataSource:[self demSource]
                                             elevationDecoder:[self elevationDecoder]];
    [layer setPreloading:YES];
    [layer setTileSubstitutionPolicy:MSF_TILE_SUBSTITUTION_POLICY_VISIBLE];
    [layer setTileFilterMode:MSF_RASTER_TILE_FILTER_MODE_BILINEAR];
    _hillshadeLayer = layer;
    [self applyHillshadeConfig];
    return layer;
}

- (void)applyHillshadeConfig {
    MSFHillshadeRasterTileLayer *layer = _hillshadeLayer;
    if (!layer) {
        return;
    }
    [layer setHillshadeMethod:[self hillshadeMethod]];
    [layer setContrast:[DemoConfig floatFor:@"hsContrast"]];
    [layer setHeightScale:[DemoConfig floatFor:@"hsHeightScale"]];
    [layer setExaggeration:[DemoConfig floatFor:@"hsExaggeration"]];
    [layer setIlluminationMapRotationEnabled:NO];
    // The illumination direction is a vector; the panel/config express it in degrees.
    double radians = [DemoConfig floatFor:@"hsIllumination"] * M_PI / 180.0;
    [layer setIlluminationDirection:[[MSFMapVec alloc] initWithX:sin(radians)
                                                              y:cos(radians)
                                                              z:[[layer getIlluminationDirection] getZ]]];

    // GPU contour lines drawn in the hillshade pass: the normal map then encodes absolute
    // elevation, and the fragment shader draws anti-aliased lines from it. Unlike the geometry
    // contours, these are per-fragment at a fixed metre interval (no labels, no CartoCSS).
    [layer setContourEnabled:[DemoConfig boolFor:@"hsContours"]];
    [layer setContourInterval:[DemoConfig floatFor:@"hsContourInterval"]];

    // Slope colouring replaces the lighting shader entirely.
    BOOL slopes = [DemoConfig boolFor:@"slopes"];
    [layer setExagerateHeightScaleEnabled:!slopes];
    [layer setNormalMapLightingShader:slopes ? [DemoStyles slopesShader] : @""];
    [self requestRender];
}

- (enum MSFHillshadeMethod)hillshadeMethod {
    NSString *name = [DemoConfig stringFor:@"hsMethod"];
    if ([name isEqualToString:@"STANDARD"]) return MSF_STANDARD;
    if ([name isEqualToString:@"COMBINED"]) return MSF_COMBINED;
    if ([name isEqualToString:@"MULTIDIRECTIONAL"]) return MSF_MULTIDIRECTIONAL;
    if ([name isEqualToString:@"BASIC"]) return MSF_BASIC;
    return MSF_IGOR;
}

// --- other layers --------------------------------------------------------------------------------

/** CustomRasterTileLayer: any filter shader over any raster source (here: hypsometric tint). */
- (MSFLayer *)createHypsoLayer {
    MSFCustomRasterTileLayer *layer = [[MSFCustomRasterTileLayer alloc] initWithDataSource:[self demSource]];
    [layer setShaderSource:[DemoStyles hypsometricShader]];
    return layer;
}

/**
 * Stand-alone contour layer: vector tiles GENERATED on the fly from the shared DEM, styled by its
 * own CartoCSS. This is the geometry path (labels possible, styled per 'div'), as opposed to the
 * shader contours of the hillshade layer.
 */
- (MSFLayer *)createContourLayer {
    MSFMBVectorTileDecoder *decoder = [[MSFMBVectorTileDecoder alloc] initWithCartoCSSStyleSet:
        [[MSFCartoCSSStyleSet alloc] initWithCartoCSS:[DemoStyles contourStyle]]];
    return [[MSFVectorTileLayer alloc] initWithDataSource:[self contourSource] decoder:decoder];
}

/**
 * PRE-BAKED contour tiles over HTTP, styled with the same '#contour' rules. This is the A/B
 * reference: same attributes, but geometry baked at zoom 11..14 instead of traced from the DEM.
 */
- (MSFLayer *)createContourTilesLayer {
    MSFMBVectorTileDecoder *decoder = [[MSFMBVectorTileDecoder alloc] initWithCartoCSSStyleSet:
        [[MSFCartoCSSStyleSet alloc] initWithCartoCSS:[DemoStyles contourTilesStyle]]];
    return [[MSFVectorTileLayer alloc] initWithDataSource:[self contourTilesSource] decoder:decoder];
}

/**
 * Summit names as callout labels: their own vector tile layer on the base source, with a peaks-only
 * style. Clicking one reports it through the standard vector tile click path - a callout label is
 * picked where it is DRAWN, at the end of its leader line.
 */
- (MSFLayer *)createPeaksLayer {
    MSFMBVectorTileDecoder *decoder = [[MSFMBVectorTileDecoder alloc] initWithCartoCSSStyleSet:
        [[MSFCartoCSSStyleSet alloc] initWithCartoCSS:[DemoStyles peaksStyle]]];
    MSFVectorTileLayer *layer = [[MSFVectorTileLayer alloc] initWithDataSource:[self vectorSource]
                                                                     decoder:decoder];
    [layer setLabelRenderOrder:MSF_VECTOR_TILE_RENDER_ORDER_LAST];
    // Out of the post-process pass: the relief effect reads the terrain depth but paints over the
    // whole frame, so a ridge line drawn after the names crosses them. Opting the layer out draws
    // it AFTER the effect has resolved, which is also what keeps the glyphs crisp - they are not
    // resampled by the effect's half-resolution buffer.
    [layer setPostProcessed:NO];
    if (!_peaksListener) {
        _peaksListener = [[DemoPeaksListener alloc] init];
    }
    [layer setVectorTileEventListener:_peaksListener];
    return layer;
}

- (void)rebuildPeaksLayer {
    [self invalidate:DemoFeaturePeaks];
    [self rebuildLayers];
}

/**
 * Test elements draped on the terrain: markers on summits (billboard occlusion test - orbit around
 * a ridge and watch them disappear) and a line crossing the Isere valley (drape-lines and
 * element-vs-terrain depth test).
 */
- (MSFLayer *)createElementsLayer {
    MSFProjection *projection = [[self.mapView getOptions] getBaseProjection];
    MSFLocalVectorDataSource *source = [[MSFLocalVectorDataSource alloc] initWithProjection:projection];

    MSFMarkerStyleBuilder *markerStyle = [[MSFMarkerStyleBuilder alloc] init];
    [markerStyle setSize:24];
    [markerStyle setColor:[[MSFColor alloc] initWithR:255 g:0 b:0 a:255]];
    const double peaks[4][2] = {
        { 5.7869, 45.2876 }, // Chamechaude
        { 5.9207, 45.2989 }, // Dent de Crolles
        { 5.5433, 45.1861 }, // Le Moucherotte
        { 5.7247, 45.1988 }, // Bastille above Grenoble
    };
    for (int i = 0; i < 4; i++) {
        MSFMapPos *pos = [projection fromWgs84:[[MSFMapPos alloc] initWithX:peaks[i][0] y:peaks[i][1]]];
        [source add:[[MSFMarker alloc] initWithPos:pos style:[markerStyle buildStyle]]];
    }

    MSFLineStyleBuilder *lineStyle = [[MSFLineStyleBuilder alloc] init];
    [lineStyle setWidth:8];
    [lineStyle setColor:[[MSFColor alloc] initWithR:0 g:90 b:255 a:255]];
    MSFMapPosVector *linePoses = [[MSFMapPosVector alloc] init];
    [linePoses add:[projection fromWgs84:[[MSFMapPos alloc] initWithX:5.6800 y:45.1600]]];
    [linePoses add:[projection fromWgs84:[[MSFMapPos alloc] initWithX:5.7247 y:45.1927]]];
    [linePoses add:[projection fromWgs84:[[MSFMapPos alloc] initWithX:5.7869 y:45.2876]]];
    [source add:[[MSFLine alloc] initWithPoses:linePoses style:[lineStyle buildStyle]]];

    return [[MSFVectorLayer alloc] initWithDataSource:source];
}

// =================================================================================================
// MANEUVER ARROWS
// =================================================================================================

/**
 * The maneuver arrows. The source is served as a normal vector tile layer, so the arrow goes
 * through the same tesselator, drape and terrain occlusion as the base map's roads - and, as a
 * layer of its own, it draws over everything below it in LAYER_ORDER and under every Marker.
 */
- (MSFLayer *)createManeuversLayer {
    MSFMBVectorTileDecoder *decoder = [[MSFMBVectorTileDecoder alloc] initWithCartoCSSStyleSet:
        [[MSFCartoCSSStyleSet alloc] initWithCartoCSS:[DemoStyles maneuverStyle:[self maneuverHeadPath]]]];
    MSFGeoJSONVectorTileDataSource *source = [self maneuverSource];
    if (!_seededManeuvers) {
        // So the layer shows something without a routing package on the device. The routing test
        // overwrites these with the real turns of the route it computes.
        _seededManeuvers = YES;
        [DemoTests seedManeuverArrows:self];
    }
    return [[MSFVectorTileLayer alloc] initWithDataSource:source decoder:decoder];
}

- (MSFGeoJSONVectorTileDataSource *)maneuverSource {
    if (!_cachedManeuvers) {
        _cachedManeuvers = [[MSFGeoJSONVectorTileDataSource alloc] initWithMinZoom:0 maxZoom:24];
        _maneuverLayerIndex = [_cachedManeuvers createLayer:@"maneuver"];
        [_maneuverBuilder setLengthBefore:[DemoConfig floatFor:@"maneuverBefore"]];
        [_maneuverBuilder setLengthAfter:[DemoConfig floatFor:@"maneuverAfter"]];
    }
    return _cachedManeuvers;
}

/**
 * Shows one arrow under an id, or removes it when the arrow is nil or empty.
 *
 * The source serves a WHOLE layer at a time, so several arrows are kept here and the layer is
 * rebuilt from them - which belongs in the app now that an arrow is a single line feature.
 */
- (void)setManeuverArrow:(int)arrowId arrow:(MSFFeatureCollection *)arrow {
    [self maneuverSource];
    if (!arrow || [arrow getFeatureCount] == 0) {
        [_maneuverArrows removeObjectForKey:@(arrowId)];
    } else {
        _maneuverArrows[@(arrowId)] = arrow;
    }
    [self rebuildManeuverLayer];
}

- (void)clearManeuverArrows {
    if (!_maneuverArrows.count) {
        return;
    }
    [_maneuverArrows removeAllObjects];
    [self rebuildManeuverLayer];
}

- (void)rebuildManeuverLayer {
    if (_maneuverLayerIndex < 0) {
        return;
    }
    MSFFeatureVector *features = [[MSFFeatureVector alloc] init];
    for (MSFFeatureCollection *arrow in _maneuverArrows.allValues) {
        for (int i = 0; i < [arrow getFeatureCount]; i++) {
            [features add:[arrow getFeature:i]];
        }
    }
    [_cachedManeuvers setLayerFeatureCollection:_maneuverLayerIndex
                                     projection:nil
                              featureCollection:[[MSFFeatureCollection alloc] initWithFeatures:features]];
}

/**
 * The head outline the maneuver style should use: the explicit path if one was given, else the
 * first 'd' attribute of the selected SVG, else empty for the built-in triangle. Only the path
 * DATA is read - fills, strokes, transforms and the rest of SVG are ignored, because what the
 * renderer wants is a contour, not a picture.
 */
- (NSString *)maneuverHeadPath {
    NSString *explicit = [DemoConfig stringFor:@"maneuverPath"];
    if (explicit.length) {
        return explicit;
    }
    NSString *name = [DemoConfig stringFor:@"maneuverSvg"];
    if (!name.length) {
        return @"";
    }
    NSString *svg = [self readDocumentOrBundle:name];
    if (!svg) {
        NSLog(@"MassifDemo: maneuver svg not readable: %@", name);
        return @"";
    }
    NSRegularExpression *pattern = [NSRegularExpression regularExpressionWithPattern:@"\\bd\\s*=\\s*\"([^\"]+)\""
                                                                             options:0
                                                                               error:nil];
    NSTextCheckingResult *match = [pattern firstMatchInString:svg options:0
                                                        range:NSMakeRange(0, svg.length)];
    if (!match) {
        NSLog(@"MassifDemo: maneuver svg carries no path: %@", name);
        return @"";
    }
    // The parser reads a 'd' as written; the spaces only have to go because a launch argument would
    // be cut at the first one, and it costs nothing to normalise here as well.
    NSString *path = [svg substringWithRange:[match rangeAtIndex:1]];
    return [[NSRegularExpression regularExpressionWithPattern:@"\\s+" options:0 error:nil]
            stringByReplacingMatchesInString:path options:0 range:NSMakeRange(0, path.length)
                                withTemplate:@","];
}

/**
 * The heads to cycle through: the built-in triangle, every .svg copied into the app's Documents
 * directory, and the one bundled with the app. Documents wins, so another head can be tried with
 * a file share instead of a rebuild - the same arrangement as Android's data directory.
 */
- (NSArray<NSString *> *)maneuverHeadChoices {
    NSMutableArray<NSString *> *choices = [NSMutableArray arrayWithObject:@""];
    NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    NSArray<NSString *> *files = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:documents error:nil];
    for (NSString *file in [files sortedArrayUsingSelector:@selector(compare:)]) {
        if ([file.pathExtension.lowercaseString isEqualToString:@"svg"]) {
            [choices addObject:file];
        }
    }
    [choices addObject:@"maneuver-head-cloud.svg"];
    return choices;
}

- (NSString *)cycleManeuverHead {
    NSArray<NSString *> *choices = [self maneuverHeadChoices];
    NSUInteger index = [choices indexOfObject:[DemoConfig stringFor:@"maneuverSvg"]];
    index = (index == NSNotFound) ? 0 : (index + 1) % choices.count;
    [DemoConfig setValue:choices[index] forKey:@"maneuverSvg"];
    [DemoConfig setValue:@"" forKey:@"maneuverPath"]; // an explicit path would win over the file
    [self invalidate:DemoFeatureManeuvers];
    [self rebuildLayers];
    NSString *name = choices[index];
    return name.length ? name : @"built-in triangle";
}

/** Documents wins over the bundle, so a dataset can be swapped without a rebuild. */
- (NSString *)readDocumentOrBundle:(NSString *)name {
    NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    NSString *path = [documents stringByAppendingPathComponent:name];
    NSString *contents = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil];
    if (contents) {
        return contents;
    }
    path = [[NSBundle mainBundle] pathForResource:name.stringByDeletingPathExtension
                                            ofType:name.pathExtension];
    return path ? [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil] : nil;
}

// =================================================================================================
// SHARED TILE SOURCES (created once, used by several layers)
// =================================================================================================

- (NSString *)cachePathFor:(NSString *)name {
    NSString *dir = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject;
    return [dir stringByAppendingPathComponent:name];
}

- (MSFTileDataSource *)httpSourceWithURL:(NSString *)url minZoom:(int)minZoom maxZoom:(int)maxZoom
                                  cache:(NSString *)cacheName {
    MSFHTTPTileDataSource *source = [[MSFHTTPTileDataSource alloc] initWithMinZoom:minZoom
                                                                          maxZoom:maxZoom
                                                                          baseURL:url];
    MSFStringMap *headers = [[MSFStringMap alloc] init];
    [headers set:@"User-Agent" x:[DemoConfig stringFor:@"userAgent"]];
    [source setHTTPHeaders:headers];
    return [[MSFPersistentCacheTileDataSource alloc] initWithDataSource:source
                                                          databasePath:[self cachePathFor:cacheName]];
}

- (MSFTileDataSource *)vectorSource {
    if (!_cachedVector) {
        _cachedVector = [self httpSourceWithURL:[DemoConfig stringFor:@"vectorUrl"]
                                        minZoom:0
                                        maxZoom:[DemoConfig intFor:@"vectorMaxZoom"]
                                          cache:@"akylas_vect.db"];
    }
    return _cachedVector;
}

/**
 * The elevation source. EVERYTHING elevation-related uses this one instance (3D terrain, hillshade,
 * contours, hypsometric tint), so each DEM tile is fetched and decoded once.
 */
- (MSFTileDataSource *)demSource {
    if (!_cachedDem) {
        _cachedDem = [self httpSourceWithURL:[DemoConfig stringFor:@"demUrl"]
                                     minZoom:1
                                     maxZoom:[DemoConfig intFor:@"demMaxZoom"]
                                       cache:@"mapterhorn.db"];
    }
    return _cachedDem;
}

- (MSFTileDataSource *)rasterSource {
    if (!_cachedRaster) {
        _cachedRaster = [self httpSourceWithURL:[DemoConfig stringFor:@"rasterUrl"]
                                        minZoom:0 maxZoom:19 cache:@"openstreetmap.db"];
    }
    return _cachedRaster;
}

- (MSFTileDataSource *)contourTilesSource {
    if (!_cachedContourTiles) {
        _cachedContourTiles = [self httpSourceWithURL:[DemoConfig stringFor:@"contourTilesUrl"]
                                              minZoom:11
                                              maxZoom:[DemoConfig intFor:@"contourTilesMaxZoom"]
                                                cache:@"akylas_contours.db"];
    }
    return _cachedContourTiles;
}

/** Contours generated from the shared DEM; the same instance feeds layer and composite slot. */
- (MSFContourTileDataSource *)contourSource {
    if (!_contourSource) {
        _contourSource = [[MSFContourTileDataSource alloc] initWithDataSource:[self demSource]
                                                            elevationDecoder:[self elevationDecoder]];
        [self applyContourConfig];
    }
    return _contourSource;
}

- (void)applyContourConfig {
    if (!_contourSource) {
        return;
    }
    [_contourSource setBaseInterval:[DemoConfig floatFor:@"contourInterval"]];
    // Perf knobs: the DEM is subsampled to at most 'resolution' samples/side before tracing, and
    // geometry is simplified by 'simplify' tile pixels.
    [_contourSource setResolution:[DemoConfig intFor:@"contourResolution"]];
    [_contourSource setSimplifyTolerance:[DemoConfig floatFor:@"contourSimplify"]];
    // Fetch neighbour DEM tiles so lines meet across tile boundaries (removes seams).
    [_contourSource setSeamlessEdgesEnabled:[DemoConfig boolFor:@"contourSeamless"]];
    // NOTE: in CartoCSS 'zoom' is the TILE zoom. Contour tiles are generated at the DEM zoom, so
    // per-zoom style rules only fire if the DEM source max zoom is high enough.
    [_contourSource setMinVisibleZoom:[DemoConfig intFor:@"contourMinZoom"]];
    // Labels only: the lines come from the hillshade shader, so the tile carries a handful of stubs
    // to lay the text along instead of the traced geometry.
    [_contourSource setLabelStubsEnabled:[DemoConfig boolFor:@"contourStubs"]];
    [_contourSource setLabelInterval:[DemoConfig floatFor:@"contourStubInterval"]];
    // Stubs off the terrain's own elevation: no DEM tile of the contour source's own to fetch and
    // decode. Same DEM source on both sides, so the labels state the heights the terrain draws.
    [_contourSource setTerrainOptions:[DemoConfig boolFor:@"stubsFromTerrain"] ? _terrainOptions : nil];
    [self requestRender];
}

- (MSFElevationDecoder *)elevationDecoder {
    if ([[DemoConfig stringFor:@"demEncoding"] isEqualToString:@"mapbox"]) {
        return [[MSFMapBoxElevationDataDecoder alloc] init];
    }
    return [[MSFTerrariumElevationDataDecoder alloc] init];
}

// =================================================================================================
// OPTIONS: TERRAIN / LIGHT / SKY
// =================================================================================================

- (void)applyOptions {
    MSFOptions *options = [self.mapView getOptions];
    [options setTileThreadPoolSize:[DemoConfig intFor:@"tilePool"]];
    [options setTileLODFactor:[DemoConfig floatFor:@"lodFactor"]];
}

- (void)applyDebugConfig {
    [[self.mapView getOptions] setDebugTileBorders:[DemoConfig boolFor:@"tileBorders"]];
    [self requestRender];
}

- (void)applyTerrainOptions {
    if (!_terrainOptions) {
        _terrainOptions = [[MSFTerrainOptions alloc] initWithDataSource:[self demSource]
                                                      elevationDecoder:[self elevationDecoder]];
        [[self.mapView getOptions] setTerrainOptions:_terrainOptions];
    }
    [_terrainOptions setEnabled:[DemoConfig boolFor:@"terrain"]];
    [_terrainOptions setExaggeration:[DemoConfig floatFor:@"exaggeration"]];
    [_terrainOptions setMeshResolution:[DemoConfig intFor:@"meshResolution"]];
    [_terrainOptions setCameraClearance:[DemoConfig floatFor:@"clearance"]];
    [_terrainOptions setDrapeFillsEnabled:[DemoConfig boolFor:@"drape"]];
    [_terrainOptions setDrapeLinesEnabled:[DemoConfig boolFor:@"drapeLines"]];
    [_terrainOptions setDrapeResolution:[DemoConfig intFor:@"drapeResolution"]];
    [_terrainOptions setTileEdgeStitchingEnabled:[DemoConfig boolFor:@"stitch"]];
    [_terrainOptions setSeamlessTileEdgesEnabled:[DemoConfig boolFor:@"seamlessEdges"]];
    [_terrainOptions setElevationPrefetchEnabled:[DemoConfig boolFor:@"prefetch"]];
    [_terrainOptions setBillboardOcclusionEnabled:[DemoConfig boolFor:@"occlusion"]];
    [_terrainOptions setBillboardOcclusionTolerance:[DemoConfig floatFor:@"occlusionTolerance"]];
    [_terrainOptions setBackgroundBitmapEnabled:[DemoConfig boolFor:@"backgroundBitmap"]];
    if ([DemoConfig intFor:@"maxTileZoomOffset"] != 0) {
        [_terrainOptions setMaxTileZoomOffset:[DemoConfig intFor:@"maxTileZoomOffset"]];
    }
    // Fog and view distance go together: the distance ENDS the ground, the fog is what makes it
    // fade out instead of being cut off. The fog itself lives on FogOptions now.
    [self applyFogOptions];
    [_terrainOptions setViewDistanceFactor:[DemoConfig floatFor:@"viewDistance"]];
    [_terrainOptions setViewDistance:[DemoConfig floatFor:@"viewDistanceMeters"]];
    [_terrainOptions setMaxTileZoomCoarsening:[DemoConfig intFor:@"coarsening"]];
    [self applyReliefSurface];
    [self applyApiSet];
    [self requestRender];
}

- (void)applyLightOptions {
    if (!_lightOptions) {
        _lightOptions = [[MSFLightOptions alloc] init];
        [[self.mapView getOptions] setLightOptions:_lightOptions];
    }
    float sunHour = [DemoConfig floatFor:@"sunHour"];
    if (sunHour >= 0) {
        [_lightOptions setSunPositionFromTime:[DemoConfig intFor:@"sunYear"]
                                        month:[DemoConfig intFor:@"sunMonth"]
                                          day:[DemoConfig intFor:@"sunDay"]
                                         hour:(int)sunHour
                                       minute:(int)((sunHour - (int)sunHour) * 60)
                                     latitude:[DemoConfig doubleFor:@"lat"]
                                    longitude:[DemoConfig doubleFor:@"lon"]];
    } else {
        [_lightOptions setSunAzimuth:[DemoConfig floatFor:@"sunAzimuth"]];
        [_lightOptions setSunAltitude:[DemoConfig floatFor:@"sunAltitude"]];
    }
    [_lightOptions setSunIntensity:[DemoConfig floatFor:@"sunIntensity"]];
    [_lightOptions setAmbientIntensity:[DemoConfig floatFor:@"ambient"]];
    [_lightOptions setTerrainLightingEnabled:[DemoConfig boolFor:@"terrainLight"]];
    [_lightOptions setShadowStrength:[DemoConfig floatFor:@"shadow"]];
    [_lightOptions setShadowSoftness:[DemoConfig floatFor:@"shadowSoftness"]];
    [_lightOptions setShadowMapSize:[DemoConfig intFor:@"shadowMapSize"]];
    [_lightOptions setShadowCascades:[DemoConfig intFor:@"shadowCascades"]];
    [_lightOptions setShadowBias:[DemoConfig floatFor:@"shadowBias"]];
    [_lightOptions setShadowDistance:[DemoConfig floatFor:@"shadowDistance"]];
    [_lightOptions setShadowCasterMargin:[DemoConfig intFor:@"shadowMargin"]];
    [self updateSky];
}

/** Fog is its own options object and is independent of the terrain - it fogs a plain 2D map too. */
/**
 * Drives a property through the facade API (#146) instead of the typed setters, so the generated
 * property table and its dotted path walking can be exercised on a device:
 *
 *   xcrun simctl launch <device> com.massifmaps.MassifDemo -apiSet fogOptions.rangeStart=2.5
 */
- (void)applyApiSet {
    NSString *assignment = [DemoConfig stringFor:@"apiSet"];
    if (assignment.length == 0) {
        return;
    }
    NSRange equals = [assignment rangeOfString:@"="];
    if (equals.location == NSNotFound || equals.location == 0) {
        NSLog(@"apiSet wants path=value, got: %@", assignment);
        return;
    }
    NSString *path = [assignment substringToIndex:equals.location];
    double value = [[assignment substringFromIndex:equals.location + 1] doubleValue];

    int handle = [MSFMassifApi findObject:@"options" objectId:@"demo"];
    if (handle == 0) {
        handle = [MSFMassifInterop adopt:@"options" objectId:@"demo" options:[self.mapView getOptions]];
    }
    double before = [MSFMassifApi getFloat:handle path:path defaultValue:NAN];
    int result = [MSFMassifApi setFloat:handle path:path value:value];
    NSLog(@"apiSet %@ %f -> %f (handle=%d, result=%d)", path, before,
          [MSFMassifApi getFloat:handle path:path defaultValue:NAN], handle, result);
}

- (void)applyFogOptions {
    if (!_fogOptions) {
        _fogOptions = [[MSFFogOptions alloc] init];
        [[self.mapView getOptions] setFogOptions:_fogOptions];
    }
    [_fogOptions setEnabled:[DemoConfig boolFor:@"fog"]];
    [_fogOptions setColor:[DemoMap colorFromHex:@"#b8c6d8"]];
    [_fogOptions setRangeStart:[DemoConfig floatFor:@"fogRangeStart"]];
    [_fogOptions setRangeEnd:[DemoConfig floatFor:@"fogRangeEnd"]];
    // How far up the sky the haze reaches. The SAME term scales the ground, so the two meet at
    // the skyline with no seam at any tilt.
    [_fogOptions setHorizonBlend:[DemoConfig floatFor:@"fogBlend"]];
    // Peaks poking out of a valley haze (mapbox vertical-range).
    [_fogOptions setVerticalRangeStart:[DemoConfig floatFor:@"fogVertStart"]];
    [_fogOptions setVerticalRangeEnd:[DemoConfig floatFor:@"fogVertEnd"]];
    [self requestRender];
}

/** The sky is always attached so the panel can toggle it live; disabled = no sky at all. */
- (void)applySkyOptions {
    if (!_skyOptions) {
        _skyOptions = [[MSFSkyOptions alloc] init];
        [[self.mapView getOptions] setSkyOptions:_skyOptions];
    }
    [_skyOptions setEnabled:[DemoConfig boolFor:@"sky"]];
    NSString* skyType = [DemoConfig stringFor:@"skyType"];
    [_skyOptions setType:[skyType isEqualToString:@"gradient"] ? MSFSkyTypeSkyTypeGradient : MSFSkyTypeSkyTypeAtmosphere];
    NSString* skyQuality = [DemoConfig stringFor:@"skyQuality"];
    [_skyOptions setQuality:[skyQuality isEqualToString:@"low"] ? MSFSkyQualitySkyQualityLow
                     : [skyQuality isEqualToString:@"high"] ? MSFSkyQualitySkyQualityHigh : MSFSkyQualitySkyQualityMedium];
    [_skyOptions setAtmosphereSunIntensity:[DemoConfig floatFor:@"skyAtmoSun"]];
    [_skyOptions setAtmosphereLuminance:[DemoConfig floatFor:@"skyAtmoLum"]];
    // In the relief view the sky is part of the palette: a light one over the paper, a night one
    // over the ink.
    if ([DemoConfig boolFor:@"reliefSurface"] || [DemoConfig boolFor:@"peakfinder"]) {
        // The generated day-cycle shader owns the sky's colours, so it has to go for the palette's
        // own sky to be visible at all (SkyOptions falls back to the built-in sky).
        if (![DemoConfig boolFor:@"daycycle"]) {
            [_skyOptions setShaderSource:@""];
        }
        [_skyOptions setSkyColor:[DemoMap colorFromHex:[DemoMap reliefSky]]];
        [[self.mapView getOptions] setSkyColor:[DemoMap colorFromHex:[DemoMap reliefSky]]];
    }
    [self requestRender];
}

/**
 * Day cycle: one hour value drives sun position, sky colours, shadow strength and a generated sky
 * shader. Turning it off restores the plain sky.
 */
- (void)applyDayCycle:(float)hourUtc {
    [DemoConfig setValue:@(hourUtc) forKey:@"dayCycleHour"];
    [self updateSky]; // the hour is also what places the sun, the moon and the stars
    if (![DemoConfig boolFor:@"daycycle"]) {
        [_skyOptions setShaderSource:@""];
        return;
    }
    [DemoConfig setValue:@YES forKey:@"terrainLight"];
    [DemoConfig setValue:@YES forKey:@"sky"];
    [_lightOptions setTerrainLightingEnabled:YES];
    [_skyOptions setEnabled:YES];
    // The sun is computed for the CURRENT map centre, not the start position.
    MSFProjection *projection = [[self.mapView getOptions] getBaseProjection];
    MSFMapPos *centre = [projection toWgs84:[self.mapView getFocusPos]];
    [DemoSky applyHour:hourUtc light:_lightOptions sky:_skyOptions lat:[centre getY] lon:[centre getX]];
    [self requestRender];
}

// =================================================================================================
// CAMERA
// =================================================================================================

/** Note: setFocusPos expects BASE PROJECTION coordinates, so WGS84 must be converted first. */
- (void)applyCamera {
    MSFProjection *projection = [[self.mapView getOptions] getBaseProjection];
    MSFMapPos *focus = [projection fromWgs84:[[MSFMapPos alloc] initWithX:[DemoConfig doubleFor:@"lon"]
                                                                      y:[DemoConfig doubleFor:@"lat"]]];
    [self.mapView setFocusPos:focus durationSeconds:0];
    [self.mapView setZoom:[DemoConfig floatFor:@"zoom"] durationSeconds:0];
    [self applyLookRange];
    [self.mapView setTilt:[DemoConfig floatFor:@"tilt"] durationSeconds:0];
    [self.mapView setRotation:[DemoConfig floatFor:@"rotation"] durationSeconds:0];
    // What the camera ACTUALLY ended up at, which is not always what was asked for: the terrain
    // clamps the zoom to keep its clearance, and a launch argument whose value starts with '-' is
    // dropped by NSUserDefaults (see the README - that is why a negative tilt needs the panel).
    NSLog(@"MassifDemo: camera lon %.6f lat %.6f zoom %.2f tilt %.1f rotation %.1f",
          [DemoConfig doubleFor:@"lon"], [DemoConfig doubleFor:@"lat"],
          [self.mapView getZoom], [self.mapView getTilt], [self.mapView getRotation]);
}

/**
 * Free roam and how far above the horizon the view may look.
 *
 * A NEGATIVE tilt is the look up: the camera stays where the tilt geometry put it and only the view
 * pitches, so nothing about zoom or the visible tiles changes.
 *
 * A map stops at 30: below that the camera grazes the terrain and at 0 it looks along the ground
 * from under it. Only the two modes that are ABOUT looking up - free roam and the star sky - get
 * the full range.
 */
- (void)applyLookRange {
    MSFOptions *options = [self.mapView getOptions];
    NSString *freeRoam = [DemoConfig stringFor:@"freeRoam"];
    [options setFreeRoamMode:[self freeRoamMode:freeRoam]];
    [options setPanningSpeedMode:[self panningSpeedMode:[DemoConfig stringFor:@"panSpeed"]]];
    [options setFreeRoamLookSensitivity:[DemoConfig floatFor:@"lookSensitivity"]];
    [options setFreeRoamMoveSpeed:[DemoConfig floatFor:@"moveSpeed"]];
    BOOL lookUp = ![freeRoam isEqualToString:@"off"] || [DemoConfig boolFor:@"starSky"];
    float minTilt = lookUp ? -fmaxf(30, [DemoConfig floatFor:@"lookUp"]) : 30;
    [options setTiltRange:[[MSFMapRange alloc] initWithMin:minTilt max:90]];
}

- (enum MSFPanningSpeedMode)panningSpeedMode:(NSString *)name {
    if ([name isEqualToString:@"map"]) return MSF_PANNING_SPEED_MODE_MAP;
    if ([name isEqualToString:@"constant"]) return MSF_PANNING_SPEED_MODE_CONSTANT;
    return MSF_PANNING_SPEED_MODE_ANCHORED;
}

- (enum MSFFreeRoamMode)freeRoamMode:(NSString *)name {
    if ([name isEqualToString:@"look"]) return MSF_FREE_ROAM_MODE_LOOK;
    if ([name isEqualToString:@"fps"]) return MSF_FREE_ROAM_MODE_FIRST_PERSON;
    return MSF_FREE_ROAM_MODE_OFF;
}

/**
 * '-anim zoom|pan|rotate|zoomseq' drives a scripted camera move, so animation artifacts (which
 * still frames never show) can be captured with a screen recording without touch input.
 */
- (void)startScriptedAnimation {
    NSString *anim = [DemoConfig stringFor:@"anim"];
    if (!anim.length) {
        return;
    }
    float duration = [DemoConfig floatFor:@"animDuration"];
    [self after:[DemoConfig floatFor:@"animDelay"] / 1000.0f run:^{
        MSFProjection *projection = [[self.mapView getOptions] getBaseProjection];
        if ([anim isEqualToString:@"zoom"]) {
            [self.mapView setZoom:[DemoConfig floatFor:@"zoom"] + [DemoConfig floatFor:@"animZoomDelta"]
                  durationSeconds:duration];
        } else if ([anim isEqualToString:@"pan"]) {
            MSFMapPos *target = [projection fromWgs84:
                [[MSFMapPos alloc] initWithX:[DemoConfig doubleFor:@"lon"] + [DemoConfig floatFor:@"animLonDelta"]
                                          y:[DemoConfig doubleFor:@"lat"] + [DemoConfig floatFor:@"animLatDelta"]]];
            [self.mapView setFocusPos:target durationSeconds:duration];
        } else if ([anim isEqualToString:@"rotate"]) {
            [self.mapView setRotation:[DemoConfig floatFor:@"animRotation"] durationSeconds:duration];
        } else if ([anim isEqualToString:@"zoomseq"]) {
            // zoom out, back in, out again - each step after the map settled, which is the repro
            // shape for "stale content stays on screen" bugs.
            float zoomOut = [DemoConfig floatFor:@"animZoomOut"];
            float zoomIn = [DemoConfig floatFor:@"zoom"];
            float settle = [DemoConfig floatFor:@"animSettle"] / 1000.0f;
            NSLog(@"MassifDemo: zoomseq step1 out %g", zoomOut);
            [self.mapView setZoom:zoomOut durationSeconds:0];
            [self after:settle run:^{
                NSLog(@"MassifDemo: zoomseq step2 in %g", zoomIn);
                [self.mapView setZoom:zoomIn durationSeconds:0];
            }];
            [self after:2 * settle run:^{
                NSLog(@"MassifDemo: zoomseq step3 out %g", zoomOut);
                [self.mapView setZoom:zoomOut durationSeconds:0];
            }];
        }
    }];
}

// =================================================================================================
// THE RELIEF / PEAK-FINDER LOOK
// =================================================================================================

+ (NSString *)reliefInk {
    return [DemoConfig stringFor:[DemoConfig boolFor:@"reliefDark"] ? @"reliefInkDark" : @"reliefInkLight"];
}

+ (NSString *)reliefPaper {
    return [DemoConfig stringFor:[DemoConfig boolFor:@"reliefDark"] ? @"reliefPaperDark" : @"reliefPaperLight"];
}

+ (NSString *)reliefShade {
    return [DemoConfig stringFor:[DemoConfig boolFor:@"reliefDark"] ? @"reliefShadeDark" : @"reliefShadeLight"];
}

+ (NSString *)reliefSky {
    return [DemoConfig stringFor:[DemoConfig boolFor:@"reliefDark"] ? @"reliefSkyDark" : @"reliefSkyLight"];
}

+ (MSFColor *)colorFromHex:(NSString *)hex {
    unsigned int value = 0;
    [[NSScanner scannerWithString:[hex stringByReplacingOccurrencesOfString:@"#" withString:@""]]
        scanHexInt:&value];
    // "#rrggbb" carries no alpha; the SDK wants ARGB, so assume opaque.
    NSUInteger digits = [hex stringByReplacingOccurrencesOfString:@"#" withString:@""].length;
    if (digits <= 6) {
        value |= 0xff000000;
    }
    return [[MSFColor alloc] initWithColor:(int)value];
}

/**
 * The shaded terrain surface of the relief look. The surface shader replaces the terrain background
 * fill, so it is what shows wherever no tile layer paints - switch the base map off to see it.
 */
- (void)applyReliefSurface {
    if (!_terrainOptions) {
        return;
    }
    [_terrainOptions setSurfaceShaderSource:[DemoConfig boolFor:@"reliefSurface"]
                                            ? [DemoStyles reliefSurfaceShader] : @""];
    [_terrainOptions setSurfaceColorParameter:@"uPaperColor" color:[DemoMap colorFromHex:[DemoMap reliefPaper]]];
    [_terrainOptions setSurfaceColorParameter:@"uShadeColor" color:[DemoMap colorFromHex:[DemoMap reliefShade]]];
    [_terrainOptions setSurfaceParameter:@"uShadeStrength" value:[DemoConfig floatFor:@"reliefShade"]];
    [_terrainOptions setSurfaceParameter:@"uAmbient" value:[DemoConfig floatFor:@"reliefAmbient"]];
    [_terrainOptions setSurfaceParameter:@"uHaze" value:[DemoConfig floatFor:@"reliefHaze"]];
    [_terrainOptions setSurfaceParameter:@"uHazeDistance" value:[DemoConfig floatFor:@"reliefHazeDistance"]];
    [self requestRender];
}

/** PeakFinder-style relief outline post-process effect. */
- (void)setReliefOutlineEnabled:(BOOL)enabled {
    [DemoConfig setValue:@(enabled) forKey:@"reliefOutline"];
    MSFMapRenderer *renderer = [self.mapView getMapRenderer];
    if (enabled) {
        // The SDK provides the mechanism - an offscreen frame, the packed terrain depth and named
        // parameters - and the app provides the look, as a fragment shader string. There is no
        // relief effect in the SDK.
        _reliefEffect = [[MSFPostProcessEffect alloc] initWithName:@"relief_outline"
                                                   fragmentShader:[DemoStyles reliefOutlineShader]];
        [_reliefEffect setTerrainDepthRequired:YES];
        [self applyReliefOutlineParameters];
        [renderer setPostProcessEffect:_reliefEffect];
    } else {
        _reliefEffect = nil;
        [renderer setPostProcessEffect:nil];
    }
    [self requestRender];
}

- (void)applyReliefOutlineParameters {
    if (!_reliefEffect) {
        return;
    }
    [_reliefEffect setFloatParameter:@"uIntensity" value:1.0f];
    [_reliefEffect setFloatParameter:@"uOutlineWidth" value:[DemoConfig floatFor:@"reliefWidth"]];
    [_reliefEffect setFloatParameter:@"uHorizonBoost" value:[DemoConfig floatFor:@"reliefHorizonBoost"]];
    [_reliefEffect setFloatParameter:@"uDepthThreshold" value:[DemoConfig floatFor:@"reliefThreshold"]];
    [_reliefEffect setFloatParameter:@"uCreaseStrength" value:[DemoConfig floatFor:@"reliefCrease"]];
    [_reliefEffect setFloatParameter:@"uHaze" value:[DemoConfig floatFor:@"reliefHaze"]];
    // The depth texture is half resolution, and the two below are what keep the horizon the boldest
    // line: the silhouette test is relaxed by the grazing angle, and terrain-vs-terrain lines fade
    // with distance while the sky's do not.
    [_reliefEffect setFloatParameter:@"uDepthTexelSize" value:2.0f];
    [_reliefEffect setFloatParameter:@"uGrazingFloor" value:0.15f];
    [_reliefEffect setFloatParameter:@"uDistanceFade" value:0.45f];
    [_reliefEffect setColorParameter:@"uInkColor" color:[DemoMap colorFromHex:[DemoMap reliefInk]]];
    [_reliefEffect setColorParameter:@"uPaperColor" color:[DemoMap colorFromHex:[DemoMap reliefPaper]]];
    [self requestRender];
}

/**
 * The relief palette, in one switch: the shaded surface, the ink lines, the names, the plate behind
 * them and the sky all come from the same pair of colours. The names need their layer rebuilt
 * because a CartoCSS style bakes its colours in.
 */
- (void)setReliefDark:(BOOL)dark {
    [DemoConfig setValue:@(dark) forKey:@"reliefDark"];
    [self applyReliefSurface];
    [self applyReliefOutlineParameters];
    [self applySkyOptions];
    [self rebuildPeaksLayer];
}

/**
 * The peak-finder view, in one switch. The pieces are independent SDK features, but each one on its
 * own looks like nothing happens: the shaded surface only shows where NO tile layer paints, and
 * summit names need a view that has summits in it - which a top-down city camera has not. So the
 * mode turns the map layers off, the relief and the names on, and tilts the camera to a panorama
 * (in this SDK tilt 90 is straight down).
 */
- (void)setPeakFinderMode:(BOOL)enabled {
    [DemoConfig setValue:@(enabled) forKey:@"peakfinder"];
    if (enabled) {
        _savedLayerBase = [DemoConfig boolFor:@"map"];
        _savedLayerHillshade = [DemoConfig boolFor:@"hillshade"];
        _savedLayerContour = [DemoConfig boolFor:@"contourLayer"];
        _savedLayerContourTiles = [DemoConfig boolFor:@"contourTiles"];
        _savedLayerSatellite = [DemoConfig boolFor:@"satLayer"];
        _savedLayerHypso = [DemoConfig boolFor:@"hypso"];
        _savedTilt = [self.mapView getTilt];
        [DemoConfig setValue:@NO forKey:@"map"];
        [DemoConfig setValue:@NO forKey:@"hillshade"];
        [DemoConfig setValue:@NO forKey:@"contourLayer"];
        [DemoConfig setValue:@NO forKey:@"contourTiles"];
        [DemoConfig setValue:@NO forKey:@"satLayer"];
        [DemoConfig setValue:@NO forKey:@"hypso"];
        [DemoConfig setValue:@YES forKey:@"peaks"];
        [DemoConfig setValue:@YES forKey:@"reliefSurface"];
        _savedOcclusionTolerance = [DemoConfig floatFor:@"occlusionTolerance"];
        // A summit sitting ON a ridge, or a metre behind it, is exactly what the view is for, so
        // the label occlusion is deliberately generous here.
        [DemoConfig setValue:@([DemoConfig floatFor:@"peakFinderOcclusion"]) forKey:@"occlusionTolerance"];
        // And a panorama wants the far ranges: tangram's rule stops the ground a few kilometres
        // out, which is most of what the view is about.
        _savedViewDistance = [DemoConfig floatFor:@"viewDistance"];
        [DemoConfig setValue:@([DemoConfig floatFor:@"peakFinderViewDistance"]) forKey:@"viewDistance"];
        [self applyTerrainOptions];
        [self rebuildLayers];
        [self applyReliefSurface];
        [self applySkyOptions];
        [self setReliefOutlineEnabled:YES];
        [self.mapView setTilt:[DemoConfig floatFor:@"peakFinderTilt"] durationSeconds:0.6f];
    } else {
        [DemoConfig setValue:@(_savedLayerBase) forKey:@"map"];
        [DemoConfig setValue:@(_savedLayerHillshade) forKey:@"hillshade"];
        [DemoConfig setValue:@(_savedLayerContour) forKey:@"contourLayer"];
        [DemoConfig setValue:@(_savedLayerContourTiles) forKey:@"contourTiles"];
        [DemoConfig setValue:@(_savedLayerSatellite) forKey:@"satLayer"];
        [DemoConfig setValue:@(_savedLayerHypso) forKey:@"hypso"];
        [DemoConfig setValue:@NO forKey:@"peaks"];
        [DemoConfig setValue:@NO forKey:@"reliefSurface"];
        [DemoConfig setValue:@(_savedOcclusionTolerance) forKey:@"occlusionTolerance"];
        [DemoConfig setValue:@(_savedViewDistance) forKey:@"viewDistance"];
        [DemoConfig setValue:@0.0f forKey:@"peakFinderElevation"];
        [self applyTerrainOptions];
        [self applyViewpointElevation];
        [self rebuildLayers];
        [self applyReliefSurface];
        [self setReliefOutlineEnabled:NO];
        if (_savedTilt > 0) {
            [self.mapView setTilt:_savedTilt durationSeconds:0.6f];
        }
    }
    [self requestRender];
}

/**
 * Enters the peak-finder view from wherever the map is, as ONE move: the camera flies to the
 * current focus at the panorama's zoom and tilt (flyTo, which pulls back over a long move and comes
 * down at the target), while the viewpoint climbs and the terrain, the relief and the names come up
 * on the same clock. Everything here is ordinary SDK API driven from the app - the SDK has no
 * peak-finder mode.
 */
- (void)flyToPeakFinder {
    MSFMapPos *focus = [self.mapView getFocusPos];
    // 3D terrain first, then the mode: the terrain, the relief surface and the names all have the
    // whole flight to load and fade in, instead of appearing when it lands.
    if (![DemoConfig boolFor:@"terrain"]) {
        [DemoConfig setValue:@YES forKey:@"terrain"];
        [self applyTerrainOptions];
    }
    [self setPeakFinderMode:YES];
    // The tilt belongs to the flight, so undo what the mode did to it and let the one animation
    // carry it - together with the climb to the viewpoint's elevation.
    [self.mapView setTilt:_savedTilt > 0 ? _savedTilt : [self.mapView getTilt] durationSeconds:0];
    MSFMapPos *target = [[MSFMapPos alloc] initWithX:[focus getX] y:[focus getY]
                                                 z:[DemoConfig floatFor:@"peakFinderFlyElevation"]];
    [self.mapView flyTo:target
                   zoom:[DemoConfig floatFor:@"peakFinderFlyZoom"]
               rotation:[self.mapView getRotation]
                   tilt:[DemoConfig floatFor:@"peakFinderTilt"]
            climbHeight:[DemoConfig floatFor:@"peakFinderFlyClimb"]
        durationSeconds:[DemoConfig floatFor:@"peakFinderFlyDuration"]];
    [DemoConfig setValue:@([DemoConfig floatFor:@"peakFinderFlyElevation"]) forKey:@"peakFinderElevation"];
}

/**
 * Lifts the viewpoint by 'peakFinderElevation' metres. The focus position carries a height and the
 * camera rides on it, so raising the focus raises the eye - which is what a peak-finder view wants:
 * see over the ridge in front of you.
 * The z of a MapPos is in INTERNAL units, and one metre is worth more of them the further from the
 * equator (mercator), hence the latitude term.
 */
- (void)applyViewpointElevation {
    MSFProjection *projection = [[self.mapView getOptions] getBaseProjection];
    MSFMapPos *wgs = [projection toWgs84:[self.mapView getFocusPos]];
    double groundElevation = 0;
    if (_terrainOptions) {
        double sample = [_terrainOptions getElevation:wgs];
        if (sample > -100000) {
            groundElevation = sample;
        }
    }
    // The projection converts metres to internal units itself (toInternal scales z with x/y), so
    // this stays in METRES - with the mercator stretch the terrain heights also carry, or the
    // viewpoint would sit lower than the mountains it is measured against.
    double meters = (groundElevation + [DemoConfig floatFor:@"peakFinderElevation"])
        / cos([wgs getY] * M_PI / 180.0);
    [self.mapView setFocusPos:[projection fromWgs84:[[MSFMapPos alloc] initWithX:[wgs getX]
                                                                              y:[wgs getY]
                                                                              z:meters]]
              durationSeconds:0.3f];
}

/**
 * AR: the relief view over the camera preview. Everything here is an SDK feature the app puts
 * together - a transparent clear colour (the frame becomes a hole), a translucent GL surface (so
 * the hole shows the preview behind it), the sky off, the dark palette, and the device's
 * orientation driving the camera. The terrain, the relief surface and the names stay exactly as
 * they are in the peak-finder view; only what is BEHIND them changes.
 */
- (void)setArMode:(BOOL)enabled {
    [DemoConfig setValue:@(enabled) forKey:@"ar"];
    MSFOptions *options = [self.mapView getOptions];
    if (enabled) {
        if (![DemoConfig boolFor:@"peakfinder"]) {
            [self setPeakFinderMode:YES];
        }
        [self saveMapAppearance];
        [options setClearColor:[[MSFColor alloc] initWithR:0 g:0 b:0 a:0]];
        [options setSkyColor:[[MSFColor alloc] initWithR:0 g:0 b:0 a:0]];
        [options setBackgroundBitmap:nil];
        [_skyOptions setEnabled:NO];
        [self setReliefDark:YES];
        [self setSurfaceTranslucent:YES];
        [self setCameraPreviewEnabled:[DemoConfig boolFor:@"arCamera"]];
        [self setOrientationFollowing:[DemoConfig boolFor:@"arOrientation"]];
    } else {
        [self setOrientationFollowing:NO];
        [self setCameraPreviewEnabled:NO];
        [self setSurfaceTranslucent:NO];
        [self restoreMapAppearance];
        [_skyOptions setEnabled:[DemoConfig boolFor:@"sky"]];
        [self setReliefDark:NO];
    }
    [self requestRender];
}

// =================================================================================================
// STAR SKY: the map removed, the background cleared to nothing, only the sky left
// =================================================================================================

- (void)saveMapAppearance {
    if (_appearanceSaved) {
        return;
    }
    MSFOptions *options = [self.mapView getOptions];
    _savedClearColor = [options getClearColor];
    _savedSkyColor = [options getSkyColor];
    _savedBackgroundBitmap = [options getBackgroundBitmap];
    _appearanceSaved = YES;
}

- (void)restoreMapAppearance {
    if (!_appearanceSaved) {
        return;
    }
    MSFOptions *options = [self.mapView getOptions];
    [options setClearColor:_savedClearColor];
    [options setSkyColor:_savedSkyColor];
    [options setBackgroundBitmap:_savedBackgroundBitmap];
    _appearanceSaved = NO;
}

/**
 * Switches the whole map off and leaves the sky.
 *
 * "Not drawn" here means NOT BUILT: the map layers leave the layer list (see -isEnabled), the
 * terrain is disabled and the background is cleared to a fully transparent black, so the frame
 * costs an empty map plus the sky objects. The transparency is the point - with a translucent
 * surface, whatever is behind the view (a camera preview) shows through it.
 *
 * The map fades out before it is dropped and fades back in after it returns, so the switch is not
 * a pop.
 */
- (void)applyStarSky:(BOOL)enabled {
    float duration = fmaxf(0, [DemoConfig floatFor:@"starSkyFade"]) / 1000.0f;
    if (enabled) {
        [self saveMapAppearance];
        [self fadeMapLayersFrom:1 to:0 duration:duration completion:^{ [self enterStarSky]; }];
    } else {
        [self leaveStarSky];
        [self fadeMapLayersFrom:0 to:1 duration:duration completion:nil];
    }
}

- (void)enterStarSky {
    [DemoConfig setValue:@YES forKey:@"starSky"];
    MSFOptions *options = [self.mapView getOptions];
    // Transparent, not black: the frame is then a hole that whatever is behind the surface shows
    // through, and it looks black on its own anyway.
    [options setClearColor:[[MSFColor alloc] initWithR:0 g:0 b:0 a:0]];
    [options setSkyColor:[[MSFColor alloc] initWithR:0 g:0 b:0 a:0]];
    [options setBackgroundBitmap:nil];
    [_terrainOptions setEnabled:NO];
    [_skyOptions setEnabled:NO];
    [self setSurfaceTranslucent:[DemoConfig boolFor:@"starSkyTranslucent"]];
    [self setCameraPreviewEnabled:[DemoConfig boolFor:@"starSkyCamera"]];
    [self rebuildLayers];
    [self setMapLayerOpacity:1]; // the layers are out of the list now: leave them ready to come back
    [self applyLookRange];
    [self setOrientationFollowing:[DemoConfig boolFor:@"starSkyOrientation"]];
    [self requestRender];
}

- (void)leaveStarSky {
    [self setOrientationFollowing:NO];
    [self setCameraPreviewEnabled:NO];
    [DemoConfig setValue:@NO forKey:@"starSky"];
    [self restoreMapAppearance];
    [_terrainOptions setEnabled:[DemoConfig boolFor:@"terrain"]];
    [_skyOptions setEnabled:[DemoConfig boolFor:@"sky"]];
    if ([DemoConfig boolFor:@"starSkyTranslucent"]) {
        [self setSurfaceTranslucent:NO];
    }
    [self setMapLayerOpacity:0];
    [self rebuildLayers];
    [self applyLookRange]; // back to a map: the tilt stops at 30 again
    [self requestRender];
}

/** Opacity of every layer that is NOT the sky. */
- (void)setMapLayerOpacity:(float)opacity {
    [_layers enumerateKeysAndObjectsUsingBlock:^(NSNumber *key, MSFLayer *layer, BOOL *stop) {
        DemoFeature feature = (DemoFeature)key.integerValue;
        if (feature != DemoFeatureCelestial && feature != DemoFeatureStars) {
            [layer setOpacity:opacity];
        }
    }];
    [self requestRender];
}

- (void)fadeMapLayersFrom:(float)from to:(float)to duration:(float)duration
               completion:(void (^)(void))completion {
    if (duration <= 0) {
        [self setMapLayerOpacity:to];
        if (completion) {
            completion();
        }
        return;
    }
    // A plain display-link-free ramp: the opacity is pushed on the main thread, and the SDK
    // redraws on its own once a layer property changes.
    static const int steps = 20;
    for (int i = 1; i <= steps; i++) {
        float t = (float)i / steps;
        [self after:duration * t run:^{
            [self setMapLayerOpacity:from + (to - from) * t];
            if (i == steps && completion) {
                completion();
            }
        }];
    }
}

/** Turning the device turns the view, raising it looks up - the negative tilt in action. */
- (void)setOrientationFollowing:(BOOL)enabled {
    [DemoConfig setValue:@(enabled) forKey:@"starSkyOrientation"];
    if (enabled) {
        if (!_orientation) {
            _orientation = [[DemoOrientation alloc] initWithMapView:self.mapView];
        }
        [_orientation start];
    } else {
        [_orientation stop];
    }
}

/**
 * The live camera behind the map: what the transparent clear colour is FOR. Only meaningful with a
 * translucent surface.
 */
- (void)setCameraPreviewEnabled:(BOOL)enabled {
    [DemoConfig setValue:@(enabled) forKey:@"starSkyCamera"];
    if (enabled) {
        UIView *root = self.mapView.superview;
        if (!root) {
            NSLog(@"MassifDemo: the map has no superview: no place to put the preview");
            return;
        }
        if (!_cameraPreview) {
            _cameraPreview = [[DemoCameraPreview alloc] initWithRootView:root];
        }
        [_cameraPreview start];
    } else {
        [_cameraPreview stop];
    }
}

/**
 * A translucent GL surface, which is what makes a transparent clear colour visible: the map is then
 * composited over whatever is behind it. Without this the transparency is real but the surface is
 * still opaque, so it just looks black.
 */
- (void)setSurfaceTranslucent:(BOOL)translucent {
    if (!NSThread.isMainThread) {
        dispatch_async(dispatch_get_main_queue(), ^{ [self setSurfaceTranslucent:translucent]; });
        return;
    }
    [self.mapView setTranslucent:translucent];
}

/**
 * Demonstrates a 'param::' user setting driving the style at runtime: flips the parameter every
 * 'paramInterval' ms, which fades the hillshade slot in and out.
 */
- (void)startParamToggleLoop {
    float interval = [DemoConfig floatFor:@"paramInterval"] / 1000.0f;
    [self after:interval run:^{
        if (!self->_baseDecoder || ![[DemoConfig stringFor:@"style"] isEqualToString:@"project"]) {
            return; // the style was switched away: stop the loop
        }
        self->_paramOn = !self->_paramOn;
        [self->_baseDecoder setStyleParameter:[DemoStyles boolParameter]
                                        value:self->_paramOn ? @"true" : @"false"];
        [self startParamToggleLoop];
    }];
}

@end
