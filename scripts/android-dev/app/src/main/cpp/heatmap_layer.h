/*
 * The heatmap custom layers of the Android and iOS demos, written against MassifApiC.h and
 * GL ES 3.0 alone. See heatmap_layer.c.
 */
#ifndef HEATMAP_LAYER_H
#define HEATMAP_LAYER_H

#include "api/MassifApiC.h"

typedef struct HeatmapLayer HeatmapLayer;

/** The three MassifApiC.h functions it needs: linked directly on iOS, looked up in libmassif.so on Android. */
typedef struct {
    mm_ctx (*context_default)(void);
    int (*create)(mm_ctx, const char*, const mm_custom_layer*, mm_handle*);
    int (*request_redraw)(mm_ctx, mm_handle);
} HeatmapApi;

/**
 * The heatmap's colour legend in screen space (scene = 0), or the heatmap itself on the ground
 * (scene = 1), registered under an id. Owned by the SDK layer: it is freed when that layer is
 * destroyed, which is when on_destroy runs (optional). NULL when it did not register.
 */
HeatmapLayer* heatmap_layer_create(const HeatmapApi* api, const char* id, int scene,
                                   void (*on_destroy)(HeatmapLayer*));

mm_handle heatmap_layer_handle(const HeatmapLayer* layer);

/** n * n positions (EPSG3857 metres, z with the mercator stretch), row by row, and values in 0..1. */
void heatmap_layer_set(HeatmapLayer* layer, const double* xyz, const float* values, int n);

/** Pulls the heatmap forward of the ground in depth, so it lies on it between its nodes. */
void heatmap_layer_set_drape(HeatmapLayer* layer, int drape);

#endif
