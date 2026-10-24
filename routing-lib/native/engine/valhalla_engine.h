#ifndef VALHALLA_ENGINE_H
#define VALHALLA_ENGINE_H

/* The C boundary of libvalhalla_routing.so, called by libmassif.so and by this library's own JNI and
 * ObjC wrappers. Only C types cross it; docs/internals/native-modules.md says why. */

#ifdef __cplusplus
extern "C" {
#endif

#define VR_ABI_VERSION 1

#if defined(__GNUC__) || defined(__clang__)
#define VR_API __attribute__((visibility("default")))
#else
#define VR_API
#endif

VR_API int vr_abi_version(void);

/* Valhalla configuration JSON, static storage. */
VR_API const char* vr_default_config(void);

/* Registers a narrative locale. The first registration of a key wins. */
VR_API void vr_add_locale(const char* key, const char* json);

/* Runs one endpoint ("route", "trace_attributes", ...) against the MBTiles files at paths.
 * Returns 0 and a malloc'd JSON string in *result, or non-zero and a malloc'd message in *error.
 * Release either with vr_free. Never throws. */
VR_API int vr_call(const char* const* paths, int path_count, const char* config_json,
                   const char* endpoint, const char* body_json, char** result, char** error);

VR_API void vr_free(char* ptr);

#ifdef __cplusplus
}
#endif

#endif
