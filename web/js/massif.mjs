/*
 * The JavaScript binding: a thin wrapper over the facade's C ABI (all/native/api/MassifApiC.h).
 *
 * There is deliberately almost nothing here. The facade is a table, not a class hierarchy - every
 * object is a kind plus an id, every property a path, every method a name - so a binding is string
 * marshalling and nothing else, and a new SDK feature reaches JavaScript without touching this
 * file. See docs/internals/api-facade.md.
 *
 * The typings for what you can pass are generated: bindings/typescript/massif.d.ts.
 */

const RESULT_OK = 0;

/** Thrown for any non-zero ABI result, carrying the name the ABI gives it. */
export class MassifError extends Error {
  constructor(code, name, what) {
    super(`${what} failed: ${name} (${code})`);
    this.code = code;
  }
}

export class Massif {
  #module;
  #ctx;
  #fn;
  // Every function pointer we added to the table, so they can be released.
  #handlers = new Map();

  constructor(module) {
    this.#module = module;
    const cwrap = module.cwrap;
    this.#fn = {
      resultName: cwrap('mm_result_name', 'string', ['number']),
      contextDefault: cwrap('mm_context_default', 'number', []),
      abiVersion: cwrap('mm_abi_version', 'number', []),
      create: cwrap('mm_create', 'number', ['number', 'string', 'string', 'string', 'number']),
      destroy: cwrap('mm_destroy', 'number', ['number', 'string', 'string']),
      find: cwrap('mm_find', 'number', ['number', 'string', 'string', 'number']),
      setBool: cwrap('mm_set_bool', 'number', ['number', 'number', 'string', 'number']),
      setDouble: cwrap('mm_set_double', 'number', ['number', 'number', 'string', 'number']),
      setString: cwrap('mm_set_string', 'number', ['number', 'number', 'string', 'string']),
      setJson: cwrap('mm_set_json', 'number', ['number', 'number', 'string', 'string']),
      getBool: cwrap('mm_get_bool', 'number', ['number', 'number', 'string', 'number']),
      getDouble: cwrap('mm_get_double', 'number', ['number', 'number', 'string', 'number']),
      getString: cwrap('mm_get_string', 'number', ['number', 'number', 'string', 'string', 'number', 'number', 'number']),
      setObject: cwrap('mm_set_object', 'number', ['number', 'number', 'string', 'number']),
      getObject: cwrap('mm_get_object', 'number', ['number', 'number', 'string']),
      call: cwrap('mm_call', 'number', ['number', 'number', 'string', 'string', 'number']),
      destroyHandle: cwrap('mm_destroy_handle', 'number', ['number', 'number']),
      on: cwrap('mm_on', 'number', ['number', 'number', 'string', 'number', 'number', 'number']),
      off: cwrap('mm_off', 'number', ['number', 'number']),
      drain: cwrap('mm_drain', 'number', ['number', 'number']),
    };
    this.#ctx = this.#fn.contextDefault();
  }

  get abiVersion() {
    return this.#fn.abiVersion();
  }

  #check(code, what) {
    if (code !== RESULT_OK) {
      throw new MassifError(code, this.#fn.resultName(code), what);
    }
    return code;
  }

  /**
   * Runs `body(pointer)` with `size` bytes of scratch, and always frees them.
   * Zeroed: malloc does not, and an out-parameter the ABI leaves alone then reads as garbage - a
   * void method's result length came back as 4 and parsed as a control character.
   */
  #withBuffer(size, body) {
    const pointer = this.#module._malloc(size);
    this.#module.HEAPU8.fill(0, pointer, pointer + size);
    try {
      return body(pointer);
    } finally {
      this.#module._free(pointer);
    }
  }

  /**
   * A string out-parameter: the ABI answers the length when the buffer is too small, so ask once
   * with nothing and once with enough.
   */
  #readString(fill) {
    return this.#withBuffer(4, (lengthPointer) => {
      this.#check(fill(0, 0, lengthPointer), 'read');
      const length = this.#module.getValue(lengthPointer, 'i32');
      if (length <= 0) {
        return '';
      }
      return this.#withBuffer(length + 1, (buffer) => {
        this.#check(fill(buffer, length + 1, lengthPointer), 'read');
        return this.#module.UTF8ToString(buffer);
      });
    });
  }

  /** Creates an object of `kind` under `id`, described by `spec`. Returns its handle. */
  create(kind, id, spec) {
    return this.#withBuffer(4, (out) => {
      this.#check(this.#fn.create(this.#ctx, kind, id, JSON.stringify(spec ?? {}), out), `create ${kind}`);
      return this.#module.getValue(out, 'i32');
    });
  }

  destroy(kind, id) {
    this.#check(this.#fn.destroy(this.#ctx, kind, id), `destroy ${kind}`);
  }

  /** The handle of an object created earlier, or 0. */
  find(kind, id) {
    return this.#withBuffer(4, (out) => {
      const code = this.#fn.find(this.#ctx, kind, id, out);
      return code === RESULT_OK ? this.#module.getValue(out, 'i32') : 0;
    });
  }

  set(handle, path, value) {
    switch (typeof value) {
      case 'boolean':
        return void this.#check(this.#fn.setBool(this.#ctx, handle, path, value ? 1 : 0), `set ${path}`);
      case 'number':
        return void this.#check(this.#fn.setDouble(this.#ctx, handle, path, value), `set ${path}`);
      case 'string':
        return void this.#check(this.#fn.setString(this.#ctx, handle, path, value), `set ${path}`);
      default:
        // An object property is a spec, which the ABI takes as JSON on the object itself.
        return void this.#check(this.#fn.setJson(this.#ctx, handle, JSON.stringify(value), ''), `set ${path}`);
    }
  }

  getNumber(handle, path) {
    return this.#withBuffer(8, (out) => {
      this.#check(this.#fn.getDouble(this.#ctx, handle, path, out), `get ${path}`);
      return this.#module.getValue(out, 'double');
    });
  }

  getBool(handle, path) {
    return this.#withBuffer(4, (out) => {
      this.#check(this.#fn.getBool(this.#ctx, handle, path, out), `get ${path}`);
      return this.#module.getValue(out, 'i32') !== 0;
    });
  }

  /**
   * Writes an object PROPERTY - `setObject(mapOptions, 'lightOptions', light)`.
   *
   * Its own call because a handle is neither a value nor a spec: `set` would send it as a number
   * and the ABI refuses that with MM_UNSUPPORTED_TYPE.
   */
  setObject(handle, path, value) {
    this.#check(this.#fn.setObject(this.#ctx, handle, path, value), `set ${path}`);
  }

  /**
   * The handle of an object PROPERTY - `getObject(mapOptions, 'lightOptions')`.
   *
   * Reading a sub-object as a value would flatten it; a handle is what lets its own methods be
   * called. Returns 0 when nothing is set there.
   */
  getObject(handle, path) {
    return this.#fn.getObject(this.#ctx, handle, path);
  }

  getString(handle, path, projection = '') {
    return this.#readString((buffer, size, lengthOut) =>
      this.#fn.getString(this.#ctx, handle, path, projection, buffer, size, lengthOut));
  }

  /**
   * Calls a facade method. `args` is positional - `flyTo` takes
   * `[[lon, lat], zoom, rotation, tilt, climbHeight, seconds]` - and the result comes back parsed.
   * A method that produces nothing returns undefined.
   */
  call(handle, method, args) {
    // The result is a HANDLE to a registered value, not a string - so it is read like any other
    // object and then released, or the context keeps it forever.
    const resultHandle = this.#withBuffer(4, (out) => {
      this.#check(this.#fn.call(this.#ctx, handle, method, JSON.stringify(args ?? []), out), `call ${method}`);
      return this.#module.getValue(out, 'i32');
    });
    if (!resultHandle) {
      return undefined; // the method produced nothing, which is most of them
    }
    try {
      const text = this.getString(resultHandle, '');
      if (!text) {
        return undefined;
      }
      try {
        return JSON.parse(text);
      } catch (error) {
        return text; // not every result is JSON
      }
    } finally {
      this.#fn.destroyHandle(this.#ctx, resultHandle);
    }
  }

  /**
   * Subscribes to an event. Returns an unsubscribe function.
   * The handler is added to the wasm table, so it must be removed again or the table only grows.
   */
  on(handle, event, callback) {
    const pointer = this.#module.addFunction((ctx, subscription, jsonPointer, userData) => {
      const json = jsonPointer ? this.#module.UTF8ToString(jsonPointer) : '';
      callback(json ? JSON.parse(json) : {});
    }, 'viiii');
    const subscription = this.#withBuffer(4, (out) => {
      this.#check(this.#fn.on(this.#ctx, handle, event, pointer, 0, out), `on ${event}`);
      return this.#module.getValue(out, 'i32');
    });
    this.#handlers.set(subscription, pointer);
    return () => {
      this.#check(this.#fn.off(this.#ctx, subscription), 'off');
      this.#module.removeFunction(pointer);
      this.#handlers.delete(subscription);
    };
  }

  /** Delivers queued events for subscriptions that asked for the UI thread. */
  drain() {
    return this.#withBuffer(4, (out) => {
      this.#check(this.#fn.drain(this.#ctx, out), 'drain');
      return this.#module.getValue(out, 'i32');
    });
  }
}

/**
 * The camera of a map adopted by the host under `kind: "map"`.
 *
 * Sugar, not a second API: every call below is one facade call, spelled the way a web map spells
 * it. `massif.call(handle, ...)` reaches anything this does not cover.
 */
export class MassifCamera {
  #massif;
  #handle;

  constructor(massif, id = 'map') {
    this.#massif = massif;
    this.#handle = massif.find('map', id);
    if (!this.#handle) {
      throw new Error(`No map adopted under the id "${id}"`);
    }
  }

  /**
   * Waits for the host to adopt its map, then returns the camera.
   *
   * The module's promise resolves when the RUNTIME is up, which with pthreads is before main() has
   * run - so the map is usually adopted a tick or two later, and reading it straight away is a
   * race. Polling costs nothing and works whenever the host adopts.
   */
  static async attach(massif, id = 'map', timeoutMs = 10000) {
    const deadline = performance.now() + timeoutMs;
    for (;;) {
      if (massif.find('map', id)) {
        return new MassifCamera(massif, id);
      }
      if (performance.now() > deadline) {
        throw new Error(`No map adopted under the id "${id}" after ${timeoutMs} ms`);
      }
      await new Promise((resolve) => requestAnimationFrame(resolve));
    }
  }

  get handle() {
    return this.#handle;
  }

  get zoom() {
    return this.#massif.getNumber(this.#handle, 'zoom');
  }

  get rotation() {
    return this.#massif.getNumber(this.#handle, 'rotation');
  }

  get tilt() {
    return this.#massif.getNumber(this.#handle, 'tilt');
  }

  get focusPos() {
    return JSON.parse(this.#massif.getString(this.#handle, 'focusPos', 'EPSG:4326'));
  }

  moveTo(options) {
    return this.#massif.call(this.#handle, 'moveTo', options);
  }

  flyTo(options) {
    return this.#massif.call(this.#handle, 'flyTo', options);
  }

  fitBounds(options) {
    return this.#massif.call(this.#handle, 'fitBounds', options);
  }

  screenToMap(options) {
    return this.#massif.call(this.#handle, 'screenToMap', options);
  }

  mapToScreen(options) {
    return this.#massif.call(this.#handle, 'mapToScreen', options);
  }
}
