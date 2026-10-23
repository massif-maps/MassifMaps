#import "MSFCustomHeatmap.h"
#import "api/MSFMassif.h"
#import "api/MSFMassifObject.h"

#include "heatmap_layer.h"

static const int kN = 160;
static const double kSideMeters = 8000;
static const double kEarthRadius = 6378137.0;

static HeatmapLayer *sScene;

static void forget(HeatmapLayer *layer) {
    if (sScene == layer) {
        sScene = NULL;
    }
}

@implementation MSFCustomHeatmap

+ (int)gridSize {
    return kN;
}

+ (nullable MSFMassifLayer *)createWithId:(NSString *)layerId scene:(BOOL)scene {
    // Linked, not looked up: the SDK is a static library in this app.
    HeatmapApi api = { mm_context_default, mm_layer_create_custom, mm_layer_request_redraw };
    HeatmapLayer *layer = heatmap_layer_create(&api, layerId.UTF8String, scene, forget);
    if (!layer) {
        return nil;
    }
    if (scene) {
        sScene = layer;
    }
    return [MSFMassif layer:layerId];
}

+ (NSData *)gridPositionsForLongitude:(double)lon latitude:(double)lat {
    double cosLat = cos(lat * M_PI / 180);
    double step = kSideMeters / (kN - 1);
    NSMutableData *data = [NSMutableData dataWithLength:sizeof(double) * 2 * kN * kN];
    double *positions = data.mutableBytes;
    for (int j = 0; j < kN; j++) {
        for (int i = 0; i < kN; i++) {
            double east = (i - (kN - 1) / 2.0) * step, north = (j - (kN - 1) / 2.0) * step;
            positions[2 * (j * kN + i)] = lon + east / (111320 * cosLat);
            positions[2 * (j * kN + i) + 1] = lat + north / 110540;
        }
    }
    return data;
}

+ (void)setHeatmapAtLongitude:(double)lon latitude:(double)lat heights:(nullable NSData *)heights {
    if (!sScene) {
        return;
    }
    // Mercator metres whatever the base projection: the SDK's world units are that plane.
    double cosLat = cos(lat * M_PI / 180);
    NSData *positionData = [self gridPositionsForLongitude:lon latitude:lat];
    const double *positions = positionData.bytes;
    const double *ground = heights.length >= sizeof(double) * kN * kN ? heights.bytes : NULL;
    static const double spots[3][4] = { { 0, 0, 0.35, 1.0 }, { 0.5, -0.4, 0.2, 0.8 }, { -0.55, 0.45, 0.25, 0.7 } };
    double *xyz = malloc(sizeof(double) * 3 * kN * kN);
    float *values = malloc(sizeof(float) * kN * kN);
    for (int k = 0; k < kN * kN; k++) {
        double height = ground && !isnan(ground[k]) && ground[k] > -100000 ? ground[k] : 0;
        double sinLat = sin(positions[2 * k + 1] * M_PI / 180);
        xyz[3 * k] = positions[2 * k] * M_PI / 180 * kEarthRadius;
        xyz[3 * k + 1] = 0.5 * kEarthRadius * log((1 + sinLat) / (1 - sinLat));
        // 20 m of clearance over the mesh's chords, then the mercator stretch heights carry.
        xyz[3 * k + 2] = (height + 20) / cosLat;
        double u = (k % kN - (kN - 1) / 2.0) / ((kN - 1) / 2.0), v = (k / kN - (kN - 1) / 2.0) / ((kN - 1) / 2.0), heat = 0;
        for (int s = 0; s < 3; s++) {
            double du = u - spots[s][0], dv = v - spots[s][1];
            heat += spots[s][3] * exp(-(du * du + dv * dv) / (2 * spots[s][2] * spots[s][2]));
        }
        values[k] = (float) fmin(1, heat);
    }
    heatmap_layer_set(sScene, xyz, values, kN);
    free(xyz);
    free(values);
}

+ (void)setDrape:(BOOL)drape {
    if (sScene) {
        heatmap_layer_set_drape(sScene, drape);
    }
}

@end
