#import "MSFExampleViewController.h"
#import "MSFExampleCodeViewController.h"
#import "DemoCfg.h"
#import <objc/runtime.h>

#import "MassifMaps.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

@interface MSFExampleViewController ()
@property (nonatomic, strong) MSFExampleEntry *entry;
@property (nonatomic, strong) id<MSFExample> example;
@property (nonatomic, strong) MSFMapView *mapView;
@property (nonatomic, strong) MSFMassifMap *massifMap;
@property (nonatomic, strong) UIStackView *controls;
@property (nonatomic, strong) UILabel *captionLabel;
@property (nonatomic, strong) UIView *topBar;
@property (nonatomic, strong) NSMutableArray<NSTimer *> *timers;
@end

@implementation MSFExampleViewController

- (instancetype)initWithEntry:(MSFExampleEntry *)entry {
    if ((self = [super initWithNibName:nil bundle:nil])) {
        _entry = entry;
        _timers = [NSMutableArray array];
    }
    return self;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;

    _mapView = [[MSFMapView alloc] initWithFrame:self.view.bounds];
    _mapView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self.view addSubview:_mapView];

    // lon/lat everywhere. Every example is written in degrees, and one that had to convert would
    // be teaching the wrong thing.
    MSFOptions *options = [_mapView getOptions];
    [options setBaseProjection:[[MSFEPSG4326 alloc] init]];
    [options setRestrictedPanning:YES];
    [options setSeamlessPanning:YES];

    [self buildChrome];

    _massifMap = [MSFMassifMap attach:_mapView objectId:_entry.identifier];
    _massifMap.eventProjection = @"EPSG:4326";
}

- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    if (_example || !_entry.exampleClass) {
        return;
    }
    // Started from viewDidAppear, not viewDidLoad: restricted panning clamps a focus set while
    // the map view still has no size, and the camera silently ends up somewhere else entirely -
    // the same trap Android's startWhenLaidOut exists for.
    _example = [[_entry.exampleClass alloc] init];
    @try {
        [_example startWithHost:self];
    } @catch (NSException *exception) {
        NSLog(@"example '%@' failed: %@", _entry.identifier, exception);
        [self toast:[NSString stringWithFormat:@"Failed: %@", exception.reason]];
    }
    [self applyCameraOverrides];
}

/**
 * Camera overrides from the launch arguments, applied AFTER the example set its own.
 *
 *   xcrun simctl launch <device> com.massifmaps.MassifDemo -example terrain-3d -zoom 11.5 -tilt 33
 *
 * Composing a screenshot is a loop, and a rebuild per nudge makes it unaffordable. Same keys as
 * Android's intent extras.
 */
- (void)applyCameraOverrides {
    if (![DemoCfg has:@"lon"] && ![DemoCfg has:@"lat"] && ![DemoCfg has:@"zoom"]
        && ![DemoCfg has:@"rotation"] && ![DemoCfg has:@"tilt"]) {
        return;
    }
    MSFMapPos *focus = _massifMap.camera.currentPosition;
    double lon = [DemoCfg doubleFor:@"lon" defaultValue:[focus getX]];
    double lat = [DemoCfg doubleFor:@"lat" defaultValue:[focus getY]];
    float zoom = [DemoCfg floatFor:@"zoom" defaultValue:_massifMap.camera.currentZoom];
    float rotation = [DemoCfg floatFor:@"rotation" defaultValue:_massifMap.camera.currentRotation];
    float tilt = [DemoCfg floatFor:@"tilt" defaultValue:_massifMap.camera.currentTilt];
    [_massifMap.camera moveTo:[[MSFMapPos alloc] initWithX:lon y:lat] zoom:zoom
                     rotation:rotation tilt:tilt];
    NSLog(@"camera lon=%.5f lat=%.5f zoom=%.2f rotation=%.0f tilt=%.0f", lon, lat, zoom, rotation, tilt);
}

- (void)dealloc {
    for (NSTimer *timer in _timers) {
        [timer invalidate];
    }
    if ([_example respondsToSelector:@selector(stop)]) {
        [_example stop];
    }
    // Drops the map's id, its handlers and every layer it built, so leaving an example and
    // opening another cannot collide on an id.
    [_massifMap detach];
}

// --- chrome ------------------------------------------------------------------------------------

- (void)buildChrome {
    if (self.chromeHidden) {
        return;
    }
    _topBar = [[UIView alloc] init];
    _topBar.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_topBar];

    UIButton *back = [UIButton buttonWithType:UIButtonTypeSystem];
    [back setImage:[UIImage systemImageNamed:@"chevron.left"] forState:UIControlStateNormal];
    [back addTarget:self action:@selector(goBack) forControlEvents:UIControlEventTouchUpInside];
    [self styleAsPill:back];

    UILabel *title = [[UILabel alloc] init];
    title.text = _entry.title;
    title.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold];
    title.textAlignment = NSTextAlignmentCenter;
    [self styleAsPill:title];

    UIButton *code = [UIButton buttonWithType:UIButtonTypeSystem];
    [code setImage:[UIImage systemImageNamed:@"chevron.left.forwardslash.chevron.right"]
          forState:UIControlStateNormal];
    [code addTarget:self action:@selector(showCode) forControlEvents:UIControlEventTouchUpInside];
    [self styleAsPill:code];

    UIStackView *bar = [[UIStackView alloc] initWithArrangedSubviews:@[ back, title, code ]];
    bar.spacing = 8;
    bar.alignment = UIStackViewAlignmentFill;
    bar.translatesAutoresizingMaskIntoConstraints = NO;
    [_topBar addSubview:bar];

    _controls = [[UIStackView alloc] init];
    _controls.spacing = 8;
    _controls.translatesAutoresizingMaskIntoConstraints = NO;
    UIScrollView *controlScroll = [[UIScrollView alloc] init];
    controlScroll.showsHorizontalScrollIndicator = NO;
    controlScroll.translatesAutoresizingMaskIntoConstraints = NO;
    [controlScroll addSubview:_controls];
    [self.view addSubview:controlScroll];

    _captionLabel = [[UILabel alloc] init];
    _captionLabel.numberOfLines = 0;
    _captionLabel.textColor = UIColor.whiteColor;
    _captionLabel.backgroundColor = [UIColor colorWithWhite:0.05 alpha:0.7];
    _captionLabel.font = [UIFont systemFontOfSize:14];
    _captionLabel.hidden = YES;
    _captionLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_captionLabel];

    UILayoutGuide *safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [_topBar.topAnchor constraintEqualToAnchor:safe.topAnchor constant:8],
        [_topBar.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:12],
        [_topBar.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-12],
        [bar.topAnchor constraintEqualToAnchor:_topBar.topAnchor],
        [bar.bottomAnchor constraintEqualToAnchor:_topBar.bottomAnchor],
        [bar.leadingAnchor constraintEqualToAnchor:_topBar.leadingAnchor],
        [bar.trailingAnchor constraintEqualToAnchor:_topBar.trailingAnchor],
        [back.widthAnchor constraintEqualToConstant:44],
        [code.widthAnchor constraintEqualToConstant:44],
        [bar.heightAnchor constraintEqualToConstant:38],

        [controlScroll.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:12],
        [controlScroll.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-12],
        [controlScroll.bottomAnchor constraintEqualToAnchor:_captionLabel.topAnchor constant:-10],
        [controlScroll.heightAnchor constraintEqualToConstant:38],
        [_controls.topAnchor constraintEqualToAnchor:controlScroll.topAnchor],
        [_controls.bottomAnchor constraintEqualToAnchor:controlScroll.bottomAnchor],
        [_controls.leadingAnchor constraintEqualToAnchor:controlScroll.leadingAnchor],
        [_controls.trailingAnchor constraintEqualToAnchor:controlScroll.trailingAnchor],
        [_controls.heightAnchor constraintEqualToAnchor:controlScroll.heightAnchor],

        [_captionLabel.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [_captionLabel.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [_captionLabel.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
    ]];
}

- (void)styleAsPill:(UIView *)view {
    view.backgroundColor = [UIColor colorWithWhite:1 alpha:0.92];
    view.layer.cornerRadius = 19;
    view.clipsToBounds = YES;
}

- (void)goBack {
    [self.navigationController popViewControllerAnimated:YES];
}

- (void)showCode {
    [self.navigationController pushViewController:
        [[MSFExampleCodeViewController alloc] initWithEntry:_entry] animated:YES];
}

// --- MSFExampleHost ------------------------------------------------------------------------------

- (MSFMassifMap *)map {
    return _massifMap;
}

- (UIViewController *)viewController {
    return self;
}

- (void)caption:(NSString *)text {
    dispatch_async(dispatch_get_main_queue(), ^{
        self.captionLabel.text = text ? [@"  " stringByAppendingString:text] : nil;
        self.captionLabel.hidden = text.length == 0;
    });
}

- (void)button:(NSString *)label action:(void (^)(void))action {
    dispatch_async(dispatch_get_main_queue(), ^{
        UIButton *button = [self pillButtonWithTitle:label on:NO];
        [self onTap:button run:^(UIButton *tapped) { action(); }];
        [self.controls addArrangedSubview:button];
    });
}

- (void)toggle:(NSString *)label on:(BOOL)on action:(void (^)(BOOL))action {
    dispatch_async(dispatch_get_main_queue(), ^{
        UIButton *button = [self pillButtonWithTitle:label on:on];
        button.tag = on ? 1 : 0;
        [self onTap:button run:^(UIButton *tapped) {
            BOOL next = tapped.tag == 0;
            tapped.tag = next ? 1 : 0;
            [self paint:tapped on:next];
            action(next);
        }];
        [self.controls addArrangedSubview:button];
    });
}

- (void)toast:(NSString *)text {
    dispatch_async(dispatch_get_main_queue(), ^{
        [self caption:text];
    });
}

- (void)after:(NSTimeInterval)seconds run:(void (^)(void))action {
    NSTimer *timer = [NSTimer scheduledTimerWithTimeInterval:seconds repeats:NO
                                                     block:^(NSTimer *t) { action(); }];
    [_timers addObject:timer];
}

- (UIButton *)pillButtonWithTitle:(NSString *)title on:(BOOL)on {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    [button setTitle:title forState:UIControlStateNormal];
    button.titleLabel.font = [UIFont systemFontOfSize:14 weight:UIFontWeightMedium];
    button.contentEdgeInsets = UIEdgeInsetsMake(0, 14, 0, 14);
    button.layer.cornerRadius = 19;
    button.clipsToBounds = YES;
    [self paint:button on:on];
    return button;
}

- (void)paint:(UIButton *)button on:(BOOL)on {
    button.backgroundColor = on ? [UIColor colorWithRed:0.12 green:0.44 blue:0.36 alpha:1]
                                : [UIColor colorWithWhite:1 alpha:0.92];
    [button setTitleColor:on ? UIColor.whiteColor : UIColor.blackColor
                 forState:UIControlStateNormal];
}

/** UIControl takes a target/selector, and every control here wants a block instead. */
- (void)onTap:(UIButton *)button run:(void (^)(UIButton *))block {
    objc_setAssociatedObject(button, @selector(onTap:run:), [block copy],
                             OBJC_ASSOCIATION_COPY_NONATOMIC);
    [button addTarget:self action:@selector(handleTap:) forControlEvents:UIControlEventTouchUpInside];
}

- (void)handleTap:(UIButton *)button {
    void (^block)(UIButton *) = objc_getAssociatedObject(button, @selector(onTap:run:));
    if (block) {
        block(button);
    }
}

@end
