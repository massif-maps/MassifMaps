/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_MASSIFAPIC_H_
#define _MASSIF_API_MASSIFAPIC_H_

/*
 * The facade API as a flat C ABI.
 *
 * This is the surface NativeScript, React Native, a WASM build and any other language with an FFI
 * bind to: no C++ types, no exceptions, no ownership rules beyond the ones stated here. Everything
 * is an int result code and a uint32 handle - which is also a JavaScript number, so nothing needs
 * BigInt.
 *
 * The invariant this exists to keep: ADDING A FEATURE NEVER ADDS A FUNCTION HERE. A new source
 * type is a spec factory, a new option is a row in the generated property table, a new event is a
 * bridge, a new method is a table row. The count below grows with TYPES, not with features.
 *
 * See docs/internals/api-facade.md.
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
 * Result codes. 0..99 mirror massif::api::Result one for one - a static assert in the
 * implementation keeps them in step. 100 and above exist only here, for mistakes only a C caller
 * can make.
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
 * build. Static storage; do not free. So a binding can log something readable without keeping its
 * own copy of the table.
 */
MM_API const char* mm_result_name(int result);

/**
 * The process-wide context. Never null. Cache it; every other call takes it.
 */
MM_API mm_ctx mm_context_default(void);

/* --- create / destroy ---------------------------------------------------------------------- */

/**
 * Builds an object from a JSON spec and registers it under a kind and id.
 *
 * An IDENTICAL spec under an existing id returns the existing handle, so two maps can share one
 * source without coordinating; a different spec under the same id is MM_DUPLICATE_ID. Keys the
 * factory does not need are applied as properties, and a key the SDK does not know is dropped with
 * a warning rather than failing the whole spec.
 */
MM_API int mm_create(mm_ctx ctx, const char* kind, const char* id, const char* json,
                     mm_handle* out);

/**
 * Drops an id and, with it, the context's reference to the object. Handles held elsewhere go
 * stale rather than dangling; subscriptions and pending async calls on it are dropped.
 */
MM_API int mm_destroy(mm_ctx ctx, const char* kind, const char* id);

/**
 * The same, addressed by handle - which is what a caller holding a call result has, since a result
 * has no id the app chose.
 */
MM_API int mm_destroy_handle(mm_ctx ctx, mm_handle handle);

/**
 * Looks up a handle by kind and id. MM_BAD_HANDLE when nothing is registered under it.
 */
MM_API int mm_find(mm_ctx ctx, const char* kind, const char* id, mm_handle* out);

/**
 * Whether a handle still resolves. MM_OK or MM_BAD_HANDLE.
 *
 * A binding needs this to tell "destroyed" from "never existed" without a property read, which
 * can legitimately fail for another reason.
 */
MM_API int mm_valid(mm_ctx ctx, mm_handle handle);

/* --- set / get ----------------------------------------------------------------------------- */

/*
 * The path may walk object properties: "fogOptions.rangeStart", and it may continue INSIDE a
 * JSON value: "feature.properties.name", or index an array: "feature.properties.tags.1".
 *
 * Types coerce both ways - a bool read as a double is 1 or 0, a double written to a bool is its
 * truthiness - so a binding with one numeric type does not need to know which it is dealing with.
 */

MM_API int mm_set_bool(mm_ctx ctx, mm_handle handle, const char* path, int value);
MM_API int mm_set_long(mm_ctx ctx, mm_handle handle, const char* path, int64_t value);
MM_API int mm_set_double(mm_ctx ctx, mm_handle handle, const char* path, double value);
/** Also how a struct is written: a position is "[x,y]" or "[x,y,z]", a range "[min,max]". */
MM_API int mm_set_string(mm_ctx ctx, mm_handle handle, const char* path, const char* value);

/**
 * Points an object property at another registered object - a layer's data source, a decoder's
 * style. MM_NULL_HANDLE clears it.
 *
 * The value's class is checked against the property's first, so pointing a style property at a
 * source is MM_UNKNOWN_CLASS rather than undefined behaviour.
 */
MM_API int mm_set_object(mm_ctx ctx, mm_handle handle, const char* path, mm_handle value);

MM_API int mm_get_bool(mm_ctx ctx, mm_handle handle, const char* path, int* value);
MM_API int mm_get_long(mm_ctx ctx, mm_handle handle, const char* path, int64_t* value);
MM_API int mm_get_double(mm_ctx ctx, mm_handle handle, const char* path, double* value);

/**
 * Reads a string, a struct as JSON, or a JSON subtree.
 *
 * Two-call protocol: pass a null buffer to learn the size, allocate, call again. `needed` is the
 * byte count INCLUDING the terminating NUL, and is always set on MM_OK and MM_BUFFER_TOO_SMALL, so
 * a caller that guessed can retry without asking twice.
 *
 * @param projection A well-known name, e.g. "EPSG:3857", to read a position in. Null or empty
 *                   means the projection the running event handler asked for, and WGS84 when there
 *                   is none. Ignored for anything that is not a coordinate.
 */
MM_API int mm_get_string(mm_ctx ctx, mm_handle handle, const char* path, const char* projection,
                         char* buffer, size_t size, size_t* needed);

/**
 * Reads a position, or anything else that is a small array of numbers, straight into doubles.
 *
 * mm_get_string would hand a click handler a JSON string to allocate and parse, per event. JSI,
 * WASM and dart:ffi all want the numbers. No two-call protocol here on purpose: a position is at
 * most 3 doubles and a bounds 6, so the caller passes a fixed buffer and is told how many were
 * there.
 *
 * @param projection A well-known name, e.g. "EPSG:3857". Null or empty means the projection the
 *                   running event handler asked for, and WGS84 when there is none.
 * @param out Filled with up to `count` numbers. May be null to ask only how many there are.
 * @param count The capacity of `out`.
 * @param needed Set to how many numbers the value HAS, whether or not they fit.
 * @return MM_OK, MM_BUFFER_TOO_SMALL when there are more than `count`, or the read's own error.
 *         MM_UNSUPPORTED_TYPE when the value is not an array of numbers.
 */
MM_API int mm_get_position(mm_ctx ctx, mm_handle handle, const char* path, const char* projection,
                           double* out, size_t count, size_t* needed);

/* --- binary and bulk ----------------------------------------------------------------------- */

/*
 * Neither of these is allowed to become a string. A tile is a blob and an elevation profile is
 * thousands of numbers; encoding either as JSON is what this API exists to avoid.
 */

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
 * Runs a method on an object.
 *
 * The result is ALWAYS a handle THE CALLER OWNS - pass it to mm_destroy_handle. An object result
 * is that object; anything else is a JSON document read with an empty path.
 *
 * @param args_json The arguments as a JSON array, e.g. "[[8467,5852,14]]". Null for none.
 */
MM_API int mm_call(mm_ctx ctx, mm_handle handle, const char* method, const char* args_json,
                   mm_handle* result);

/**
 * The same, on a worker thread, with the result delivered as an event on the object.
 *
 * Subscribe to `event` first; the payload is the result, and 0 means the call failed. The payload
 * is freed once the handlers have run, so unlike mm_call nothing has to be destroyed by hand.
 *
 * The handle, the method and the argument JSON are checked before anything is queued, so a mistake
 * is reported here rather than to a log later.
 */
MM_API int mm_call_async(mm_ctx ctx, mm_handle handle, const char* method, const char* args_json,
                         const char* event, mm_call_id* call);

/**
 * Cancels a queued or running async call.
 *
 * Cancelling stops a call being STARTED and stops its result being DELIVERED. It cannot abort one
 * already running - the SDK's load paths take no cancellation token - so a cancelled call in
 * flight finishes and its result is dropped. Either way no event fires.
 *
 * @return MM_OK when it was queued or running, MM_BAD_HANDLE when it had already finished.
 */
MM_API int mm_cancel_call(mm_ctx ctx, mm_call_id call);

/** Cancels every queued or running call on an object. @param count Set to how many. Optional. */
MM_API int mm_cancel_calls(mm_ctx ctx, mm_handle handle, int* count);

/* --- events -------------------------------------------------------------------------------- */

/**
 * Subscribes to an event on an object. Handlers run in registration order.
 *
 * @param opts_json A JSON object, or null for the defaults. Options, rather than parameters, so a
 *        new one never changes this signature:
 *          {"delivery":"origin"|"ui"|"background",   where the handler runs, default "origin"
 *           "consume":true,      its return value can stop the event; requires "origin"
 *           "coalesce":true,     replace a pending event rather than queueing a second
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
 * Registers how to reach the embedder's loop, for subscriptions that asked for "ui" delivery.
 *
 * The dispatcher is called from whatever thread produced the event, and must arrange for
 * `function(argument)` to run on the target loop; that call is mm_drain. Without one, "ui"
 * subscriptions run inline and say so once.
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
