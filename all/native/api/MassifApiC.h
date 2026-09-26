/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_MASSIFAPIC_H_
#define _MASSIF_API_MASSIFAPIC_H_

/*
 * The facade API as a flat C ABI: int result codes and uint32 handles, no C++ types or exceptions.
 * A feature never adds a function here, only a table row; the sole exception plugs in caller code
 * (handlers, dispatchers, tile loaders). See docs/internals/api-facade.md.
 */

#include <stddef.h>
#include <stdint.h>

#ifndef MM_API
#  if defined(_WIN32)
#    define MM_API __declspec(dllexport)
#  else
#    define MM_API __attribute__((visibility("default")))
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** A registered object. 0 is never a valid one. */
typedef uint32_t mm_handle;
/** An event subscription. */
typedef uint32_t mm_subscription;
/** A queued or running async call, for mm_cancel_call. */
typedef uint32_t mm_call_id;
/** An isolated world of handles and ids. Get the process-wide one from mm_context_default. */
typedef void* mm_ctx;

#define MM_NULL_HANDLE 0u
#define MM_NULL_SUBSCRIPTION 0u
#define MM_NULL_CALL 0u

/*
 * Result codes. 0..99 mirror massif::api::Result (a static assert keeps them in step); 100 and
 * above are mistakes only a C caller can make.
 */
#define MM_OK                 0
#define MM_BAD_HANDLE         1  /* never registered, or freed and the generation moved on */
#define MM_UNKNOWN_CLASS      2
#define MM_UNKNOWN_PROPERTY   3
#define MM_READONLY           4
#define MM_UNSUPPORTED_TYPE   5
#define MM_DUPLICATE_ID       6
#define MM_NOT_TRAVERSABLE    7  /* a dotted path crossed something that is not an object */
#define MM_NULL_OBJECT        8  /* an object property on the way was not set */
#define MM_BAD_SPEC           9  /* not JSON, or not the shape the callee wanted */
#define MM_UNKNOWN_TYPE      10  /* no factory builds that "type", or no such projection */
#define MM_UNKNOWN_METHOD    11
#define MM_FAILED            12  /* it ran and could not produce a result */
#define MM_REJECTED          13  /* the SDK's own setter refused the value */

#define MM_BAD_CONTEXT      100  /* a null or unknown mm_ctx */
#define MM_BUFFER_TOO_SMALL 101  /* ask with a null buffer first, then allocate */

/**
 * Delivered on the thread the subscription asked for.
 * @return Non-zero when the handler consumed the event, stopping it reaching later handlers. Only
 *         meaningful for a subscription that asked to consume.
 */
typedef int (*mm_handler)(void* user_data, mm_handle target, const char* event, mm_handle payload);

/** How the embedder gets a call onto its own loop. See mm_set_ui_dispatcher. */
typedef void (*mm_dispatcher)(void* user_data, void (*function)(void*), void* argument);

/* --- the ABI itself ------------------------------------------------------------------------ */

/**
 * The ABI version. Incremented when an existing signature or result code changes meaning, never
 * for an addition. A binding built against an older one keeps working until this changes.
 */
MM_API int mm_abi_version(void);

/**
 * The name of a result code, e.g. "MM_UNKNOWN_PROPERTY", or "MM_UNKNOWN" for one from a newer
 * build. Static storage; do not free.
 */
MM_API const char* mm_result_name(int result);

/**
 * The process-wide context. Never null. Cache it; every other call takes it.
 */
MM_API mm_ctx mm_context_default(void);

/* --- create / destroy ---------------------------------------------------------------------- */

/**
 * Builds an object from a JSON spec and registers it under a kind and id. An existing id with an
 * identical spec returns its handle; a different spec is MM_DUPLICATE_ID. Keys the factory does not
 * take are applied as properties, unknown ones dropped with a warning.
 */
MM_API int mm_create(mm_ctx ctx, const char* kind, const char* id, const char* json,
                     mm_handle* out);

/**
 * Drops an id and, with it, the context's reference to the object. Handles held elsewhere go
 * stale rather than dangling; subscriptions and pending async calls on it are dropped.
 */
MM_API int mm_destroy(mm_ctx ctx, const char* kind, const char* id);

/**
 * The same, addressed by handle - what a caller holding a call result has.
 */
MM_API int mm_destroy_handle(mm_ctx ctx, mm_handle handle);

/**
 * Looks up a handle by kind and id. MM_BAD_HANDLE when nothing is registered under it.
 */
MM_API int mm_find(mm_ctx ctx, const char* kind, const char* id, mm_handle* out);

/**
 * Whether a handle still resolves: MM_OK or MM_BAD_HANDLE, without a property read that can fail
 * for another reason.
 */
MM_API int mm_valid(mm_ctx ctx, mm_handle handle);

/* --- set / get ----------------------------------------------------------------------------- */

/*
 * A path walks object properties ("fogOptions.rangeStart"), JSON values ("feature.properties.tags.1")
 * and bag keys ("params.water_color"); a bag also takes a JSON object. Types coerce both ways: a bool
 * reads as 1 or 0, a double writes to a bool as its truthiness.
 */

MM_API int mm_set_bool(mm_ctx ctx, mm_handle handle, const char* path, int value);
MM_API int mm_set_long(mm_ctx ctx, mm_handle handle, const char* path, int64_t value);
MM_API int mm_set_double(mm_ctx ctx, mm_handle handle, const char* path, double value);
/** Also how a struct is written: a position is "[x,y]" or "[x,y,z]", a range "[min,max]". */
MM_API int mm_set_string(mm_ctx ctx, mm_handle handle, const char* path, const char* value);

/**
 * Writes several properties in one crossing, from a JSON object of path to value. Every key is
 * attempted and the first failure is returned.
 * @param projection Applies to the positions among them, as in mm_set_position.
 */
MM_API int mm_set_json(mm_ctx ctx, mm_handle handle, const char* json, const char* projection);

/**
 * Writes a position, or bounds, from doubles - the write counterpart of mm_get_position.
 * @param projection A well-known name the values are in, e.g. "EPSG:3857". Null or empty behaves
 *                   like mm_set_string.
 * @param count 2 or 3 numbers are a position; 4 or 6 are BOUNDS, as a pair of positions.
 */
MM_API int mm_set_position(mm_ctx ctx, mm_handle handle, const char* path, const char* projection,
                           const double* values, size_t count);

/**
 * Points an object property at another registered object - a layer's data source, a decoder's
 * style. MM_NULL_HANDLE clears it; a value of the wrong class is MM_UNKNOWN_CLASS.
 */
MM_API int mm_set_object(mm_ctx ctx, mm_handle handle, const char* path, mm_handle value);

/**
 * The object an object property points at, as a handle the caller owns - the read counterpart of
 * mm_set_object, and how a child is shared rather than built twice.
 * @return 0 when the path does not resolve, is not an object property, or is null.
 */
MM_API mm_handle mm_get_object(mm_ctx ctx, mm_handle handle, const char* path);

MM_API int mm_get_bool(mm_ctx ctx, mm_handle handle, const char* path, int* value);
MM_API int mm_get_long(mm_ctx ctx, mm_handle handle, const char* path, int64_t* value);
MM_API int mm_get_double(mm_ctx ctx, mm_handle handle, const char* path, double* value);

/**
 * Reads a string, a struct as JSON, or a JSON subtree. Two-call protocol: pass a null buffer to
 * learn the size, then allocate; `needed` includes the terminating NUL and is always set on MM_OK
 * and MM_BUFFER_TOO_SMALL.
 * @param projection A well-known name, e.g. "EPSG:3857", to read a position in. Null or empty
 *                   means the projection the running event handler asked for, and WGS84 when there
 *                   is none. Ignored for anything that is not a coordinate.
 */
MM_API int mm_get_string(mm_ctx ctx, mm_handle handle, const char* path, const char* projection,
                         char* buffer, size_t size, size_t* needed);

/**
 * Reads a position, or any small array of numbers, straight into doubles - no JSON to parse per
 * event. No two-call protocol: a position is at most 3 doubles and bounds 6, so pass a fixed buffer.
 * @param projection A well-known name, e.g. "EPSG:3857". Null or empty means the projection the
 *                   running event handler asked for, and WGS84 when there is none.
 * @param out Filled with up to `count` numbers. May be null to ask only how many there are.
 * @param count The capacity of `out`.
 * @param needed Set to how many numbers the value has, whether or not they fit.
 * @return MM_OK, MM_BUFFER_TOO_SMALL when there are more than `count`, or the read's own error.
 *         MM_UNSUPPORTED_TYPE when the value is not an array of numbers.
 */
MM_API int mm_get_position(mm_ctx ctx, mm_handle handle, const char* path, const char* projection,
                           double* out, size_t count, size_t* needed);

/* --- binary and bulk ----------------------------------------------------------------------- */

/* Neither of these becomes a string: a tile is a blob, an elevation profile thousands of numbers. */

/**
 * The size in bytes of a binary property, e.g. "data" on a tile. An empty path means the handle
 * is the blob itself.
 */
MM_API int mm_data_size(mm_ctx ctx, mm_handle handle, const char* path, size_t* size);

/**
 * Copies a binary property into the caller's buffer. MM_BUFFER_TOO_SMALL when it does not fit,
 * with `copied` set to what was needed.
 */
MM_API int mm_data_copy(mm_ctx ctx, mm_handle handle, const char* path, void* buffer, size_t size,
                        size_t* copied);

/** How many numbers a bulk numeric result holds. */
MM_API int mm_doubles_count(mm_ctx ctx, mm_handle handle, size_t* count);

/** Copies a bulk numeric result, flat, in one crossing. */
MM_API int mm_doubles_copy(mm_ctx ctx, mm_handle handle, double* buffer, size_t count,
                           size_t* copied);

/* --- call ---------------------------------------------------------------------------------- */

/**
 * Runs a method on an object. The result is always a handle the caller owns - pass it to
 * mm_destroy_handle. An object result is that object; anything else is a JSON document read with
 * an empty path.
 * @param args_json The arguments as a JSON array, e.g. "[[8467,5852,14]]". Null for none.
 */
MM_API int mm_call(mm_ctx ctx, mm_handle handle, const char* method, const char* args_json,
                   mm_handle* result);

/**
 * The same, on a worker thread, with the result delivered as `event` on the object (subscribe
 * first). A payload of 0 means failure; it is freed once the handlers have run. The handle, method
 * and argument JSON are checked before anything is queued.
 */
MM_API int mm_call_async(mm_ctx ctx, mm_handle handle, const char* method, const char* args_json,
                         const char* event, mm_call_id* call);

/**
 * Cancels a queued or running async call: it will not start and no event fires, but a call in
 * flight is not aborted - it finishes and its result is dropped.
 * @return MM_OK when it was queued or running, MM_BAD_HANDLE when it had already finished.
 */
MM_API int mm_cancel_call(mm_ctx ctx, mm_call_id call);

/** Cancels every queued or running call on an object. @param count Set to how many. Optional. */
MM_API int mm_cancel_calls(mm_ctx ctx, mm_handle handle, int* count);

/* --- custom sources ------------------------------------------------------------------------ */

/** The tile is an encoded file - PNG, JPEG, WEBP, or a vector tile's protobuf. */
#define MM_TILE_ENCODED 0
/**
 * The tile is width * height * 4 bytes of premultiplied RGBA, already decoded - no PNG round trip.
 */
#define MM_TILE_RGBA8   1

/**
 * Hands one tile's bytes to the SDK. Call it from inside an mm_tile_loader, at most once: the SDK
 * copies during the call, so a stack buffer is fine. Never calling it means "no such tile".
 * @param sink_data The pointer handed to the loader. Not yours to interpret.
 * @param format MM_TILE_ENCODED or MM_TILE_RGBA8.
 * @param width, height Pixel dimensions. Required for MM_TILE_RGBA8, ignored otherwise.
 */
typedef void (*mm_tile_sink)(void* sink_data, const void* data, size_t size,
                             int format, int width, int height);

/**
 * Produces one tile of the SDK's grid. Called on several tile threads at once: it must be
 * thread-safe.
 * @param zoom, x, y The tile, in the usual XYZ scheme with y running south.
 * @param sink Call it with the bytes; skip it for a tile that does not exist.
 * @return MM_OK, or MM_FAILED when the tile should have existed and could not be produced.
 */
typedef int (*mm_tile_loader)(void* user_data, int zoom, int x, int y,
                              mm_tile_sink sink, void* sink_data);

/** What mm_source_create_custom needs to know. */
typedef struct {
    int min_zoom;
    int max_zoom;
    mm_tile_loader load_tile;
    /** Called once when the source is destroyed, on whichever thread dropped it. Optional. */
    void (*destroy)(void* user_data);
    void* user_data;
} mm_tile_source;

/**
 * Registers a tile source the caller implements, under an id, so a layer spec can name it: encoded
 * images for a "raster" layer, vector tile protobuf for a "vector" one. On anything but MM_OK nothing
 * was taken: `destroy` is not called and user_data stays the caller's.
 * @param id The source id. Must be free, as for any other create.
 * @param out The handle. Optional.
 * @return MM_OK, MM_DUPLICATE_ID, or MM_BAD_SPEC when load_tile is null.
 */
MM_API int mm_source_create_custom(mm_ctx ctx, const char* id, const mm_tile_source* source,
                                   mm_handle* out);

/**
 * Tells the layers reading a source that its tiles changed, so they reload - e.g. a custom source
 * whose underlying file was replaced.
 * @param remove_tiles Non-zero to drop the cached tiles first, rather than replacing each as its
 *        reload finishes. Non-zero flashes; zero can show stale tiles for a moment.
 */
MM_API int mm_source_notify_changed(mm_ctx ctx, mm_handle handle, int remove_tiles);

/* --- events -------------------------------------------------------------------------------- */

/**
 * Subscribes to an event on an object. Handlers run in registration order.
 * @param opts_json A JSON object of options, or null for the defaults:
 *          {"delivery":"origin"|"ui"|"background",   where the handler runs, default "origin"
 *           "consume":true,      its return value can stop the event; requires "origin"
 *           "coalesce":true,     replace a pending event rather than queueing a second
 *           "throttle":100,      drop events arriving within this many ms of the last delivered
 *           "projection":"EPSG:4326"}   what its position reads default to, for this call only
 */
MM_API int mm_on(mm_ctx ctx, mm_handle handle, const char* event, mm_handler handler,
                 void* user_data, const char* opts_json, mm_subscription* out);

/** Removes one subscription. */
MM_API int mm_off(mm_ctx ctx, mm_subscription subscription);

/** Removes every handler of one event on one object. @param count Set to how many. Optional. */
MM_API int mm_off_event(mm_ctx ctx, mm_handle handle, const char* event, int* count);

/** Removes every handler on one object. @param count Set to how many. Optional. */
MM_API int mm_off_all(mm_ctx ctx, mm_handle handle, int* count);

/**
 * Registers how to reach the embedder's loop, for "ui" delivery. The dispatcher is called on the
 * producing thread and must run `function(argument)` (that is mm_drain) on the loop; without one,
 * "ui" subscriptions run inline.
 */
MM_API int mm_set_ui_dispatcher(mm_ctx ctx, mm_dispatcher dispatcher, void* user_data);

/**
 * Runs the handlers waiting for this thread. Called by whatever the dispatcher posted.
 * @param count Set to how many were delivered. Optional.
 */
MM_API int mm_drain(mm_ctx ctx, int* count);

#ifdef __cplusplus
}
#endif

#endif
