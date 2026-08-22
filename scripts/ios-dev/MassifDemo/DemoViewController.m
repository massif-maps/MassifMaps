#import "DemoViewController.h"
#import "DemoCfg.h"
#import "DemoConfig.h"
#import "DemoMap.h"
#import "DemoPanel.h"
#import "DemoToast.h"
#import "DemoLive.h"

#import "api/MSFMassif.h"
#import "api/MSFMassifObject.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMapEvents.h"

@interface DemoViewController ()
@property (nonatomic, strong) MSFMapView *mapView;
@property (nonatomic, strong) DemoMap *demo;
/** The facade sugar (#146), when -apiSugar true is on the command line. */
@property (nonatomic, strong) MSFMassifMap *sugarMap;
@property (nonatomic, strong) MSFSubscription *sugarClick;
@property (nonatomic, strong) MSFSubscription *sugarFeature;
/** The camera readout along the bottom - Android's zoomText. */
@property (nonatomic, strong) UILabel *statusLabel;
@property (nonatomic, strong) MSFMapEventListener *mapListener;
- (void)updateStatusLabel;
@end

/**
 * Camera readout (also what a scripted run reads back out of the log) + a terrain-aware click
 * probe, the same pair SecondFragment.installMapListener sets up on Android.
 */
@interface DemoMapListener : MSFMapEventListener
@property (nonatomic, weak) DemoViewController *controller;
@end

@implementation DemoViewController

- (void)viewDidLoad {
    [super viewDidLoad];

    // Android reads its knobs in the fragment; here the launch arguments are already folded into
    // NSUserDefaults by the time the view loads, so this is the equivalent hook.
    [DemoConfig applyLaunchArgumentOverrides];

    // Native logs go to stdout, so they only show with 'simctl launch --console-pty'.
    [MSFLog setShowInfo:YES];
    [MSFLog setShowDebug:YES];
    [MSFLog setShowWarn:YES];
    [MSFLog setShowError:YES];

    self.mapView = [[MSFMapView alloc] initWithFrame:self.view.bounds];
    self.mapView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self.view addSubview:self.mapView];

    // Base map options that are not part of the demo configuration itself - the same block
    // SecondFragment.java sets on Android, and the reason double-tap zoom did nothing here:
    // Options.zoomGestures is OFF by default in the SDK, so both the double-tap step and the
    // double-tap-and-drag zoom are disabled until an app asks for them.
    // The base projection is EPSG4326 on both demos as well, so a camera copied from one command
    // line to the other lands in the same place - zoom levels are per projection.
    MSFOptions *options = [self.mapView getOptions];
    [options setBaseProjection:[[MSFEPSG4326 alloc] init]];
    [options setZoomGestures:YES];
    [options setRestrictedPanning:YES];
    [options setSeamlessPanning:YES];
    [options setRotatable:YES];
    [options setTiltRange:[[MSFMapRange alloc] initWithMin:30 max:90]];
    [options setPanningMode:MSF_PANNING_MODE_STICKY];

    self.demo = [[DemoMap alloc] initWithMapView:self.mapView];
    // So massifdemo://config can change a knob on the RUNNING map (see DemoLive).
    [DemoLive attach:self.demo];
    [self.demo build];

    if ([DemoConfig boolFor:@"ui"]) {
        [self addSettingsButton];
        [self addStatusLabel];
    }
    [self installMapListener];
    [self installApiSugar];
}

/**
 * Exercises the Objective-C facade sugar (#146), the counterpart of the Android demo's
 * --es apiSugar true:
 *
 *   xcrun simctl launch --console-pty <device> com.massifmaps.MassifDemo -apiSugar true
 *
 * Installed AFTER the demo's own listener on purpose: the bridge chains to whatever was there, so
 * the camera readout keeps working alongside it.
 *
 * This demo's base projection is EPSG4326, so the interesting conversion is the other way round
 * from Android's - a position asked for in EPSG:3857 should come back in metres.
 */
- (void)installApiSugar {
    // DemoCfg, not DemoConfig: the latter only knows keys its own table registers, and this is a
    // knob rather than a demo setting.
    if (![DemoCfg boolFor:@"apiSugar" defaultValue:NO]) {
        return;
    }
    self.sugarMap = [MSFMassifMap attach:self.mapView objectId:@"demo"];
    if (!self.sugarMap) {
        NSLog(@"apiSugar: could not attach");
        return;
    }
    self.sugarMap.eventProjection = @"EPSG:4326";

    self.sugarClick = [self.sugarMap onClick:^(MSFMapClickEvent *e) {
        MSFMapPos *pos = e.position;
        NSLog(@"sugar map.clicked at [%f, %f] type=%d", [pos getX], [pos getY], e.clickType);
    }];

    // The demo builds its layers with the object API, which is exactly the migration case: adopt
    // the first vector tile one rather than expecting a facade id.
    MSFMassifLayer *layer = nil;
    for (int index = 0; index < self.sugarMap.layerCount; index++) {
        if ([[self.sugarMap rawLayerAt:index] isKindOfClass:[MSFVectorTileLayer class]]) {
            layer = [self.sugarMap adoptLayer:@"demoBase" atIndex:index];
            break;
        }
    }
    if (layer) {
        self.sugarFeature = [layer onFeatureClick:^(MSFVectorTileClickEvent *e) {
            MSFMapPos *pos = e.position;
            MSFMapPos *merc = [e getPos:@"featurePos" projection:@"EPSG:3857"];
            NSLog(@"sugar feature %lld layer=%@ at=[%f, %f] merc=[%f, %f] name=%@ geojsonLen=%lu",
                  e.featureId, e.layerName, [pos getX], [pos getY],
                  merc ? [merc getX] : 0, merc ? [merc getY] : 0,
                  [e property:@"name"] ?: @"(null)", (unsigned long)e.geoJson.length);
        }];
    }
    NSLog(@"apiSugar on, map=%d fogRangeStart=%f layer=%@",
          self.sugarMap.options.handle,
          [self.sugarMap.fog getDouble:@"rangeStart" defaultValue:-1],
          layer);
}

/** The camera readout, bottom centre - the counterpart of the Android layout's zoomText. */
- (void)addStatusLabel {
    UILabel *label = [[UILabel alloc] init];
    label.font = [UIFont monospacedDigitSystemFontOfSize:12 weight:UIFontWeightRegular];
    label.textColor = [UIColor whiteColor];
    label.backgroundColor = [UIColor colorWithWhite:0 alpha:0.55];
    label.textAlignment = NSTextAlignmentCenter;
    label.layer.cornerRadius = 6;
    label.clipsToBounds = YES;
    label.translatesAutoresizingMaskIntoConstraints = NO;
    // The map keeps every gesture it has under the readout.
    label.userInteractionEnabled = NO;
    [self.view addSubview:label];
    self.statusLabel = label;

    UILayoutGuide *safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [label.centerXAnchor constraintEqualToAnchor:safe.centerXAnchor],
        [label.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-16],
        [label.heightAnchor constraintEqualToConstant:24],
        [label.widthAnchor constraintGreaterThanOrEqualToConstant:240],
    ]];
    [self updateStatusLabel];
}

- (void)updateStatusLabel {
    MSFMapPos *focus = [[[self.mapView getOptions] getBaseProjection] toWgs84:[self.mapView getFocusPos]];
    self.statusLabel.text = [NSString stringWithFormat:@"z=%.2f  tilt=%.0f  %.5f, %.5f",
                             [self.mapView getZoom], [self.mapView getTilt],
                             [focus getY], [focus getX]];
}

- (void)installMapListener {
    DemoMapListener *listener = [[DemoMapListener alloc] init];
    listener.controller = self;
    self.mapListener = listener;
    [self.mapView setMapEventListener:listener];
}

/** Bottom-left gear, the same corner the Android demo puts it in. */
- (void)addSettingsButton {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    [button setImage:[UIImage systemImageNamed:@"gearshape.fill"] forState:UIControlStateNormal];
    button.tintColor = [UIColor labelColor];
    button.backgroundColor = [[UIColor systemBackgroundColor] colorWithAlphaComponent:0.85];
    button.layer.cornerRadius = 22;
    button.translatesAutoresizingMaskIntoConstraints = NO;
    [button addTarget:self action:@selector(showPanel) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:button];

    UILayoutGuide *safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [button.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:16],
        [button.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-16],
        [button.widthAnchor constraintEqualToConstant:44],
        [button.heightAnchor constraintEqualToConstant:44],
    ]];
}

/**
 * The panel comes up as a BOTTOM SHEET rather than a full-screen modal: the point of a knob is
 * watching the map change as you drag it, and a sheet at a medium detent keeps the map on screen.
 * Undimmed for the same reason, and .large is available for the long sections.
 */
- (void)showPanel {
    DemoPanel *panel = [[DemoPanel alloc] initWithDemo:self.demo];
    UINavigationController *nav = [[UINavigationController alloc] initWithRootViewController:panel];

    UISheetPresentationController *sheet = nav.sheetPresentationController;
    if (sheet) {
        sheet.detents = @[[UISheetPresentationControllerDetent mediumDetent],
                          [UISheetPresentationControllerDetent largeDetent]];
        sheet.prefersGrabberVisible = YES;
        sheet.prefersScrollingExpandsWhenScrolledToEdge = NO;
        // Keep the map live behind the sheet at the medium detent.
        sheet.largestUndimmedDetentIdentifier = UISheetPresentationControllerDetentIdentifierMedium;
        sheet.preferredCornerRadius = 16;
    }
    [self presentViewController:nav animated:YES completion:nil];
}

@end

@implementation DemoMapListener

- (void)onMapMoved {
    DemoViewController *controller = self.controller;
    if (!controller) {
        return;
    }
    MSFMapView *mapView = controller.mapView;
    MSFMapPos *focus = [[[mapView getOptions] getBaseProjection] toWgs84:[mapView getFocusPos]];
    NSLog(@"MassifDemo: lat=%.6f lng=%.6f rotation=%.2f z=%.2f tilt=%.0f",
          [focus getY], [focus getX], [mapView getRotation], [mapView getZoom], [mapView getTilt]);
    // The listener runs on the map's own thread; the label is UIKit.
    dispatch_async(dispatch_get_main_queue(), ^{ [controller updateStatusLabel]; });
}

- (void)onMapClicked:(MSFMapClickInfo *)mapClickInfo {
    DemoViewController *controller = self.controller;
    if (!controller) {
        return;
    }
    // getClickPos already resolves to the TERRAIN surface; the elevation query itself may block on
    // tile loading, so it runs off the caller's thread.
    MSFMapPos *wgs84Pos = [[[controller.mapView getOptions] getBaseProjection]
                          toWgs84:[mapClickInfo getClickPos]];
    DemoMap *demo = controller.demo;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        double elevation = [demo getElevation:wgs84Pos];
        [DemoToast show:[NSString stringWithFormat:@"%.5f, %.5f   %.0f m",
                         [wgs84Pos getY], [wgs84Pos getX], elevation]];
    });
}

@end
