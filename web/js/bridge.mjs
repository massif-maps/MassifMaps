/*
 * The web's NativeBridge for @massif-maps/api: the same interface the NativeScript plugin gives
 * Android and iOS, over the facade's C ABI. "Native" objects are tokens naming a part of the one map.
 */

const OK = 0;

/** A JavaScript handler lives in the page's wasm table only, so every event is delivered there. */
const DELIVERY_UI = 'ui';

export function createBridge(module) {
  const fn = (name, returns, args) => module.cwrap(name, returns, args);
  const c = {
    ctx: fn('mm_context_default', 'number', [])(),
    create: fn('mm_create', 'number', ['number', 'string', 'string', 'string', 'number']),
    destroy: fn('mm_destroy', 'number', ['number', 'string', 'string']),
    destroyHandle: fn('mm_destroy_handle', 'number', ['number', 'number']),
    find: fn('mm_find', 'number', ['number', 'string', 'string', 'number']),
    valid: fn('mm_valid', 'number', ['number', 'number']),
    setBool: fn('mm_set_bool', 'number', ['number', 'number', 'string', 'number']),
    setDouble: fn('mm_set_double', 'number', ['number', 'number', 'string', 'number']),
    setString: fn('mm_set_string', 'number', ['number', 'number', 'string', 'string']),
    setJson: fn('mm_set_json', 'number', ['number', 'number', 'string', 'string']),
    setObject: fn('mm_set_object', 'number', ['number', 'number', 'string', 'number']),
    getObject: fn('mm_get_object', 'number', ['number', 'number', 'string']),
    getBool: fn('mm_get_bool', 'number', ['number', 'number', 'string', 'number']),
    getDouble: fn('mm_get_double', 'number', ['number', 'number', 'string', 'number']),
    getString: fn('mm_get_string', 'number', ['number', 'number', 'string', 'string', 'number', 'number', 'number']),
    call: fn('mm_call', 'number', ['number', 'number', 'string', 'string', 'number']),
    callAsync: fn('mm_call_async', 'number', ['number', 'number', 'string', 'string', 'string', 'number']),
    cancelCall: fn('mm_cancel_call', 'number', ['number', 'number']),
    cancelCalls: fn('mm_cancel_calls', 'number', ['number', 'number', 'number']),
    doublesCount: fn('mm_doubles_count', 'number', ['number', 'number', 'number']),
    doublesCopy: fn('mm_doubles_copy', 'number', ['number', 'number', 'number', 'number', 'number']),
    dataSize: fn('mm_data_size', 'number', ['number', 'number', 'string', 'number']),
    dataCopy: fn('mm_data_copy', 'number', ['number', 'number', 'string', 'number', 'number', 'number']),
    on: fn('mm_on', 'number', ['number', 'number', 'string', 'number', 'number', 'string', 'number']),
    off: fn('mm_off', 'number', ['number', 'number']),
    offAll: fn('mm_off_all', 'number', ['number', 'number', 'number']),
    resultName: fn('mm_result_name', 'string', ['number']),
    adopt: fn('massifAdopt', 'number', ['string', 'string', 'string']),
    attachMapEvents: fn('massifAttachMapEvents', null, ['number']),
    bridgeLayerClicks: fn('massifBridgeLayerClicks', 'number', ['number']),
  };
  const ctx = c.ctx;
  // Function-table slot of each live subscription, released on off.
  const handlers = new Map();

  // Zeroed scratch memory, always freed: an out-parameter the ABI leaves alone must not read as garbage.
  const scratch = (size, body) => {
    const pointer = module._malloc(size);
    module.HEAPU8.fill(0, pointer, pointer + size);
    try {
      return body(pointer);
    } finally {
      module._free(pointer);
    }
  };
  // Handles, subscriptions and call ids are uint32 with a generation in the top bits: read unsigned.
  const unsigned = (out) => module.getValue(out, 'i32') >>> 0;
  const outInt = (body) => scratch(4, (out) => (body(out) === OK ? unsigned(out) : 0));
  const fail = (what, code) => {
    throw new Error(`${what} failed: ${c.resultName(code)} (${code})`);
  };
  const readString = (handle, path, projection) => scratch(4, (needed) => {
    if (c.getString(ctx, handle, path, projection ?? '', 0, 0, needed) !== OK) {
      return null;
    }
    const size = module.getValue(needed, 'i32');
    return scratch(size + 1, (buffer) =>
      c.getString(ctx, handle, path, projection ?? '', buffer, size + 1, needed) === OK ? module.UTF8ToString(buffer) : null);
  });
  const readNumber = (getter, handle, path, fallback, type) => scratch(8, (out) =>
    getter(ctx, handle, path, out) === OK ? module.getValue(out, type) : fallback);
  // A token for a part of the map, which massifAdopt names.
  const part = (what) => ({ webPart: what });

  return {
    available: true,
    canConsume: false,

    create(kind, id, json) {
      let code = OK;
      const handle = scratch(4, (out) => {
        code = c.create(ctx, kind, id, json, out);
        return unsigned(out);
      });
      return code === OK ? handle : fail(`create ${kind} "${id}"`, code);
    },
    destroy: (handle) => c.destroyHandle(ctx, handle) === OK,
    isValid: (handle) => c.valid(ctx, handle) === OK,
    findObject: (kind, id) => outInt((out) => c.find(ctx, kind, id, out)),
    unregisterObject: (kind, id) => c.destroy(ctx, kind, id) === OK,

    // One numeric type in JavaScript; the ABI coerces a double into an int property.
    setFloat: (handle, path, value) => c.setDouble(ctx, handle, path, value),
    setInt: (handle, path, value) => c.setDouble(ctx, handle, path, value),
    setBool: (handle, path, value) => c.setBool(ctx, handle, path, value ? 1 : 0),
    setString: (handle, path, value) => c.setString(ctx, handle, path, value),
    setObject: (handle, path, value) => c.setObject(ctx, handle, path, value),
    setAll: (handle, json, projection) => c.setJson(ctx, handle, json, projection ?? ''),
    getObject: (handle, path) => c.getObject(ctx, handle, path) >>> 0,

    getFloat: (handle, path, fallback) => readNumber(c.getDouble, handle, path, fallback, 'double'),
    getInt: (handle, path, fallback) => Math.trunc(readNumber(c.getDouble, handle, path, fallback, 'double')),
    getBool: (handle, path, fallback) => !!readNumber(c.getBool, handle, path, fallback ? 1 : 0, 'i32'),
    getString: (handle, path) => readString(handle, path, ''),
    getPos: (handle, path, projection) => readString(handle, path, projection) || null,

    call(handle, method, argsJson) {
      let code = OK;
      const result = scratch(4, (out) => {
        code = c.call(ctx, handle, method, argsJson, out);
        return unsigned(out);
      });
      return code === OK ? result : fail(`call ${method}`, code);
    },
    callAsync(handle, method, argsJson, event) {
      let code = OK;
      const call = scratch(4, (out) => {
        code = c.callAsync(ctx, handle, method, argsJson, event, out);
        return unsigned(out);
      });
      return code === OK ? call : fail(`callAsync ${method}`, code);
    },
    cancelCall: (call) => c.cancelCall(ctx, call) === OK,
    cancelCalls: (handle) => outInt((out) => c.cancelCalls(ctx, handle, out)),

    getDoubles(handle) {
      const count = outInt((out) => c.doublesCount(ctx, handle, out));
      if (!count) {
        return [];
      }
      return scratch(count * 8, (buffer) => scratch(4, (copied) => {
        c.doublesCopy(ctx, handle, buffer, count, copied);
        return Array.from(new Float64Array(module.HEAPF64.buffer, buffer, count));
      }));
    },
    getData(handle, path) {
      const size = outInt((out) => c.dataSize(ctx, handle, path, out));
      if (!size) {
        return null;
      }
      return scratch(size, (buffer) => scratch(4, (copied) =>
        c.dataCopy(ctx, handle, path, buffer, size, copied) === OK ? module.HEAPU8.slice(buffer, buffer + size).buffer : null));
    },

    on(handle, event, handler, delivery, coalesce, projection) {
      const pointer = module.addFunction((userData, target, eventName, payload) =>
        (handler(target >>> 0, module.UTF8ToString(eventName), payload >>> 0) ? 1 : 0), 'iiiii');
      const options = JSON.stringify({ delivery: DELIVERY_UI, coalesce: !!coalesce, ...(projection ? { projection } : {}) });
      const subscription = outInt((out) => c.on(ctx, handle, event, pointer, 0, options, out));
      if (subscription) {
        handlers.set(subscription, pointer);
      } else {
        module.removeFunction(pointer);
      }
      return subscription;
    },
    off(subscription) {
      const removed = c.off(ctx, subscription) === OK;
      const pointer = handlers.get(subscription);
      if (pointer !== undefined) {
        module.removeFunction(pointer);
        handlers.delete(subscription);
      }
      return removed;
    },
    // Table slots of these subscriptions stay until the page goes: the ABI does not say which they were.
    offAll: (handle) => outInt((out) => c.offAll(ctx, handle, out)),

    adoptOptions: (kind, id, native) => (native?.webPart === 'options' ? c.adopt(kind, 'options', id) : 0),
    adoptLayers: (kind, id, native) => (native?.webPart === 'layers' ? c.adopt(kind, 'layers', id) : 0),
    adoptView: (kind, id, native) => (native?.webPart === 'view' ? c.adopt(kind, 'view', id) : 0),
    // Layers, sources and assets are only ever built from specs on the web: there is no object API.
    adoptLayer: () => 0,
    adoptSource: () => 0,
    adoptAssets: () => 0,
    getNativeLayer(id) {
      const handle = outInt((out) => c.find(ctx, 'layer', id, out));
      return handle ? { webLayer: handle } : null;
    },
    getNativeSource: () => null,
    getNativeLayerByHandle: (handle) => ({ webLayer: handle }),
    getNativeSourceByHandle: () => null,

    attachMapEvents: (mapView, handle) => c.attachMapEvents(handle),
    attachVectorTileEvents: (layer, handle) => void c.bridgeLayerClicks(handle),
    attachVectorElementEvents: (layer, handle) => void c.bridgeLayerClicks(handle),
    attachCelestialEvents: (layer, handle) => void c.bridgeLayerClicks(handle),

    nativeShortClassName: () => null,

    /** The tokens a MapViewLike hands to the adopt calls above. */
    parts: { view: part('view'), options: part('options'), layers: part('layers') },
  };
}
