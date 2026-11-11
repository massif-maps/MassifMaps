#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * A legend resolved from the live style: switch the Massif variant and every swatch follows.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/styles/MapLegendExample.java.
 */
@interface MSFMapLegendExample : NSObject <MSFExample>
@end

@implementation MSFMapLegendExample {
    UIStackView *_list;
}

+ (NSString *)exampleId {
    return @"map-legend";
}

static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

static UIColor *colorOf(NSDictionary *item);

static NSArray<NSArray<NSString *> *> *variants(void) {
    return @[ @[ @"streets", @"Streets" ], @[ @"outdoor", @"Outdoor" ],
              @[ @"topo", @"Topo" ], @[ @"eink", @"E-ink" ] ];
}

- (void)startWithHost:(id<MSFExampleHost>)host {
    MSFMassifMap *map = host.map;

    MSFMassifObject *style = [map style:@"massif"
                                   spec:[[MSFSpec of:@"mbvt"]
                                           set:@"project" value:[[[MSFSpec of:@"project"]
                                               set:@"assets" value:[[MSFSpec of:@"zip"]
                                                   set:@"data" value:[[MSFSpec of:@"url"]
                                                       set:@"url" value:@"assets://styles/massif.zip"]]]
                                               set:@"name" value:@"outdoor"]]
                                  error:nil];
    [map addLayer:@"basemap"
             spec:[[[MSFSpec of:@"vector"]
                     set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
                         set:@"databasePath" value:[host cachePath:@"openfreemap.db"]]
                         set:@"capacity" value:@(100 * 1024 * 1024)]
                         set:@"source" value:[[[[MSFSpec of:@"http"]
                             set:@"url" value:@"https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf"]
                             set:@"maxZoom" value:@14]
                             set:@"HTTPHeaders" value:[[MSFSpec object] set:@"User-Agent" value:kUserAgent]]]]
                     set:@"style" value:@"massif"]
            error:nil];
    [map.camera moveTo:[MSFPosition positionWithLng:5.7262 lat:45.2] zoom:13];

    [self addPanelTo:host.viewController.view top:host.topInset + 8];
    [self showLegend:style];

    __weak __typeof(self) weakSelf = self;
    for (NSArray<NSString *> *variant in variants()) {
        [host button:variant[1] action:^{
            [style set:@"params.variant" value:variant[0]];
            [weakSelf showLegend:style];
            [host caption:[NSString stringWithFormat:@"Massif %@: the legend was resolved again from the style.", variant[1]]];
        }];
    }
    [host caption:@"Massif Outdoor, and its legend: colours read from the compiled style."];
}

- (void)addPanelTo:(UIView *)view top:(CGFloat)top {
    _list = [[UIStackView alloc] init];
    _list.axis = UILayoutConstraintAxisVertical;
    _list.spacing = 4;
    _list.layoutMarginsRelativeArrangement = YES;
    _list.layoutMargins = UIEdgeInsetsMake(4, 10, 4, 10);
    _list.translatesAutoresizingMaskIntoConstraints = NO;
    UIScrollView *panel = [[UIScrollView alloc] init];
    panel.backgroundColor = [UIColor colorWithWhite:1 alpha:0.93];
    panel.translatesAutoresizingMaskIntoConstraints = NO;
    [panel addSubview:_list];
    [view addSubview:panel];
    [NSLayoutConstraint activateConstraints:@[
        [panel.topAnchor constraintEqualToAnchor:view.topAnchor constant:top],
        [panel.trailingAnchor constraintEqualToAnchor:view.safeAreaLayoutGuide.trailingAnchor constant:-8],
        [panel.widthAnchor constraintEqualToConstant:230],
        [panel.heightAnchor constraintEqualToAnchor:view.heightAnchor multiplier:0.5],
        [_list.topAnchor constraintEqualToAnchor:panel.contentLayoutGuide.topAnchor],
        [_list.bottomAnchor constraintEqualToAnchor:panel.contentLayoutGuide.bottomAnchor],
        [_list.leadingAnchor constraintEqualToAnchor:panel.contentLayoutGuide.leadingAnchor],
        [_list.trailingAnchor constraintEqualToAnchor:panel.contentLayoutGuide.trailingAnchor],
        [_list.widthAnchor constraintEqualToAnchor:panel.frameLayoutGuide.widthAnchor],
    ]];
}

/** getLegend reads the decoder's CURRENT parameters: call it again after any change. */
- (void)showLegend:(MSFMassifObject *)style {
    for (UIView *row in [_list.arrangedSubviews copy]) {
        [_list removeArrangedSubview:row];
        [row removeFromSuperview];
    }
    MSFMassifObject *result = [style call:@"getLegend" args:@[ @"" ] error:nil];
    NSData *json = [result.json dataUsingEncoding:NSUTF8StringEncoding];
    NSDictionary *legend = json ? [NSJSONSerialization JSONObjectWithData:json options:0 error:nil] : nil;
    [result destroy];
    for (NSDictionary *section in legend[@"sections"]) {
        UILabel *heading = [self labelFor:section[@"label"] size:13];
        heading.font = [UIFont boldSystemFontOfSize:13];
        [_list addArrangedSubview:heading];
        for (NSDictionary *item in section[@"items"]) {
            [_list addArrangedSubview:[self rowFor:item]];
        }
    }
}

- (UIView *)rowFor:(NSDictionary *)item {
    UIView *swatch = [[UIView alloc] init];
    swatch.backgroundColor = colorOf(item);
    [swatch.widthAnchor constraintEqualToConstant:44].active = YES;
    [swatch.heightAnchor constraintEqualToConstant:16].active = YES;
    UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[ swatch, [self labelFor:item[@"label"] size:12] ]];
    row.spacing = 8;
    row.alignment = UIStackViewAlignmentCenter;
    return row;
}

/** A label is a string or a {lang: text} object. */
- (UILabel *)labelFor:(id)label size:(CGFloat)size {
    if ([label isKindOfClass:[NSDictionary class]]) {
        label = label[@"en"] ?: [label allValues].firstObject;
    }
    UILabel *view = [[UILabel alloc] init];
    view.text = [label isKindOfClass:[NSString class]] ? label : @"";
    view.font = [UIFont systemFontOfSize:size];
    view.textColor = UIColor.blackColor;
    view.numberOfLines = 0;
    return view;
}

/** The top-most colour the item draws with, whatever its kind. */
static UIColor *colorOf(NSDictionary *item) {
    NSArray *lines = item[@"lines"];
    NSString *css = (lines.count ? lines.lastObject : item)[@"color"];
    for (NSString *key in @[ @"plate", @"marker", @"text" ]) {
        if (![css isKindOfClass:[NSString class]] || css.length == 0) {
            css = item[key][@"color"];
        }
    }
    if (![css isKindOfClass:[NSString class]] || css.length < 7) {
        return UIColor.clearColor;
    }
    unsigned rgb = 0;
    unsigned alpha = 255;
    [[NSScanner scannerWithString:[css substringWithRange:NSMakeRange(1, 6)]] scanHexInt:&rgb];
    if (css.length == 9) {
        [[NSScanner scannerWithString:[css substringFromIndex:7]] scanHexInt:&alpha];
    }
    return [UIColor colorWithRed:((rgb >> 16) & 0xFF) / 255.0 green:((rgb >> 8) & 0xFF) / 255.0
                            blue:(rgb & 0xFF) / 255.0 alpha:alpha / 255.0];
}

@end
