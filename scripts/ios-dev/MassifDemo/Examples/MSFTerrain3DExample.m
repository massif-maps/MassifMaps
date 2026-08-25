#import "MSFExample.h"
#import "MassifMaps.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifMap.h"
#import "api/MSFMassifObject.h"

/**
 * The flagship: satellite imagery draped over 3D terrain, with roads and summits on top.
 *
 * The Objective-C twin of the Android example with the same id - see
 * scripts/android-dev/.../examples/terrain/Terrain3DExample.java.
 */
@interface MSFTerrain3DExample : NSObject <MSFExample>
@end

@implementation MSFTerrain3DExample

+ (NSString *)exampleId {
    return @"terrain-3d";
}

/** Identifies the app to the tile servers, which both of them ask for. */
static NSString * const kUserAgent =
    @"MassifMapsExamples/1.0 (+https://github.com/massif-maps/MassifMaps)";

/** Open DEM tiles, terrarium-encoded, cached on disk in front of the server. */
static MSFSpec *dem(id<MSFExampleHost> host) {
    return [[[[MSFSpec of:@"persistent-cache"]
        set:@"databasePath" value:[host cachePath:@"mapterhorn-dem.db"]]
        set:@"capacity" value:@(200 * 1024 * 1024)]
        set:@"source" value:[[[[[MSFSpec of:@"http"]
            set:@"url" value:@"https://tiles.mapterhorn.com/{z}/{x}/{y}.webp"]
            set:@"minZoom" value:@1]
            set:@"maxZoom" value:@16]
            // What picks the elevation decoder, per TILE: the source stamps its meta data on
            // every tile it loads. Without it the SDK assumes mapbox encoding, and mapbox-decoding
            // terrarium tiles gives heights in the hundreds of kilometres - the terrain inflates
            // and the camera ends up inside it.
            //
            // Written as the whole map, not as a "metaData.dem_encoding" path: a NESTED spec is
            // applied by applySpecProperties, which matches a property by its full name and has
            // no indexed-path handling, so a dotted key there is dropped.
            //
            // It stays on the HTTP source, not on the cache in front of it: a wrapper source with
            // no map of its own answers with its wrapped source's.
            set:@"metaData" value:[[MSFSpec object] set:@"dem_encoding" value:@"terrarium"]]];
}

- (void)startWithHost:(id<MSFExampleHost>)host {
    MSFMassifMap *map = host.map;

    // Imagery underneath. The {y}/{x} order is this server's, and the template substitutes by
    // name, so any order works.
    [map addLayer:@"satellite"
             spec:[[MSFSpec of:@"raster"]
                     // Cached on disk in front of the server: imagery tiles are big, and a demo
                     // that gets panned around re-fetches the same ones on every run.
                     set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
                         set:@"databasePath" value:[host cachePath:@"world-imagery.db"]]
                         set:@"capacity" value:@(200 * 1024 * 1024)]
                         set:@"source" value:[[[[MSFSpec of:@"http"]
                             set:@"url" value:@"https://server.arcgisonline.com/ArcGIS/rest/services/"
                                               @"World_Imagery/MapServer/tile/{z}/{y}/{x}"]
                             set:@"maxZoom" value:@18]
                             set:@"HTTPHeaders" value:[[MSFSpec object] set:@"User-Agent" value:kUserAgent]]]]
            error:nil];

    // Roads, place names and summits ON TOP, from a style project with no background of its own -
    // see scripts/android-dev/app/src/main/style-projects/hybrid.
    [map style:@"hybrid"
          spec:[[MSFSpec of:@"mbvt"]
                  set:@"project" value:[[MSFSpec of:@"project"]
                      set:@"assets" value:[[MSFSpec of:@"zip"]
                          set:@"data" value:[[MSFSpec of:@"url"]
                              set:@"url" value:@"assets://styles/hybrid.zip"]]]]
         error:nil];
    [map addLayer:@"labels"
             spec:[[[MSFSpec of:@"vector"]
                     set:@"source" value:[[[[MSFSpec of:@"persistent-cache"]
                         set:@"databasePath" value:[host cachePath:@"openfreemap.db"]]
                         set:@"capacity" value:@(100 * 1024 * 1024)]
                         set:@"source" value:[[[[MSFSpec of:@"http"]
                             set:@"url" value:@"https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf"]
                             set:@"maxZoom" value:@14]
                             set:@"HTTPHeaders" value:[[MSFSpec object] set:@"User-Agent" value:kUserAgent]]]]
                     set:@"style" value:@"hybrid"]
            error:nil];

    MSFPropertyGroup *terrain =
        [map terrainWithSpec:[[MSFSpec of:@"terrain"] set:@"source" value:dem(host)] error:nil];
    // apply, not three sets: one crossing for the whole group.
    //
    // viewDistanceFactor is how far the ground goes on, in multiples of the camera-to-focus
    // distance, so one value holds at every zoom - pair a short one with fog or the ground ends on
    // a hard edge. cameraClearance is normally 200 m, which swings a close view into the nearest
    // hillside; lowered so the camera can sit among the peaks.
    [terrain apply:[[[[MSFSpec object]
        set:@"exaggeration" value:@1.25]
        set:@"viewDistanceFactor" value:@1.6]
        set:@"cameraClearance" value:@40]];

    // Options starts with these EMPTY, so they are BUILT here rather than written through.
    [map skyWithSpec:[MSFSpec of:@"sky"] error:nil];
    [map fogWithSpec:[[[MSFSpec of:@"fog"] set:@"rangeStart" value:@2.2]
                       set:@"rangeEnd" value:@8] error:nil];
    // The sun has to come from BEHIND the camera, or the face being looked at is the one in shadow:
    // this view is of the north side, so the light is north-west. Mid altitude, because a low sun
    // here puts the whole massif in its own shadow.
    [map lightWithSpec:[[[[[[MSFSpec of:@"light"]
        set:@"terrainLightingEnabled" value:@YES]
        set:@"sunAzimuth" value:@315]
        set:@"sunAltitude" value:@42]
        set:@"shadowStrength" value:@0.35]
        set:@"shadowSoftness" value:@1.5] error:nil];

    // Looking SOUTH at the Matterhorn from high over Zermatt. tilt 90 is straight down in this SDK,
    // so a landscape view is a LOW tilt.
    [map.camera moveTo:[MSFPosition positionWithLng:7.6586 lat:45.9763]
                  zoom:11.5 rotation:180 tilt:33];

    [host toggle:@"Terrain" on:YES action:^(BOOL on) {
        // A path off the map itself, with the readable spelling: "terrain" is an alias for
        // "terrainOptions", so this is map.options set:@"terrainOptions.enabled".
        [map set:@"terrain.enabled" value:@(on)];
    }];
    [host toggle:@"Labels" on:YES action:^(BOOL on) {
        [[map layer:@"labels"] visible:on];
    }];
    [host button:@"Exaggerate" action:^{
        double current = [map.terrain getDouble:@"exaggeration" defaultValue:1];
        [map.terrain set:@"exaggeration" value:@(current >= 2 ? 1.0 : current + 0.35)];
    }];
    [host caption:@"The Matterhorn. Imagery on the mesh, roads and summits above it."];
}

@end
