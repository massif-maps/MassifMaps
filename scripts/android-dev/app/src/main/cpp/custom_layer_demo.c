/* The JNI end of heatmap_layer.c: com.massifmaps.MassifDemo.customlayer.CustomHeatmap. */
#include "heatmap_layer.h"

#include <dlfcn.h>
#include <jni.h>

static HeatmapLayer* g_scene;

static void forget(HeatmapLayer* layer) {
    if (g_scene == layer) {
        g_scene = NULL;
    }
}

JNIEXPORT jint JNICALL Java_com_massifmaps_MassifDemo_customlayer_CustomHeatmap_nativeCreate(
        JNIEnv* env, jclass clazz, jstring jid, jboolean scene) {
    void* lib = dlopen("libmassif.so", RTLD_NOW | RTLD_NOLOAD);
    if (!lib) {
        return 0;
    }
    HeatmapApi api;
    api.context_default = (mm_ctx (*)(void)) dlsym(lib, "mm_context_default");
    api.create = (int (*)(mm_ctx, const char*, const mm_custom_layer*, mm_handle*)) dlsym(lib, "mm_layer_create_custom");
    api.request_redraw = (int (*)(mm_ctx, mm_handle)) dlsym(lib, "mm_layer_request_redraw");
    if (!api.context_default || !api.create || !api.request_redraw) {
        return 0;
    }
    const char* id = (*env)->GetStringUTFChars(env, jid, NULL);
    HeatmapLayer* layer = heatmap_layer_create(&api, id, scene, forget);
    (*env)->ReleaseStringUTFChars(env, jid, id);
    if (!layer) {
        return 0;
    }
    if (scene) {
        g_scene = layer;
    }
    return (jint) heatmap_layer_handle(layer);
}

JNIEXPORT void JNICALL Java_com_massifmaps_MassifDemo_customlayer_CustomHeatmap_nativeSetHeatmap(
        JNIEnv* env, jclass clazz, jdoubleArray xyz, jfloatArray values, jint n) {
    if (!g_scene || n < 2 || n > 255) {
        return;
    }
    jdouble* positions = (*env)->GetDoubleArrayElements(env, xyz, NULL);
    jfloat* heat = (*env)->GetFloatArrayElements(env, values, NULL);
    heatmap_layer_set(g_scene, positions, heat, n);
    (*env)->ReleaseFloatArrayElements(env, values, heat, JNI_ABORT);
    (*env)->ReleaseDoubleArrayElements(env, xyz, positions, JNI_ABORT);
}

JNIEXPORT void JNICALL Java_com_massifmaps_MassifDemo_customlayer_CustomHeatmap_nativeSetDrape(
        JNIEnv* env, jclass clazz, jboolean drape) {
    if (g_scene) {
        heatmap_layer_set_drape(g_scene, drape);
    }
}
