#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * Shields that give their name the free side of the icon, font icons, plates and callout labels.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/styles/LabelStylingExample.java.
 */
@interface MSFLabelStylingExample : NSObject <MSFExample>
@end

/** A tile server wants to know who is asking: a real app identifies itself. */
static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

static NSDictionary *point(NSDictionary *properties, double lon, double lat) {
    return @{ @"type": @"Feature", @"properties": properties,
              @"geometry": @{ @"type": @"Point", @"coordinates": @[ @(lon), @(lat) ] } };
}

static NSString *collection(NSArray *features) {
    NSData *json = [NSJSONSerialization dataWithJSONObject:@{ @"type": @"FeatureCollection", @"features": features }
                                                   options:0
                                                     error:nil];
    return [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding];
}

/** Chamonix and the summits above it, at OpenFreeMap's own positions so they sit on the basemap. */
static NSArray *pois(void) {
    return @[
        point(@{ @"name": @"Gare de Chamonix", @"icon": @"railway", @"color": @"#3b6fd8" }, 6.87384, 45.92278),
        point(@{ @"name": @"Montenvers train", @"icon": @"railway", @"color": @"#3b6fd8" }, 6.87533, 45.92263),
        point(@{ @"name": @"Aiguille du Midi cable car", @"icon": @"aerialway", @"color": @"#3b6fd8" }, 6.87014, 45.91814),
        point(@{ @"name": @"Planpraz gondola", @"icon": @"aerialway", @"color": @"#3b6fd8" }, 6.86319, 45.92404),
        point(@{ @"name": @"Musée Alpin", @"icon": @"museum", @"color": @"#c7801a" }, 6.87126, 45.92402),
        point(@{ @"name": @"Tourist office", @"letter": @"i", @"color": @"#0f766e" }, 6.86835, 45.92344),
        point(@{ @"name": @"Parking du Mont Blanc", @"letter": @"P", @"color": @"#1d4ed8" }, 6.87284, 45.92495),
        point(@{ @"name": @"Refuge de Bellachat", @"icon": @"alpine_hut", @"color": @"#2f855a" }, 6.82961, 45.92218),
        point(@{ @"name": @"Refuge du Plan de l’Aiguille", @"icon": @"alpine_hut", @"color": @"#2f855a" }, 6.88273, 45.90561),
        point(@{ @"name": @"Refuge des Cosmiques", @"icon": @"alpine_hut", @"color": @"#2f855a" }, 6.88558, 45.87324),
    ];
}

static NSArray *peaks(void) {
    return @[
        point(@{ @"name": @"Mont Blanc", @"ele": @4807 }, 6.86517, 45.8327),
        point(@{ @"name": @"Aiguille du Midi", @"ele": @3842 }, 6.88735, 45.87864),
        point(@{ @"name": @"Aiguille du Plan", @"ele": @3673 }, 6.90722, 45.8917),
        point(@{ @"name": @"Aiguille de Blaitière", @"ele": @3522 }, 6.91304, 45.89928),
    ];
}

/** The TMB from Les Houches over the Brévent to La Flégère. */
static NSArray *trail(void) {
    return @[ @{
        @"type": @"Feature",
        @"properties": @{ @"ref": @"TMB" },
        @"geometry": @{ @"type": @"LineString", @"coordinates": @[
            @[ @6.7985, @45.8905 ], @[ @6.8135, @45.9075 ], @[ @6.82961, @45.92218 ], @[ @6.83783, @45.93392 ],
            @[ @6.85259, @45.93585 ], @[ @6.8712, @45.9481 ], @[ @6.8889, @45.9607 ],
        ] },
    } ];
}

static MSFSpec *massifAssets(void) {
    return [[MSFSpec of:@"zip"] set:@"data" value:[[MSFSpec of:@"url"] set:@"url" value:@"assets://styles/massif.zip"]];
}

/** One CartoCSS for the three layers; the toggles flip the two properties it takes. */
static NSString *labelStyle(BOOL freeSide, BOOL callouts) {
    return [@[
        @"@medium: 'ios:Helvetica Neue Medium, Roboto Medium, sans-serif Medium';",
        @"@bold: 'ios:Helvetica Neue Bold, Roboto Bold, sans-serif Bold';",
        @"#trail {",
        @"  line-color: #d6322b;",
        @"  line-width: 3;",
        @"  line-dasharray: 8, 4;",
        @"}",
        // The road-shield placement: upright, repeated along the line, on a plate.
        @"#trail::ref {",
        @"  text-name: [ref];",
        @"  text-face-name: @bold;",
        @"  text-size: 11;",
        @"  text-fill: #ffffff;",
        @"  text-placement: billboard-line-repeat;",
        @"  text-spacing: 100;",
        @"  text-background-fill: #d6322b;",
        @"  text-background-radius: 3;",
        @"  text-background-padding-x: 4;",
        @"  text-background-padding-y: 2;",
        @"  text-background-border-fill: #ffffff;",
        @"  text-background-border-width: 1.5;",
        @"}",
        @"#poi {",
        @"  shield-name: [name];",
        @"  shield-face-name: @medium;",
        @"  shield-size: 12;",
        @"  shield-fill: #1f2937;",
        @"  shield-halo-fill: #ffffff;",
        @"  shield-halo-radius: 1.5;",
        @"  shield-wrap-width: 90;",
        @"  shield-icon-fill: #ffffff;",
        @"  shield-icon-background-fill: [color];",
        @"  shield-icon-background-width: 22;",
        @"  shield-icon-background-height: 22;",
        @"  shield-icon-background-radius: 11;",
        @"  shield-icon-background-border-fill: #ffffff;",
        @"  shield-icon-background-border-width: 1.5;",
        freeSide ? @"  shield-anchors: 'right,left,top,bottom';" : @"  shield-anchors: 'right';",
        @"  shield-text-optional: true;",
        @"  shield-text-dx: 4;",
        @"  shield-text-horizontal-alignment: 'auto';",
        @"}",
        // A glyph of Massif's icon set (styles/massif/carto/icons-glyph), a distance field the style tints.
        @"#poi[icon != null] {",
        @"  shield-file: 'icons-glyph/' + [icon] + '.png';",
        @"  shield-sdf: true;",
        @"  shield-unlock-image: true;",
        @"  shield-image-scale: 0.2;",
        @"}",
        // A glyph of a font. The icon face takes ONE name, not a list.
        @"#poi[letter != null] {",
        @"  shield-icon-name: [letter];",
        @"  shield-icon-face-name: 'Arial Bold';",
        @"  shield-icon-size: 15;",
        @"  shield-placement-priority: 1;",
        @"}",
        @"#peak {",
        @"  marker-width: 7;",
        @"  marker-fill: #3f2a1d;",
        @"  marker-line-color: #ffffff;",
        @"  marker-line-width: 1.5;",
        @"}",
        @"#peak::name {",
        @"  text-name: [name];",
        @"  text-secondary-name: [ele] + ' m';",
        @"  text-secondary-scale: 0.8;",
        @"  text-face-name: @bold;",
        @"  text-size: 12;",
        @"  text-fill: #3f2a1d;",
        @"  text-placement-priority: [ele];",
        @"  text-background-fill: #fffaf0;",
        @"  text-background-opacity: 0.9;",
        @"  text-background-radius: 4;",
        @"  text-background-border-fill: #3f2a1d;",
        @"  text-background-border-width: 1;",
        callouts ? [@[
            @"  text-placement: callout;",
            @"  text-callout-offset: 22;",
            @"  text-callout-step: 18;",
            @"  text-callout-max-rows: 5;",
            @"  text-callout-line-anchor: bottom;",
            @"  text-callout-line-width: 1.5;",
        ] componentsJoinedByString:@"\n"] : @"  text-dy: -18;",
        @"}",
    ] componentsJoinedByString:@"\n"];
}

@implementation MSFLabelStylingExample {
    MSFMassifMap *_map;
    BOOL _freeSide;
    BOOL _callouts;
    int _generation;
}

+ (NSString *)exampleId {
    return @"label-styling";
}

- (void)startWithHost:(id<MSFExampleHost>)host {
    _map = host.map;
    _freeSide = YES;
    _callouts = YES;

    // The Massif streets style (styles/massif/carto, bundled by the build as styles/massif.zip) over
    // OpenFreeMap's vector tiles, cached on disk: a free service a demo would re-fetch every run.
    [_map addLayer:@"basemap"
              spec:[[[MSFSpec of:@"vector"]
                      set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
                          set:@"databasePath" value:[host cachePath:@"openfreemap.db"]]
                          set:@"capacity" value:@(100 * 1024 * 1024)]
                          set:@"source" value:[[[[MSFSpec of:@"http"]
                              set:@"url" value:@"https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf"]
                              set:@"maxZoom" value:@14]
                              set:@"HTTPHeaders" value:[[MSFSpec object] set:@"User-Agent" value:kUserAgent]]]]
                      set:@"style" value:[[MSFSpec of:@"mbvt"] set:@"project" value:[[[MSFSpec of:@"project"]
                          set:@"assets" value:massifAssets()]
                          set:@"name" value:@"streets"]]]
             error:nil];

    MSFMassifSource *data = [_map source:@"label-data"
                                    spec:[[MSFSpec of:@"geojson"] set:@"maxZoom" value:@14]
                                   error:nil];
    [data setLayerGeoJSON:[data createLayer:@"trail"] geoJson:collection(trail())];
    [data setLayerGeoJSON:[data createLayer:@"poi"] geoJson:collection(pois())];
    [data setLayerGeoJSON:[data createLayer:@"peak"] geoJson:collection(peaks())];
    [self show];

    [_map.camera moveTo:[MSFPosition positionWithLng:6.878 lat:45.9017] zoom:11.5];

    [host toggle:@"Free side" on:YES action:^(BOOL on) {
        self->_freeSide = on;
        [self show];
    }];
    [host toggle:@"Callouts" on:YES action:^(BOOL on) {
        self->_callouts = on;
        [self show];
    }];
    [host caption:@"Names take the free side of their icon, summits lift theirs onto a leader line."];
}

/** Placement is fixed when a tile is decoded, so a switch is a new layer. */
- (void)show {
    _generation++;
    [_map addLayer:[NSString stringWithFormat:@"labels.%d", _generation]
              spec:[[[MSFSpec of:@"vector"]
                      set:@"source" value:@"label-data"]
                      set:@"style" value:[[MSFSpec of:@"mbvt"] set:@"cartocss" value:[[[MSFSpec of:@"cartocss"]
                          set:@"css" value:labelStyle(_freeSide, _callouts)]
                          // Over Massif's own files, so `shield-file` finds its icon glyphs.
                          set:@"assets" value:massifAssets()]]]
             error:nil];
    if (_generation > 1) {
        [_map removeLayer:[NSString stringWithFormat:@"labels.%d", _generation - 1]];
    }
}

@end
