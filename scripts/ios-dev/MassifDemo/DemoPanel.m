#import "DemoPanel.h"
#import "DemoConfig.h"
#import "DemoMap.h"
#import "DemoTests.h"
#import <objc/runtime.h>

// The entry a control belongs to, carried on the control itself so the target/action handlers stay
// one-liners instead of resolving an index path back to a row.
static const void *kDemoEntryKey = &kDemoEntryKey;
static void setEntry(UIControl *control, id entry) {
    objc_setAssociatedObject(control, kDemoEntryKey, entry, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}
static id getEntry(UIControl *control) {
    return objc_getAssociatedObject(control, kDemoEntryKey);
}

typedef NS_ENUM(NSInteger, DemoEntryKind) {
    DemoEntryToggle,
    DemoEntrySlider,
    DemoEntryChoice,
    DemoEntryAction,
};

/**
 * One row. 'apply' is what the row does after writing DemoConfig - the same closure the Java panel
 * passes as a BoolSetting/FloatSetting, so each row still says which apply it needs and nothing
 * central has to know.
 */
@interface DemoEntry : NSObject
@property (nonatomic, copy) NSString *key;
@property (nonatomic, copy) NSString *label;
@property (nonatomic) DemoEntryKind kind;
@property (nonatomic, copy) void (^apply)(void);
@property (nonatomic) float minimum;
@property (nonatomic) float maximum;
@property (nonatomic, copy) NSArray<NSString *> *choices;
@end

@implementation DemoEntry

+ (instancetype)toggle:(NSString *)key label:(NSString *)label apply:(void (^)(void))apply {
    DemoEntry *e = [DemoEntry new];
    e.key = key; e.label = label; e.kind = DemoEntryToggle; e.apply = apply;
    return e;
}

+ (instancetype)slider:(NSString *)key label:(NSString *)label
                   min:(float)minimum max:(float)maximum apply:(void (^)(void))apply {
    DemoEntry *e = [DemoEntry new];
    e.key = key; e.label = label; e.kind = DemoEntrySlider; e.apply = apply;
    e.minimum = minimum; e.maximum = maximum;
    return e;
}

+ (instancetype)choice:(NSString *)key label:(NSString *)label
               choices:(NSArray<NSString *> *)choices apply:(void (^)(void))apply {
    DemoEntry *e = [DemoEntry new];
    e.key = key; e.label = label; e.kind = DemoEntryChoice; e.apply = apply;
    e.choices = choices;
    return e;
}

+ (instancetype)button:(NSString *)label apply:(void (^)(void))apply {
    DemoEntry *e = [DemoEntry new];
    e.label = label; e.kind = DemoEntryAction; e.apply = apply;
    return e;
}

/** Matched against the label AND the config key, so searching "zoom" finds both. */
- (BOOL)matches:(NSString *)query {
    if (!query.length) {
        return YES;
    }
    return [self.label rangeOfString:query options:NSCaseInsensitiveSearch].location != NSNotFound
        || (self.key && [self.key rangeOfString:query options:NSCaseInsensitiveSearch].location != NSNotFound);
}

@end

@interface DemoSection : NSObject
@property (nonatomic, copy) NSString *title;
@property (nonatomic, copy) NSArray<DemoEntry *> *entries;
@property (nonatomic) BOOL collapsed;
@end

@implementation DemoSection
@end

@interface DemoPanel () <UITableViewDataSource, UITableViewDelegate, UISearchBarDelegate>
@property (nonatomic, strong) DemoMap *demo;
@property (nonatomic, strong) NSArray<DemoSection *> *allSections;
/** Sections after the search filter, with only their surviving entries. */
@property (nonatomic, strong) NSArray<DemoSection *> *visibleSections;
@property (nonatomic, copy) NSString *query;
@property (nonatomic, strong) UITableView *tableView;
@property (nonatomic, strong) UISearchBar *searchBar;
@end

@implementation DemoPanel

- (instancetype)initWithDemo:(DemoMap *)demo {
    if ((self = [super init])) {
        _demo = demo;
        _query = @"";
        [self buildEntries];
        // Everything but the layer list starts closed: the panel is long, and this is the section
        // you reach for first.
        for (NSUInteger i = 1; i < _allSections.count; i++) {
            _allSections[i].collapsed = YES;
        }
        [self applyFilter];
    }
    return self;
}

- (DemoSection *)section:(NSString *)title entries:(NSArray<DemoEntry *> *)entries {
    DemoSection *section = [DemoSection new];
    section.title = title;
    section.entries = entries;
    return section;
}

- (void)buildEntries {
    DemoMap *demo = self.demo;
    // The applies, named once so the rows below read like the Java panel's one-liners.
    void (^layers)(void) = ^{ [demo rebuildLayers]; };
    void (^base)(void) = ^{ [demo rebuildBaseLayer]; };
    void (^composite)(void) = ^{ [demo syncCompositeSources]; };
    void (^terrain)(void) = ^{ [demo applyTerrainOptions]; };
    void (^light)(void) = ^{ [demo applyLightOptions]; };
    void (^sky)(void) = ^{ [demo applySkyOptions]; };
    void (^fog)(void) = ^{ [demo applyFogOptions]; };
    void (^hillshade)(void) = ^{ [demo applyHillshadeConfig]; };
    void (^contours)(void) = ^{ [demo applyContourConfig]; };
    void (^camera)(void) = ^{ [demo applyCamera]; };
    void (^look)(void) = ^{ [demo applyLookRange]; };
    void (^skyObjects)(void) = ^{ [demo updateSky]; };
    void (^relief)(void) = ^{ [demo applyReliefSurface]; };
    void (^outline)(void) = ^{ [demo applyReliefOutlineParameters]; };
    void (^peaks)(void) = ^{ [demo rebuildPeaksLayer]; };
    void (^debug)(void) = ^{ [demo applyDebugConfig]; };

    self.allSections = @[
        [self section:@"LAYERS" entries:@[
            [DemoEntry toggle:@"celestial" label:@"celestial" apply:layers],
            [DemoEntry toggle:@"stars" label:@"stars" apply:layers],
            [DemoEntry toggle:@"map" label:@"base map" apply:layers],
            [DemoEntry toggle:@"satLayer" label:@"satellite" apply:layers],
            [DemoEntry toggle:@"hillshade" label:@"hillshade" apply:layers],
            [DemoEntry toggle:@"hypso" label:@"hypso (hypsometric tint)" apply:layers],
            [DemoEntry toggle:@"contourLayer" label:@"contour (traced from the DEM)" apply:layers],
            [DemoEntry toggle:@"contourTiles" label:@"contour tiles (pre-baked)" apply:layers],
            [DemoEntry toggle:@"routeTest" label:@"route test" apply:layers],
            [DemoEntry toggle:@"maneuvers" label:@"maneuvers" apply:layers],
            [DemoEntry toggle:@"elements" label:@"elements" apply:layers],
            [DemoEntry toggle:@"peaks" label:@"peaks" apply:layers],
        ]],
        [self section:@"BASE MAP" entries:@[
            [DemoEntry choice:@"base" label:@"mode" choices:@[@"composite", @"plain"] apply:base],
            [DemoEntry choice:@"style" label:@"style" choices:@[@"inline", @"zip", @"project"] apply:base],
            [DemoEntry toggle:@"labels" label:@"labels (inline style)" apply:base],
            [DemoEntry toggle:@"bld3d" label:@"3D buildings (inline style)" apply:base],
            [DemoEntry toggle:@"minimal" label:@"minimal style (slots only)" apply:base],
            [DemoEntry slider:@"landcoverOpacity" label:@"landcover opacity" min:0 max:1 apply:base],
            [DemoEntry slider:@"satZoom" label:@"satellite min zoom" min:0 max:19 apply:base],
        ]],
        [self section:@"COMPOSITE SLOTS" entries:@[
            [DemoEntry toggle:@"hs" label:@"hillshade slot" apply:composite],
            [DemoEntry toggle:@"sat" label:@"satellite slot" apply:composite],
            [DemoEntry toggle:@"contour" label:@"contour slot" apply:composite],
            [DemoEntry slider:@"hsBias" label:@"hillshade zoom bias" min:-2 max:2 apply:composite],
        ]],
        [self section:@"SHIELDS (style 'poi')" entries:@[
            [DemoEntry toggle:@"poiTextOptional" label:@"icon alone when nothing fits" apply:base],
            [DemoEntry toggle:@"poiFontIcon" label:@"font icon" apply:base],
            [DemoEntry toggle:@"poiBitmapIcon" label:@"bitmap shield" apply:base],
            [DemoEntry toggle:@"poiTextBg" label:@"plate behind the name" apply:base],
            [DemoEntry toggle:@"poiIconBg" label:@"plate behind the icon" apply:base],
            [DemoEntry slider:@"poiBgRadius" label:@"plate radius" min:0 max:20 apply:base],
            [DemoEntry slider:@"poiBgPadding" label:@"plate padding" min:0 max:12 apply:base],
            [DemoEntry slider:@"poiTextDx" label:@"gap icon/name" min:0 max:12 apply:base],
            [DemoEntry slider:@"poiWrapWidth" label:@"wrap width" min:30 max:200 apply:base],
        ]],
        [self section:@"TERRAIN" entries:@[
            [DemoEntry toggle:@"terrain" label:@"3D terrain" apply:terrain],
            [DemoEntry slider:@"exaggeration" label:@"exaggeration" min:0 max:3 apply:terrain],
            [DemoEntry slider:@"meshResolution" label:@"mesh resolution" min:8 max:256 apply:terrain],
            [DemoEntry slider:@"clearance" label:@"camera clearance (m)" min:0 max:400 apply:terrain],
            [DemoEntry toggle:@"drape" label:@"drape fills" apply:terrain],
            [DemoEntry toggle:@"drapeLines" label:@"drape lines" apply:terrain],
            [DemoEntry toggle:@"stitch" label:@"tile edge stitching" apply:terrain],
            [DemoEntry toggle:@"seamlessEdges" label:@"seamless tile edges" apply:terrain],
            [DemoEntry toggle:@"prefetch" label:@"elevation prefetch" apply:terrain],
            [DemoEntry toggle:@"occlusion" label:@"billboard occlusion" apply:terrain],
            [DemoEntry toggle:@"backgroundBitmap" label:@"background bitmap" apply:terrain],
        ]],
        [self section:@"HILLSHADE LAYER" entries:@[
            [DemoEntry choice:@"hsMethod" label:@"method"
                      choices:@[@"IGOR", @"COMBINED", @"BASIC", @"MULTIDIRECTIONAL", @"STANDARD"]
                        apply:hillshade],
            [DemoEntry slider:@"hsContrast" label:@"contrast" min:0 max:1 apply:hillshade],
            [DemoEntry slider:@"hsHeightScale" label:@"height scale" min:0 max:0.5 apply:hillshade],
            [DemoEntry slider:@"hsExaggeration" label:@"exaggeration" min:0 max:3 apply:hillshade],
            [DemoEntry slider:@"hsIllumination" label:@"illumination (deg)" min:0 max:360 apply:hillshade],
            [DemoEntry toggle:@"hsContours" label:@"shader contour lines" apply:hillshade],
            [DemoEntry slider:@"hsContourInterval" label:@"shader interval (m)" min:10 max:500 apply:hillshade],
            [DemoEntry toggle:@"slopes" label:@"slope-angle bands" apply:hillshade],
        ]],
        [self section:@"CONTOUR SOURCE" entries:@[
            [DemoEntry slider:@"contourInterval" label:@"base interval (m)" min:5 max:200 apply:contours],
            [DemoEntry slider:@"contourResolution" label:@"tracing grid" min:32 max:512 apply:contours],
            [DemoEntry slider:@"contourSimplify" label:@"simplify (tile px)" min:0 max:8 apply:contours],
            [DemoEntry slider:@"contourMinZoom" label:@"min zoom" min:1 max:16 apply:contours],
            [DemoEntry toggle:@"contourSeamless" label:@"seamless edges" apply:contours],
            [DemoEntry toggle:@"contourStubs" label:@"label stubs only" apply:contours],
            [DemoEntry slider:@"contourStubInterval" label:@"stub interval (m)" min:0 max:500 apply:contours],
            [DemoEntry toggle:@"stubsFromTerrain" label:@"stubs off the terrain" apply:contours],
        ]],
        [self section:@"ROUTE TEST" entries:@[
            [DemoEntry slider:@"routeWidth" label:@"width" min:1 max:30 apply:layers],
            [DemoEntry slider:@"routeCaseWidth" label:@"casing width" min:0 max:40 apply:layers],
            [DemoEntry choice:@"routeJoin" label:@"join" choices:@[@"round", @"miter", @"bevel"] apply:layers],
            [DemoEntry choice:@"routeCap" label:@"cap" choices:@[@"round", @"square", @"butt"] apply:layers],
            [DemoEntry slider:@"routeMiterLimit" label:@"miter limit" min:1 max:10 apply:layers],
            [DemoEntry slider:@"routeOpacity" label:@"opacity" min:0 max:1 apply:layers],
            [DemoEntry choice:@"routeOpacityMode" label:@"opacity mode" choices:@[@"geom", @"layer"] apply:layers],
            [DemoEntry slider:@"maneuverWidth" label:@"maneuver width" min:1 max:20 apply:layers],
            [DemoEntry slider:@"maneuverCaseWidth" label:@"maneuver casing" min:0 max:30 apply:layers],
            [DemoEntry slider:@"maneuverArrowWidth" label:@"arrow width (x line)" min:1 max:5 apply:layers],
            [DemoEntry slider:@"maneuverArrowLength" label:@"arrow length (x line)" min:1 max:5 apply:layers],
        ]],
        [self section:@"SUN" entries:@[
            [DemoEntry toggle:@"terrainLight" label:@"terrain lighting" apply:light],
            [DemoEntry toggle:@"daycycle" label:@"day cycle" apply:^{
                [demo applyDayCycle:[DemoConfig floatFor:@"dayCycleHour"]];
            }],
            [DemoEntry slider:@"dayCycleHour" label:@"hour (UTC)" min:0 max:24 apply:^{
                [demo applyDayCycle:[DemoConfig floatFor:@"dayCycleHour"]];
            }],
            [DemoEntry slider:@"sunAzimuth" label:@"azimuth" min:0 max:360 apply:light],
            [DemoEntry slider:@"sunAltitude" label:@"altitude" min:-10 max:90 apply:light],
            [DemoEntry slider:@"sunIntensity" label:@"intensity" min:0 max:2 apply:light],
            [DemoEntry slider:@"ambient" label:@"ambient" min:0 max:2 apply:light],
        ]],
        [self section:@"SHADOWS" entries:@[
            [DemoEntry slider:@"shadow" label:@"strength" min:0 max:1 apply:light],
            [DemoEntry slider:@"shadowSoftness" label:@"softness" min:0 max:4 apply:light],
            [DemoEntry slider:@"shadowCascades" label:@"cascades" min:1 max:4 apply:light],
            [DemoEntry slider:@"shadowBias" label:@"bias" min:0 max:4 apply:light],
            [DemoEntry slider:@"shadowDistance" label:@"distance (m, 0=view)" min:0 max:20000 apply:light],
        ]],
        [self section:@"SKY OBJECTS" entries:@[
            [DemoEntry choice:@"freeRoam" label:@"free roam" choices:@[@"off", @"look", @"fps"] apply:look],
            [DemoEntry slider:@"lookSensitivity" label:@"look sensitivity (deg/inch)" min:20 max:200 apply:look],
            [DemoEntry slider:@"moveSpeed" label:@"move speed" min:0.05 max:2 apply:look],
            [DemoEntry slider:@"lookUp" label:@"look above horizon (deg)" min:0 max:90 apply:look],
            [DemoEntry toggle:@"celestialSun" label:@"sun" apply:skyObjects],
            [DemoEntry toggle:@"celestialMoon" label:@"moon" apply:skyObjects],
            [DemoEntry toggle:@"celestialMoonPhase" label:@"moon phase" apply:skyObjects],
            [DemoEntry toggle:@"celestialArc" label:@"sun path today" apply:skyObjects],
            [DemoEntry toggle:@"celestialMoonArc" label:@"moon path today" apply:skyObjects],
        ]],
        [self section:@"STARS" entries:@[
            [DemoEntry toggle:@"starsStars" label:@"stars" apply:skyObjects],
            [DemoEntry toggle:@"starsFigures" label:@"constellations" apply:skyObjects],
            [DemoEntry toggle:@"starsLabels" label:@"constellation names" apply:skyObjects],
            [DemoEntry toggle:@"starsPlanets" label:@"planets" apply:skyObjects],
            [DemoEntry toggle:@"starsEquator" label:@"celestial equator" apply:skyObjects],
            // No map at all: the layers leave the layer list, so this costs an empty map.
            [DemoEntry toggle:@"starSky" label:@"star sky (no map, transparent)" apply:^{
                [demo applyStarSky:[DemoConfig boolFor:@"starSky"]];
            }],
            [DemoEntry toggle:@"starSkyOrientation" label:@"follow device orientation" apply:^{
                [demo setOrientationFollowing:[DemoConfig boolFor:@"starSkyOrientation"]];
            }],
            // The camera preview goes BEHIND the transparent map: the sky over what the camera sees.
            [DemoEntry toggle:@"starSkyCamera" label:@"camera behind (AR sky)" apply:^{
                [demo setCameraPreviewEnabled:[DemoConfig boolFor:@"starSkyCamera"]];
            }],
        ]],
        [self section:@"RELIEF" entries:@[
            // One switch for the whole view: the pieces below are independent, and each one on its
            // own looks like nothing happens (the surface hides under the map, the names need
            // summits). Entering it FLIES there - one camera move that pulls back, comes down at
            // the panorama's zoom and tilt, and lifts the viewpoint while the terrain loads.
            [DemoEntry toggle:@"peakfinder" label:@"peak finder mode" apply:^{
                if ([DemoConfig boolFor:@"peakfinder"]) {
                    [demo flyToPeakFinder];
                } else {
                    [demo setPeakFinderMode:NO];
                }
            }],
            [DemoEntry toggle:@"reliefSurface" label:@"relief surface" apply:relief],
            [DemoEntry toggle:@"reliefOutline" label:@"relief outline effect" apply:^{
                [demo setReliefOutlineEnabled:[DemoConfig boolFor:@"reliefOutline"]];
            }],
            [DemoEntry toggle:@"reliefDark" label:@"dark palette" apply:^{
                [demo setReliefDark:[DemoConfig boolFor:@"reliefDark"]];
            }],
            [DemoEntry toggle:@"ar" label:@"AR (over the camera)" apply:^{
                [demo setArMode:[DemoConfig boolFor:@"ar"]];
            }],
            [DemoEntry slider:@"reliefWidth" label:@"outline width (px)" min:0.5 max:4 apply:outline],
            [DemoEntry slider:@"reliefHorizonBoost" label:@"horizon boost" min:0 max:8 apply:outline],
            [DemoEntry slider:@"reliefThreshold" label:@"silhouette threshold" min:0.1 max:4 apply:outline],
            [DemoEntry slider:@"reliefCrease" label:@"ridge lines" min:0 max:1 apply:outline],
            [DemoEntry slider:@"reliefShade" label:@"shade strength" min:0 max:1 apply:relief],
            [DemoEntry slider:@"reliefAmbient" label:@"ambient" min:0 max:1 apply:relief],
            [DemoEntry slider:@"reliefHaze" label:@"haze" min:0 max:1 apply:^{
                [demo applyReliefSurface];
                [demo applyReliefOutlineParameters];
            }],
            [DemoEntry slider:@"reliefHazeDistance" label:@"haze distance (m)" min:5000 max:200000 apply:relief],
            [DemoEntry slider:@"peakFinderTilt" label:@"panorama tilt" min:0 max:90 apply:camera],
            // The peak labels are style-driven, so every knob here rebuilds the layer.
            [DemoEntry slider:@"peaksBand" label:@"label band (screen)" min:0 max:0.6 apply:peaks],
            [DemoEntry slider:@"peaksAngle" label:@"label angle" min:0 max:90 apply:peaks],
            [DemoEntry slider:@"peaksStep" label:@"row step (px)" min:8 max:60 apply:peaks],
            [DemoEntry slider:@"peaksMaxDistance" label:@"max distance (m)" min:0 max:300000 apply:peaks],
            [DemoEntry toggle:@"peaksPinTop" label:@"labels pinned to top" apply:peaks],
            // How far behind the terrain an anchor may sit and still be labelled. A summit ON the
            // ridge line is a hair behind it as far as the depth buffer is concerned.
            [DemoEntry slider:@"occlusionTolerance" label:@"label occlusion tolerance" min:0 max:0.5 apply:terrain],
        ]],
        [self section:@"SKY" entries:@[
            [DemoEntry toggle:@"sky" label:@"sky" apply:sky],
            [DemoEntry slider:@"fogBlend" label:@"sky fog blend (0-1)" min:0 max:0.5 apply:fog],
            [DemoEntry slider:@"fogHorizon" label:@"sky fog horizon (deg, <0=auto)" min:-1 max:30 apply:fog],
        ]],
        [self section:@"FOG / DISTANCE" entries:@[
            [DemoEntry toggle:@"fog" label:@"fog" apply:fog],
            // Multiples of the camera-to-focus distance, so one pair holds at every zoom.
            [DemoEntry slider:@"fogRangeStart" label:@"fog range start (x cam dist)" min:0 max:4 apply:fog],
            [DemoEntry slider:@"fogRangeEnd" label:@"fog range end (x cam dist)" min:0 max:20 apply:fog],
            [DemoEntry slider:@"lodFactor" label:@"tile LOD (x tangram)" min:0 max:4 apply:^{
                [demo applyOptions];
            }],
            [DemoEntry slider:@"coarsening" label:@"tile coarsening (levels)" min:0 max:6 apply:terrain],
            [DemoEntry slider:@"viewDistance" label:@"view distance (x tangram)" min:0 max:4 apply:terrain],
            // Absolute distance wins over the factor above: the ground reaches the same distance
            // whatever the camera's height and pitch.
            [DemoEntry slider:@"viewDistanceMeters" label:@"view distance (m, 0=factor)" min:0 max:300000 apply:terrain],
        ]],
        [self section:@"CAMERA" entries:@[
            [DemoEntry slider:@"zoom" label:@"zoom" min:1 max:20 apply:camera],
            [DemoEntry slider:@"tilt" label:@"tilt" min:0 max:90 apply:camera],
            [DemoEntry slider:@"rotation" label:@"rotation" min:-180 max:180 apply:camera],
        ]],
        [self section:@"DEBUG" entries:@[
            [DemoEntry toggle:@"tileBorders" label:@"tile borders" apply:debug],
        ]],
        [self section:@"ACTIONS" entries:@[
            [DemoEntry button:@"maneuver gallery: reseed" apply:^{
                [DemoTests run:@"maneuverGallery" demo:demo];
            }],
            [DemoEntry button:@"maneuver head: next svg" apply:^{
                [DemoTests run:@"maneuverHead" demo:demo];
            }],
            [DemoEntry button:@"online routing test" apply:^{
                [DemoTests run:@"onlineRouting" demo:demo];
            }],
            [DemoEntry button:@"vector tile search test" apply:^{
                [DemoTests run:@"search" demo:demo];
            }],
            [DemoEntry button:@"geojson line test" apply:^{
                [DemoTests run:@"geojsonLine" demo:demo];
            }],
            [DemoEntry button:@"geojson bench: long routes" apply:^{
                [DemoTests run:@"geojsonBench" demo:demo];
            }],
            [DemoEntry button:@"popup font sampler" apply:^{
                [DemoTests run:@"popupFonts" demo:demo];
            }],
            [DemoEntry button:@"clear test layers" apply:^{
                [DemoTests run:@"clear" demo:demo];
            }],
        ]],
    ];
}

- (void)applyFilter {
    if (!self.query.length) {
        self.visibleSections = self.allSections;
        return;
    }
    // A search flattens the accordion: matching rows show whatever their section's state, since a
    // hit inside a closed section would otherwise look like no hit at all.
    NSMutableArray<DemoSection *> *filtered = [NSMutableArray array];
    for (DemoSection *section in self.allSections) {
        NSMutableArray<DemoEntry *> *matches = [NSMutableArray array];
        for (DemoEntry *entry in section.entries) {
            if ([entry matches:self.query]) {
                [matches addObject:entry];
            }
        }
        if (matches.count) {
            [filtered addObject:[self section:section.title entries:matches]];
        }
    }
    self.visibleSections = filtered;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor systemBackgroundColor];

    self.searchBar = [[UISearchBar alloc] init];
    self.searchBar.placeholder = @"Search settings";
    self.searchBar.delegate = self;
    self.searchBar.searchBarStyle = UISearchBarStyleMinimal;
    self.searchBar.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:self.searchBar];

    self.tableView = [[UITableView alloc] initWithFrame:CGRectZero style:UITableViewStyleInsetGrouped];
    self.tableView.translatesAutoresizingMaskIntoConstraints = NO;
    self.tableView.dataSource = self;
    self.tableView.delegate = self;
    self.tableView.keyboardDismissMode = UIScrollViewKeyboardDismissModeOnDrag;
    [self.view addSubview:self.tableView];

    UILayoutGuide *safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [self.searchBar.topAnchor constraintEqualToAnchor:safe.topAnchor constant:8],
        [self.searchBar.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [self.searchBar.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [self.tableView.topAnchor constraintEqualToAnchor:self.searchBar.bottomAnchor],
        [self.tableView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [self.tableView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [self.tableView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
    ]];
}

// ---- search ----

- (void)searchBar:(UISearchBar *)searchBar textDidChange:(NSString *)text {
    self.query = text;
    [self applyFilter];
    [self.tableView reloadData];
}

- (void)searchBarSearchButtonClicked:(UISearchBar *)searchBar {
    [searchBar resignFirstResponder];
}

// ---- table ----

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView {
    return self.visibleSections.count;
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    DemoSection *demoSection = self.visibleSections[section];
    return demoSection.collapsed ? 0 : demoSection.entries.count;
}

- (UIView *)tableView:(UITableView *)tableView viewForHeaderInSection:(NSInteger)section {
    DemoSection *demoSection = self.visibleSections[section];

    UITableViewHeaderFooterView *header = [[UITableViewHeaderFooterView alloc] initWithReuseIdentifier:nil];
    UIListContentConfiguration *content = [UIListContentConfiguration groupedHeaderConfiguration];
    content.text = [NSString stringWithFormat:@"%@  %@",
                    demoSection.collapsed ? @"▸" : @"▾", demoSection.title];
    header.contentConfiguration = content;

    header.tag = section;
    [header addGestureRecognizer:
        [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(headerTapped:)]];
    return header;
}

- (void)headerTapped:(UITapGestureRecognizer *)recognizer {
    NSInteger index = recognizer.view.tag;
    DemoSection *demoSection = self.visibleSections[index];
    demoSection.collapsed = !demoSection.collapsed;
    [self.tableView reloadSections:[NSIndexSet indexSetWithIndex:index]
                  withRowAnimation:UITableViewRowAnimationAutomatic];
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath {
    DemoEntry *entry = self.visibleSections[indexPath.section].entries[indexPath.row];
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1
                                                   reuseIdentifier:nil];
    cell.textLabel.text = entry.label;
    cell.selectionStyle = UITableViewCellSelectionStyleNone;

    switch (entry.kind) {
        case DemoEntryToggle: {
            UISwitch *toggle = [[UISwitch alloc] init];
            toggle.on = [DemoConfig boolFor:entry.key];
            setEntry(toggle, entry);
            [toggle addTarget:self action:@selector(toggleChanged:)
             forControlEvents:UIControlEventValueChanged];
            cell.accessoryView = toggle;
            break;
        }
        case DemoEntrySlider: {
            UISlider *slider = [[UISlider alloc] initWithFrame:CGRectMake(0, 0, 150, 30)];
            slider.minimumValue = entry.minimum;
            slider.maximumValue = entry.maximum;
            slider.value = [DemoConfig floatFor:entry.key];
            setEntry(slider, entry);
            // Continuous would rebuild the layer stack on every pixel of drag.
            slider.continuous = NO;
            [slider addTarget:self action:@selector(sliderChanged:)
             forControlEvents:UIControlEventValueChanged];
            cell.accessoryView = slider;
            cell.detailTextLabel.text = [NSString stringWithFormat:@"%g", slider.value];
            break;
        }
        case DemoEntryChoice: {
            cell.detailTextLabel.text = [DemoConfig stringFor:entry.key];
            cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
            cell.selectionStyle = UITableViewCellSelectionStyleDefault;
            break;
        }
        case DemoEntryAction: {
            cell.textLabel.textColor = [UIColor systemBlueColor];
            cell.selectionStyle = UITableViewCellSelectionStyleDefault;
            break;
        }
    }
    return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath {
    DemoEntry *entry = self.visibleSections[indexPath.section].entries[indexPath.row];
    [tableView deselectRowAtIndexPath:indexPath animated:YES];

    if (entry.kind == DemoEntryChoice) {
        // Cycle rather than push a picker: two or three values per knob, and one tap is faster.
        NSString *current = [DemoConfig stringFor:entry.key];
        NSUInteger index = [entry.choices indexOfObject:current];
        index = (index == NSNotFound) ? 0 : (index + 1) % entry.choices.count;
        [DemoConfig setValue:entry.choices[index] forKey:entry.key];
        [tableView reloadRowsAtIndexPaths:@[indexPath] withRowAnimation:UITableViewRowAnimationNone];
        if (entry.apply) {
            entry.apply();
        }
    } else if (entry.kind == DemoEntryAction && entry.apply) {
        entry.apply();
    }
}

- (void)toggleChanged:(UISwitch *)sender {
    DemoEntry *entry = getEntry(sender);
    [DemoConfig setValue:@(sender.isOn) forKey:entry.key];
    if (entry.apply) {
        entry.apply();
    }
}

- (void)sliderChanged:(UISlider *)sender {
    DemoEntry *entry = getEntry(sender);
    [DemoConfig setValue:@(sender.value) forKey:entry.key];
    [self.tableView reloadData];
    if (entry.apply) {
        entry.apply();
    }
}

@end
