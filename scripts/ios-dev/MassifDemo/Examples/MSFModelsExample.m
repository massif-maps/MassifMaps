#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * glTF models placed at points by a CartoCSS `model` rule: Massif's own trees, and a wind turbine
 * whose rotor spins - the way an app adds models of its own.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/styles/ModelsExample.java.
 */
@interface MSFModelsExample : NSObject <MSFExample>
@end

@implementation MSFModelsExample

+ (NSString *)exampleId {
    return @"3d-models";
}

static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

/** The models Massif ships (styles/massif/models), read straight from the repository. */
#define MODELS @"https://raw.githubusercontent.com/massif-maps/MassifMaps/master/styles/massif/models/"

/** The trees of Grenoble's Jardin de Ville, from OpenStreetMap (natural=tree). */
static NSString * const kTrees =
    @"{\"type\":\"Feature\",\"properties\":{},\"geometry\":{\"type\":\"MultiPoint\",\"coordinates\":["
    @"[5.72924,45.19367],[5.72935,45.19370],[5.72937,45.19366],[5.72985,45.19367],[5.72698,45.19293],[5.72688,45.19293],"
    @"[5.72797,45.19306],[5.72786,45.19304],[5.72771,45.19271],[5.72909,45.19364],[5.72911,45.19272],[5.72685,45.19239],"
    @"[5.72692,45.19224],[5.72710,45.19240],[5.72709,45.19226],[5.72781,45.19245],[5.72788,45.19247],[5.72691,45.19200],"
    @"[5.72714,45.19220],[5.72717,45.19214],[5.72710,45.19207],[5.72706,45.19200],[5.72717,45.19207],[5.72684,45.19175],"
    @"[5.72694,45.19173],[5.72697,45.19195],[5.72699,45.19189],[5.72722,45.19189],[5.72705,45.19181],[5.72712,45.19175],"
    @"[5.72717,45.19178],[5.72723,45.19182],[5.72725,45.19176],[5.72683,45.19165],[5.72681,45.19153],[5.72700,45.19169],"
    @"[5.72718,45.19168],[5.72705,45.19155],[5.72753,45.19172],[5.72989,45.19364],[5.72992,45.19361],[5.72989,45.19258],"
    @"[5.73045,45.19224],[5.72996,45.19179],[5.72997,45.19179],[5.72998,45.19179],[5.72993,45.19178],[5.72993,45.19176],"
    @"[5.72993,45.19175],[5.72993,45.19174],[5.73048,45.19214],[5.73103,45.19252],[5.73100,45.19249],[5.73119,45.19265],"
    @"[5.73115,45.19261],[5.73109,45.19257],[5.73103,45.19243],[5.73109,45.19240],[5.73111,45.19239],[5.73117,45.19236],"
    @"[5.73110,45.19181],[5.72992,45.19170],[5.72992,45.19169],[5.72992,45.19167],[5.72992,45.19166],[5.72992,45.19164],"
    @"[5.72991,45.19163],[5.72991,45.19161],[5.73000,45.19163],[5.72987,45.19149],[5.72986,45.19148],[5.73003,45.19153],"
    @"[5.73001,45.19146],[5.73009,45.19154],[5.73018,45.19154],[5.73021,45.19152],[5.73068,45.19167],[5.73068,45.19160],"
    @"[5.73065,45.19152],[5.73012,45.19146],[5.73006,45.19144],[5.73016,45.19144],[5.73020,45.19143]"
    @"]}}";

/** Three turbines along the Isere quay, where no real wind farm stands. */
static NSString * const kTurbines =
    @"{\"type\":\"Feature\",\"properties\":{},\"geometry\":{\"type\":\"MultiPoint\",\"coordinates\":["
    @"[5.7232,45.1938],[5.7262,45.1939],[5.7292,45.1941]]}}";

// random() is seeded by each point's position: every tree keeps its own model, size, heading and
// tint through every zoom, and the MultiPoint's trees differ from each other. A spinning part is a
// model of its own, its origin on the axis, lifted onto the tower.
static NSString * const kStyle =
    @"#trees {\n"
    @"  model-file: step(random(0, 3), (0, url('" MODELS @"tree-spreading.glb')),\n"
    @"    (1, url('" MODELS @"tree-layered.glb')), (2, url('" MODELS @"tree-clumped.glb')));\n"
    @"  model-scale: random(0.8, 1.2);\n"
    @"  model-rotation: random(0, 360);\n"
    @"  model-color: #b4dcb4;\n"
    @"  model-color-mix: random(0, 0.3);\n"
    @"  model-max-distance: 1200;\n"
    @"  model-shadow-max-distance: 500;\n"
    @"}\n"
    @"#turbines::tower {\n"
    @"  model-file: url('" MODELS @"wind-turbine-tower.glb');\n"
    @"  model-max-distance: 5000;\n"
    @"  model-shadow-max-distance: 1500;\n"
    @"  model-rotation: 270;\n"
    @"}\n"
    @"#turbines::rotor {\n"
    @"  model-file: url('" MODELS @"wind-turbine-rotor.glb');\n"
    @"  model-max-distance: 5000;\n"
    @"  model-shadow-max-distance: 1500;\n"
    @"  model-rotation: 270;\n"
    @"  model-z-offset: 81.4;\n"
    @"  model-spin: 14;\n"
    @"  model-spin-axis: y;\n"
    @"}";

- (void)startWithHost:(id<MSFExampleHost>)host {
    MSFMassifMap *map = host.map;

    // Massif's own trees and turbines: models_3d draws them at the tiles' poi points of class
    // tree and wind_turbine. Their .glb files are NOT in the style package - download the five
    // from https://github.com/massif-maps/MassifMaps/tree/master/styles/massif/models
    // and put them in a models/ folder at the package root, beside project.json. OpenFreeMap's
    // tiles carry no such points anyway, so the models below are drawn on the app's own.
    [map style:@"massif"
          spec:[[[MSFSpec of:@"mbvt"]
                  set:@"project" value:[[[MSFSpec of:@"project"]
                      set:@"assets" value:[[MSFSpec of:@"zip"]
                          set:@"data" value:[[MSFSpec of:@"url"] set:@"url" value:@"assets://styles/massif.zip"]]]
                      set:@"name" value:@"streets"]]
                  set:@"params" value:[[MSFSpec object] set:@"models_3d" value:@"1"]]
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

    MSFMassifSource *points = [map source:@"model-points"
                                     spec:[[MSFSpec of:@"geojson"] set:@"maxZoom" value:@14]
                                    error:nil];
    [points setLayerGeoJSON:[points createLayer:@"trees"] geoJson:kTrees];
    [points setLayerGeoJSON:[points createLayer:@"turbines"] geoJson:kTurbines];
    [map addLayer:@"models"
             spec:[[[MSFSpec of:@"vector"]
                     set:@"source" value:@"model-points"]
                     set:@"style" value:[[MSFSpec of:@"mbvt"]
                         set:@"cartocss" value:[[MSFSpec of:@"cartocss"] set:@"css" value:kStyle]]]
            error:nil];

    // A terrain and a lit ground, for the shadows: cast shadows land on the terrain surface.
    [map terrainWithSpec:[[MSFSpec of:@"terrain"]
        set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
            set:@"databasePath" value:[host cachePath:@"mapterhorn-dem.db"]]
            set:@"capacity" value:@(200 * 1024 * 1024)]
            set:@"source" value:[[[[[MSFSpec of:@"http"]
                set:@"url" value:@"https://tiles.mapterhorn.com/{z}/{x}/{y}.webp"]
                set:@"minZoom" value:@1]
                set:@"maxZoom" value:@16]
                set:@"metaData" value:[[MSFSpec object] set:@"dem_encoding" value:@"terrarium"]]]] error:nil];
    [map lightWithSpec:[[[[[[MSFSpec of:@"light"]
        set:@"sunOverridingStyle" value:@YES]
        set:@"sunAzimuth" value:@220]
        set:@"sunAltitude" value:@40]
        set:@"terrainLightingEnabled" value:@YES]
        set:@"shadowStrength" value:@1.5] error:nil];

    [map.camera moveTo:[MSFPosition positionWithLng:5.7262 lat:45.1928] zoom:17.0 rotation:0 tilt:32];

    [host toggle:@"Spin" on:YES action:^(BOOL on) {
        // Off, the rotors stand still and the map stops redrawing every frame for them.
        [map set:@"modelAnimationsEnabled" value:@(on)];
    }];
    [host toggle:@"Model shadows" on:YES action:^(BOOL on) {
        // On a slow GPU the trees' shadows can cost more than the trees themselves.
        [map set:@"modelShadowsEnabled" value:@(on)];
    }];
    [host caption:@"Every tree draws its own model, size and heading; the rotors spin."];
}

@end
