import { EventData, Observable } from './observable.js';
import { bridge } from './bridge.js';
import type { Delivery as NativeDelivery } from './bridge.js';
export { setBridge } from './bridge.js';
export type { NativeBridge, NativeEventHandler, Delivery } from './bridge.js';
import { classOfShortName, classOfSpec, enumName, enumValue, eventNames, findEvent, isKnownClass, isSubclassOf, propertyNames, resolveMethod, resolvePath, specKindOf } from './resolve.js';
import type {
    Bounds,
    ClassName,
    ClassOfSpec,
    EventName,
    Handle,
    Json,
    Kind,
    MethodArgs,
    MethodName,
    MethodResult,
    MethodResultClass,
    ClassAtPath,
    ObjectPath,
    PayloadClass,
    Position,
    PositionAt,
    PositionPath,
    Tile,
    ProjectionName,
    SpecArg,
    SpecType,
    ValueAt,
    WriteAt,
    ValuePath,
    WritablePath
} from './massif-api.js';

// `export type *`, not `export *`: massif-api is a .d.ts, so there is no module to import at
// runtime and a value re-export would emit one. Verified - a plain `export *` is NOT elided.
export type * from './massif-api.js';

/**
 * The MassifMaps surface API, as a JavaScript app writes it - NativeScript or the web.
 *
 * The SDK's facade is six verbs over strings and numbers - create, destroy, set, get, call, on -
 * and every capability is data passed through them. This layer adds the three things that do not
 * survive a string API: the value comes back as the JavaScript type it should be, an event is an
 * Observable event, and the path, the spec key, the method name and the event name all complete
 * and type-check, because they are generated from the same table the C++ resolves against.
 *
 * It lives beside the object API rather than replacing it. `adopt` gives an object built the old
 * way an id and everything that follows from one.
 */

// ---------------------------------------------------------------------------------------------
// errors and results
// ---------------------------------------------------------------------------------------------

/**
 * The SDK's result codes, mirrored so a caller can tell a typo from a real failure.
 * Kept in step with `massif::api::Result`; anything unlisted arrives as its number.
 */
export const Result = {
    OK: 0,
    BAD_HANDLE: 1,
    UNKNOWN_CLASS: 2,
    UNKNOWN_PROPERTY: 3,
    READONLY: 4,
    UNSUPPORTED_TYPE: 5,
    NULL_OBJECT: 8
} as const;

export class MassifApiError extends Error {
    constructor(
        message: string,
        readonly result?: number
    ) {
        super(message);
        this.name = 'MassifApiError';
    }
}

/** Whether the linked SDK carries the surface API. False on a build made without it. */
export function isAvailable(): boolean {
    return bridge.available;
}

/**
 * Whether a handler can claim an event by setting `consumed`.
 *
 * False on an SDK whose `MassifApi.on` does not take the consume flag - the SDK ignores what a
 * non-consuming handler returns, so `consumed` is accepted and does nothing. `consumable` on
 * each event says the same thing per event, and a handler that sets it anyway is warned once.
 */
export function canConsume(): boolean {
    return bridge.canConsume;
}

function requireApi() {
    if (!bridge.available) {
        throw new MassifApiError('this MassifMaps build has no surface API - the SDK was built without all/native/api');
    }
}

// ---------------------------------------------------------------------------------------------
// events
// ---------------------------------------------------------------------------------------------

/**
 * EVERY handler in this API runs on the main thread. There is no option, because in NativeScript
 * there is no other answer.
 *
 * The SDK emits from its render and tile threads, and NativeScript has no JavaScript runtime on
 * those - a callback there does not run late, it fails to run at all. So the plugin's own native
 * listener (`com.nativescript.massifmaps.api.EventListener`, `NSMSFApiEventListener`) hops onto
 * the main thread and WAITS before calling into JavaScript, which is the same thing every other
 * listener in this plugin does.
 *
 * Waiting rather than posting is what makes two other things work: `consumed` gets back to the
 * SDK in time to claim the event, and the payload is still alive when the handler reads it.
 *
 * The facade has its own delivery modes (`DELIVERY_UI`, `DELIVERY_BACKGROUND`) and they are NOT
 * exposed here. `DELIVERY_UI` needs a UiDispatcher whose `post()` is itself called on the
 * producing thread - JavaScript again - and `DELIVERY_BACKGROUND` is a thread JavaScript cannot
 * run on at all. Both would be a knob that silently does not work.
 */
const DELIVERY_ORIGIN: NativeDelivery = 0;

export interface SubscribeOptions {
    /**
     * The projection this handler's position reads default to, e.g. `'EPSG:3857'`. Applies for
     * the duration of the call, so a payload kept and read afterwards falls back to WGS84, which
     * is the default everywhere else - name it per read to override that.
     */
    projection?: ProjectionName;
    /**
     * Drop events that arrive within this many milliseconds of the last one handled.
     *
     * `map.moved` fires well above frame rate - 47 to 159 a second during a drag - and a handler
     * that repositions a view does not need every one. The facade's own coalescing is not the
     * answer here: it replaces a PENDING payload, and nothing is ever pending when the producer
     * and the handler are the same thread, which they are.
     *
     * Never use it on a consumable event: a dropped click is one the SDK is still waiting on.
     */
    throttle?: number;
    /**
     * Deliver only the LAST event of a burst, this many milliseconds after the burst stops.
     *
     * The trailing edge, where `throttle` is the leading one: use this for work that should
     * happen once the map settles - saving the camera, refetching what is on screen - and
     * `throttle` for work that should track the movement.
     *
     * The payload the handler gets is a **snapshot**, read at emit time and frozen: the facade
     * frees the real one the moment the emit returns, long before this fires. `data.payload` is
     * therefore null and `data.get(path)` reads the snapshot.
     *
     * Never use it on a consumable event - the SDK is waiting for the answer now, not later -
     * and note that `map.stable` is already once-per-movement in the SDK, so it rarely needs one.
     */
    debounce?: number;
}

/**
 * One event, as it reaches a handler.
 *
 * `payload` is valid ONLY while the handler runs: the facade frees it on the way out. Read what
 * you need, do not keep the object.
 */
/**
 * The prototype an event of this payload class is created from, with a getter per property so a
 * handler reads `e.reason` or `e.clickType` instead of `e.get('reason')`.
 *
 * Built ONCE per class and cached: the alternative, defining the properties on each event, costs
 * a defineProperty per property per event, and `map.moved` arrives 47 to 159 times a second.
 * Creating an event is then one Object.create.
 *
 * Nothing here touches native. Each getter is one read AT ACCESS TIME through the event's own
 * `get`, which is what keeps the promise that asking for one property never parses the whole
 * feature - and what makes an event nobody reads free. Own properties (`get`, `payload`, ...)
 * shadow the prototype, so a payload property sharing one of those names cannot hide it.
 *
 * Nested paths (`feature.properties.name`) are not property names and stay with `get`.
 */
const payloadProtos: { [cls: string]: object } = {};

function payloadProto(payloadClass: string): object {
    let proto = payloadProtos[payloadClass];
    if (!proto) {
        proto = payloadProtos[payloadClass] = {};
        for (const name of propertyNames(payloadClass)) {
            Object.defineProperty(proto, name, {
                enumerable: true,
                configurable: true,
                get(this: MassifEventData) {
                    return (this as any).get(name);
                }
            });
        }
    }
    return proto;
}


/** A payload path that is a property name, i.e. not a walk into a struct. */
type TopLevelPath<P> = P extends `${string}.${string}` ? never : P;

/**
 * The payload's own properties, readable straight off the event - see withPayloadGetters.
 *
 * Names that would shadow the event's own fields are dropped: the event keeps its meaning, and
 * the property is still reachable through `get`.
 */
export type PayloadFields<C extends ClassName, E extends EventName<C>> = Omit<
    { readonly [K in TopLevelPath<ValuePath<PayloadClass<C, E>>>]: ValueAt<PayloadClass<C, E>, K> },
    'eventName' | 'object' | 'payload' | 'consumed' | 'consumable' | 'get' | 'getPos'
>;

export type MassifEventData<C extends ClassName = any, E extends EventName<C> = EventName<C>> = EventData &
    PayloadFields<C, E> & {
        eventName: string;
        object: MassifObject<C>;
        /** The event's data, or null when it carries none - `map.idle` is the only one left. */
        payload: MassifObject<PayloadClass<C, E>> | null;
        /**
         * Set this to true to claim the event: the SDK stops offering it to anything behind you -
         * later subscribers, and the map's own handling.
         *
         * Only a CONSUMABLE event can be claimed - `vectortile.clicked` and
         * `vectorelement.clicked`; `consumable` on this object says which. Setting it on anything
         * else is ignored, and warned about once rather than silently doing nothing.
         */
        consumed: boolean;
        /** Whether setting `consumed` on this event does anything. */
        readonly consumable: boolean;
        /** Shorthand for `payload.get(path)`. Throws when the event carries no payload. */
        get<P extends ValuePath<PayloadClass<C, E>>>(path: P): ValueAt<PayloadClass<C, E>, P>;
        /** Shorthand for `payload.getPos(path, projection)`. */
        getPos<P extends PositionPath<PayloadClass<C, E>>>(path: P, projection?: ProjectionName): PositionAt<PayloadClass<C, E>, P> | null;
        getPos(path: string & {}, projection?: ProjectionName): Position | Bounds | null;
    };

/** A live subscription. Removing it twice is an error the SDK reports, so this is idempotent. */
export class Subscription {
    private mRemoved = false;
    /** Cancels a debounced delivery that has not fired yet, so `remove()` really means removed. */
    private mCancelPending: (() => void) | null = null;

    constructor(readonly id: number) {}

    get valid() {
        return !this.mRemoved;
    }

    remove(): boolean {
        if (this.mRemoved) {
            return false;
        }
        this.mRemoved = true;
        if (this.mCancelPending) {
            this.mCancelPending();
            this.mCancelPending = null;
        }
        return bridge.off(this.id);
    }

    /** @internal */
    onRemove(cancel: () => void) {
        this.mCancelPending = cancel;
    }
}

// ---------------------------------------------------------------------------------------------
// promises that can be cancelled
// ---------------------------------------------------------------------------------------------

/**
 * What `callAsync` returns.
 *
 * Cancelling stops a call being started and stops its result being delivered. It cannot abort one
 * already running - the SDK's load paths take no cancellation token - so a cancelled call in
 * flight finishes its work and the result is dropped. Either way the promise rejects.
 */
export interface CancellablePromise<T> extends Promise<T> {
    cancel(): boolean;
}

// ---------------------------------------------------------------------------------------------
// values
// ---------------------------------------------------------------------------------------------

function parseJson(text: string | null): Json {
    if (text === null || text === '') {
        return null;
    }
    try {
        return JSON.parse(text);
    } catch (e) {
        // A plain string property that is not valid JSON is its own value.
        return text;
    }
}

/** A spec written inline: a plain object with a `type`, not a handle and not a live object. */
type SpecObject = { type: string } & { [key: string]: unknown };

function isSpec(value: unknown): value is SpecObject {
    return (
        !!value &&
        typeof value === 'object' &&
        !Array.isArray(value) &&
        !(value instanceof MassifObject) &&
        typeof (value as { handle?: unknown }).handle !== 'number' &&
        typeof (value as { type?: unknown }).type === 'string'
    );
}

function handleOf(value: unknown): number {
    if (typeof value === 'number') {
        return value;
    }
    if (value && typeof (value as MassifObject).handle === 'number') {
        return (value as MassifObject).handle;
    }
    return 0;
}

/**
 * What the plugin's own view takes: an object, not a `[lon, lat]` pair.
 *
 * The KEY NAMES are the app's - `latitude`/`longitude`/`altitude` unless it called
 * setMapPosKeys - so both directions below read them off the core module rather than
 * hard-coding a spelling. Writing `{ lat, lon }` here made every view call throw
 * "toNativeMapPos: missing lat/lon parameters", because that converter reads the
 * configured keys.
 */
export interface LatLon {
    // `| undefined` because an altitude is optional in every app's own position type, and an
    // index signature of plain `number` refuses one.
    [key: string]: number | undefined;
}

/** A position either way round, so the surface API and the view's own calls mix freely. */
export type AnyPosition = Position | LatLon;

/**
 * The facade's own shape, `[lng, lat]`, which is what every call on this surface carries.
 *
 * An object form is accepted because the plugin's view API uses one, and both spellings are read
 * so `{ latitude, longitude }` and `{ lat, lng }` both work. Deliberately NOT the configurable
 * LatitudeKey/LongitudeKey: those belong to the view API, and a facade position should not change
 * meaning because an app called setMapPosKeys. Either way it reaches the native side as an array
 * and never as a MapPos - which is the point of the type (massif-maps/MassifMaps#159).
 */
function toPosition(position: AnyPosition): Position {
    if (Array.isArray(position)) {
        return position;
    }
    // `lon` as well as `lng`: it is what the SDK's own MapPos spells, so an app moving off the
    // object API has its positions in that shape already.
    const lng = position.longitude ?? position.lng ?? position.lon;
    const lat = position.latitude ?? position.lat;
    const altitude = position.altitude ?? position.alt;
    if (lng === undefined || lat === undefined) {
        throw new MassifApiError(`not a position: ${JSON.stringify(position)}`);
    }
    return altitude ? [lng, lat, altitude] : [lng, lat];
}

/** The JSON `call` wants: a plain array of the arguments, with any object collapsed to a handle. */
function argsJson(args: readonly unknown[]): string {
    return JSON.stringify(args.map((a) => (a instanceof MassifObject ? a.handle : a)));
}

// ---------------------------------------------------------------------------------------------
// a path-prefixed view
// ---------------------------------------------------------------------------------------------

/**
 * A scope over a path prefix, so `map.group('fogOptions').set('rangeStart', 2.5)` reads short
 * without a named accessor per property - there are 700 of them and the list grows with the SDK.
 */
export class PropertyGroup<C extends ClassName = any> {
    constructor(
        // `any`, not `MassifObject<ClassName>`: the owner's class parameter is unrelated to this
        // group's, and pinning it makes every nested group() unassignable.
        private readonly owner: MassifObject<any>,
        private readonly prefix: string,
        readonly className: C
    ) {}

    /** The full path this group would write, for logging or for a raw call. */
    path(path: string) {
        return this.prefix ? `${this.prefix}.${path}` : path;
    }

    get<P extends ValuePath<C>>(path: P): ValueAt<C, P> {
        return this.owner.get(this.path(path) as never) as ValueAt<C, P>;
    }

    set<P extends WritablePath<C>>(path: P, value: WriteAt<C, P>): this {
        this.owner.set(this.path(path) as never, value as never);
        return this;
    }

    /** Applies several properties in one call, prefixed - so the owner batches them as one. */
    apply(values: Partial<{ [P in WritablePath<C>]: WriteAt<C, P> }>): this {
        const prefixed: { [path: string]: unknown } = {};
        for (const key of Object.keys(values)) {
            prefixed[this.path(key)] = (values as never)[key];
        }
        this.owner.apply(prefixed as never);
        return this;
    }

    getPos(path: string, projection?: ProjectionName) {
        return this.owner.getPos(this.path(path), projection);
    }

    group<P extends ObjectPath<C>>(path: P): PropertyGroup<ClassAtPath<C, P>> {
        return this.owner.group(this.path(path) as never) as never;
    }

    /** @see MassifObject.child */
    child<P extends ObjectPath<C>>(path: P): MassifObject<ClassAtPath<C, P>> | null {
        return this.owner.child(this.path(path) as never) as never;
    }
}

/**
 * What `call` hands back.
 *
 * The generated table says a method's result is a `Handle`, because that is what the C++ verb
 * returns; here it arrives already wrapped, so the caller can read it and destroy it.
 */
export type CallResult<C extends ClassName, M extends MethodName<C>> = [MethodResultClass<C, M>] extends [never] ? MethodResult<C, M> : MassifObject<MethodResultClass<C, M>>;

/** The class of an object result, for the `extract` callback `callAsync` runs. */
export type ResultClass<C extends ClassName, M extends MethodName<C>> = [MethodResultClass<C, M>] extends [never] ? any : MethodResultClass<C, M>;

// ---------------------------------------------------------------------------------------------
// the object
// ---------------------------------------------------------------------------------------------

/**
 * A registered object: a layer, a source, a style, a map's options, an event payload.
 *
 * It is an Observable, so `on`, `once` and `off` are the ones an app already knows. A native
 * subscription is taken when the first handler for an event is added and dropped when the last
 * one goes, so nothing is listening that nobody asked for.
 */
export class MassifObject<C extends ClassName = any> extends Observable {
    /** The subscriptions this object took on the app's behalf, one per event name. */
    private mSubscriptions: { [event: string]: Subscription } = {};
    private mDefaults: SubscribeOptions = {};
    private mDestroyed = false;

    constructor(
        readonly handle: Handle<C>,
        readonly className: C,
        /** The registry id, when the object has one. A call result does not. */
        readonly id?: string
    ) {
        super();
    }

    /**
     * Whether this object is of a class, or of one below it.
     *
     * `instanceof`, for an API that addresses classes by name: `layer.is('massif::RasterTileLayer')`
     * is how an app asks "is this one of the raster ones" without the SDK's proxy classes.
     */
    is(className: ClassName): boolean {
        return isSubclassOf(this.className as string, className as string);
    }

    /** Whether the handle still resolves. False after `destroy`, and after the id was dropped. */
    get valid() {
        return !this.mDestroyed && bridge.isValid(this.handle);
    }

    // --- properties ---------------------------------------------------------------------------

    /**
     * Reads a property.
     *
     * The path may walk object properties (`fogOptions.rangeStart`), into a struct
     * (`clickInfo.clickType`) and into free-form data (`properties.name`). An enum comes back as
     * its constant name, a position as `[lon, lat]`, a struct as the JSON it encodes.
     */
    get<P extends ValuePath<C>>(path: P): ValueAt<C, P> {
        return this.read(path as string) as ValueAt<C, P>;
    }

    private read(path: string): unknown {
        const info = resolvePath(this.className, path);
        switch (info?.code) {
            case 'b':
                return bridge.getBool(this.handle, path, false);
            case 'i':
            case 'c':
                return bridge.getInt(this.handle, path, 0);
            case 'f':
                return bridge.getFloat(this.handle, path, 0);
            case 's':
                return bridge.getString(this.handle, path);
            case 'e':
                return enumName(info.arg, bridge.getInt(this.handle, path, 0));
            case 'p':
                return parseJson(bridge.getPos(this.handle, path, ''));
            case 't':
                return parseJson(bridge.getString(this.handle, path));
            case 'o':
                throw new MassifApiError(`${path} is an object property - reach it with group('${path}'), child('${path}') or read a path through it`);
            default:
                // Unknown to the tables: a path into a Variant, or through an object whose
                // concrete class is more derived than the declared one. The C++ resolves those;
                // read the text and take the value back out of it.
                return parseJson(bridge.getString(this.handle, path));
        }
    }

    /**
     * Writes a property. Every path but an object one takes the JavaScript value directly - an
     * enum by its constant name, a position as an array.
     */
    set<P extends WritablePath<C>>(path: P, value: WriteAt<C, P>): this {
        const result = this.write(path as string, value);
        if (result !== Result.OK) {
            throw new MassifApiError(`${this.className}.${path}: ${resultName(result)}`, result);
        }
        return this;
    }

    /** The same without throwing, for a path that may legitimately not be there. */
    trySet<P extends WritablePath<C>>(path: P, value: WriteAt<C, P>): number {
        return this.write(path as string, value);
    }

    private write(path: string, value: unknown): number {
        const info = resolvePath(this.className, path);
        switch (info?.code) {
            case 'b':
                return bridge.setBool(this.handle, path, !!value);
            case 'i':
            case 'c':
                return bridge.setInt(this.handle, path, value as number);
            case 'f':
                return bridge.setFloat(this.handle, path, value as number);
            case 's':
                return bridge.setString(this.handle, path, String(value));
            case 'e':
                return bridge.setInt(this.handle, path, enumValue(String(value)) ?? Number(value));
            case 'o':
                return isSpec(value)
                    ? this.writeSpec(path, info.arg as string, value)
                    : bridge.setObject(this.handle, path, handleOf(value));
            case 'p':
            case 't':
            case 'v':
                return bridge.setString(this.handle, path, JSON.stringify(value));
            default:
                return this.writeByType(path, value);
        }
    }

    /**
     * An OBJECT property written from an INLINE SPEC, the way a constructor argument takes one.
     *
     * `setObject` carries a handle and nothing else, so a spec used to collapse to NULL_HANDLE -
     * which CLEARS the property and returns OK. Setting `backgroundBitmap` that way blanked the
     * map background and reported success.
     *
     * The built object is registered under an id derived from the target, so it stays alive as
     * long as the property points at it and a second write replaces it rather than leaking.
     */
    private writeSpec(path: string, objectClass: string, spec: SpecObject): number {
        const kind = specKindOf(objectClass);
        if (!kind) {
            return Result.UNKNOWN_CLASS;
        }
        const id = `${this.id ?? this.handle}.${path}`;
        // A different spec under a live id is refused, so the previous one goes first.
        find(kind, id, objectClass as ClassName)?.destroy();
        const built = create(kind, id, spec as never, objectClass as ClassName);
        return bridge.setObject(this.handle, path, built.handle);
    }

    /** The fallback for a path the tables cannot resolve: pick the verb from the value. */
    private writeByType(path: string, value: unknown): number {
        if (typeof value === 'boolean') {
            return bridge.setBool(this.handle, path, value);
        }
        if (typeof value === 'number') {
            return bridge.setFloat(this.handle, path, value);
        }
        if (typeof value === 'string') {
            // An enum constant read as text would parse as 0, which is a real value.
            const asEnum = enumValue(value);
            return asEnum === undefined ? bridge.setString(this.handle, path, value) : bridge.setInt(this.handle, path, asEnum);
        }
        if (value instanceof MassifObject) {
            return bridge.setObject(this.handle, path, value.handle);
        }
        return bridge.setString(this.handle, path, JSON.stringify(value));
    }

    /**
     * Applies several properties in ONE crossing.
     *
     * `set` per key is one JNI or JSI call each, and configuring a layer writes a dozen. The
     * facade takes the whole object as JSON, which is the shape a spec already has.
     *
     * A key whose value is another object - a handle or an inline spec - cannot be JSON, so those
     * go one by one as before. So does everything, on an SDK built before `setAll`.
     */
    apply(values: Partial<{ [P in WritablePath<C>]: WriteAt<C, P> }>): this {
        const setAll = bridge.setAll;
        const batched: { [path: string]: unknown } = {};
        const single: string[] = [];
        for (const key of Object.keys(values)) {
            const value: unknown = (values as never)[key];
            if (!setAll || value instanceof MassifObject || isSpec(value) || resolvePath(this.className, key)?.code === 'o') {
                single.push(key);
            } else {
                batched[key] = value;
            }
        }
        if (setAll && Object.keys(batched).length) {
            const result = setAll(this.handle, JSON.stringify(batched), '');
            // The batch is the fast path; the per-key one is the diagnosable one, so a failure
            // replays it to name the key that failed rather than the object.
            if (result !== Result.OK) {
                single.unshift(...Object.keys(batched));
            }
        }
        for (const key of single) {
            this.set(key as WritablePath<C>, (values as never)[key]);
        }
        return this;
    }

    /**
     * Reads a coordinate.
     *
     * WGS84 unless a projection is named here or on the subscription - degrees, `[lng, lat]`, so
     * app code never repeats a `toWgs84` chain. Pass `'EPSG:3857'` to get the map's own
     * coordinates instead. An object whose source projection is unknown is still left unconverted:
     * a wrong guess is worse than an unconverted number.
     */
    getPos<P extends PositionPath<C>>(path: P, projection?: ProjectionName): PositionAt<C, P> | null;
    /** A path the tables do not carry - the concrete class is more derived than the declared one. */
    getPos(path: string & {}, projection?: ProjectionName): Position | Bounds | null;
    getPos(path: string, projection?: ProjectionName): Position | Bounds | null {
        const json = bridge.getPos(this.handle, path, projection ?? '');
        return json ? (JSON.parse(json) as Position | Bounds) : null;
    }

    /** A scope over an object property, so its own properties read short. */
    group<P extends ObjectPath<C>>(path: P): PropertyGroup<ClassAtPath<C, P>> {
        const info = resolvePath(this.className, path as string);
        if (!info || info.code !== 'o') {
            throw new MassifApiError(`${this.className}.${path} is not an object property`);
        }
        return new PropertyGroup(this, path as string, info.arg as ClassName) as never;
    }

    /**
     * The object an object property points at, as an object the CALLER OWNS - `destroy()` it.
     *
     * `group` reads THROUGH a child; this hands it over, which is what SHARING one needs: an
     * overlay drawing the base map's tiles with a different style points at that source rather
     * than building a second one over the same file.
     *
     * Null when the property is empty.
     */
    child<P extends ObjectPath<C>>(path: P): MassifObject<ClassAtPath<C, P>> | null {
        const info = resolvePath(this.className, path as string);
        if (!info || info.code !== 'o') {
            throw new MassifApiError(`${this.className}.${path} is not an object property`);
        }
        const handle = bridge.getObject(this.handle, path as string);
        return handle ? (new MassifObject(handle as Handle, info.arg as ClassName) as never) : null;
    }

    // --- methods ------------------------------------------------------------------------------

    /**
     * Runs a method. The path may traverse object properties, so
     * `layer.call('tileDecoder.setStyleParameter', 'buildings', 'false')` reaches the decoder
     * without registering it under an id of its own.
     *
     * A result that is an object is handed back as a `MassifObject` the CALLER OWNS - call
     * `destroy()` on it, or it stays registered. Everything else is copied out already.
     */
    call<M extends MethodName<C>>(method: M, ...args: MethodArgs<C, M>): CallResult<C, M> {
        return this.invoke(method as string, args) as CallResult<C, M>;
    }

    /**
     * The untyped form of `call`, for the named wrappers below and in the subclasses.
     *
     * They know which class declares the method they are calling; `call`'s generic does not,
     * because `C` is whatever the caller parameterised the object with.
     */
    protected invoke(method: string, args: readonly unknown[]): unknown {
        const info = resolveMethod(this.className, method);
        let result: number;
        try {
            result = bridge.call(this.handle, method, argsJson(args));
        } catch (e) {
            throw new MassifApiError(`${this.className}.${method}: ${e?.message ?? e}`);
        }
        return this.readResult(result, info?.code, info?.arg, true);
    }

    private readResult(handle: number, code: string | undefined, cls: string | undefined, owned: boolean): unknown {
        if (!handle) {
            return undefined;
        }
        try {
            switch (code) {
                case 'v':
                    return undefined;
                case 'b':
                    return bridge.getBool(handle, '', false);
                case 'i':
                    return bridge.getInt(handle, '', 0);
                case 'f':
                    return bridge.getFloat(handle, '', 0);
                case 's':
                    return bridge.getString(handle, '');
                case 'd':
                    return bridge.getDoubles(handle);
                case 'o': {
                    if (!cls) {
                        // Every object-returning method in the schema names its class; a missing
                        // one means the tables are older than the SDK.
                        throw new MassifApiError('an object result with no class - regenerate the typings with `npm run typings.api`');
                    }
                    // The caller owns it, so it must NOT be destroyed here.
                    owned = false;
                    return new MassifObject(handle as Handle, cls as ClassName);
                }
                default:
                    return parseJson(bridge.getString(handle, ''));
            }
        } finally {
            if (owned) {
                bridge.destroy(handle);
            }
        }
    }

    /**
     * The same on a worker, with the result delivered as an event the promise resolves from.
     *
     * `extract` runs INSIDE the delivery, which is the only place an object result is alive - the
     * facade frees the payload once the handlers have run. Read what you need out of it:
     *
     * ```ts
     * const bytes = await source.callAsync('loadTile', [[8467, 5852, 14]], (tile) => tile.getData('data'));
     * ```
     *
     * A scalar or a flat array is copied out already, so `extract` can be left off for those.
     */
    callAsync<M extends MethodName<C>, R = CallResult<C, M>>(method: M, args: MethodArgs<C, M>, extract?: (result: MassifObject<ResultClass<C, M>>) => R): CancellablePromise<R> {
        const info = resolveMethod(this.className, method as string);
        if (info?.code === 'o' && !extract) {
            // An async object result lives only for the length of the delivery, so handing it
            // back would be a dangling handle rather than a value.
            throw new MassifApiError(`${this.className}.${String(method)} returns an object - pass an extract function, it runs while the result is alive`);
        }
        const event = `${String(method)}.done.${nextCallId()}`;
        let call = 0;
        let subscription: Subscription | undefined;

        const promise = new Promise<R>((resolve, reject) => {
            subscription = this.subscribeRaw(event, (payload) => {
                subscription?.remove();
                if (!payload) {
                    reject(new MassifApiError(`${this.className}.${String(method)} failed`));
                    return false;
                }
                try {
                    // Never owned: the facade releases an async payload after the handlers run.
                    const value = info?.code === 'o' || extract ? new MassifObject(payload as Handle, (info?.arg ?? this.className) as ClassName) : null;
                    resolve(extract ? extract(value as never) : (this.readResult(payload, info?.code, info?.arg, false) as R));
                } catch (e) {
                    reject(e);
                }
                return false;
            });
            try {
                call = bridge.callAsync(this.handle, method as string, argsJson(args), event);
            } catch (e) {
                subscription?.remove();
                reject(new MassifApiError(`${this.className}.${String(method)}: ${e?.message ?? e}`));
            }
        }) as CancellablePromise<R>;

        promise.cancel = () => {
            const cancelled = call ? bridge.cancelCall(call) : false;
            subscription?.remove();
            return cancelled;
        };
        return promise;
    }

    /** Cancels every queued or running async call on this object. */
    cancelCalls(): number {
        return bridge.cancelCalls(this.handle);
    }

    /**
     * Reads a collection one element at a time, which is the only channel the facade has for one.
     *
     * The defaults cover a search result; a route's instructions are
     * `collect(fn, { countPath: 'instructionCount', method: 'getInstruction' })`.
     */
    collect<R>(map: (element: MassifObject<ResultClass<C, MethodName<C> & 'getFeature'>>, index: number) => R): R[];
    collect<M extends MethodName<C>, R>(map: (element: MassifObject<ResultClass<C, M>>, index: number) => R, options: { countPath: string; method: M }): R[];
    collect<R>(map: (element: any, index: number) => R, options: { countPath?: string; method?: string } = {}): R[] {
        const countPath = options.countPath ?? 'featureCount';
        const method = options.method ?? 'getFeature';
        const count = bridge.getInt(this.handle, countPath, 0);
        const out: R[] = [];
        for (let i = 0; i < count; i++) {
            const element = this.invoke(method, [i]) as MassifObject;
            if (!element) {
                continue;
            }
            try {
                out.push(map(element, i));
            } finally {
                element.destroy();
            }
        }
        return out;
    }

    /**
     * Anything, as JSON.
     *
     * The escape hatch for a path the tables cannot type - a key inside a feature's free-form
     * properties, or a class more derived than the one this object was wrapped as.
     */
    json(path = ''): Json {
        return parseJson(bridge.getString(this.handle, path));
    }

    /** A blob, without turning it into a string. An empty path means the handle IS the blob. */
    getData(path = ''): ArrayBuffer | null {
        return bridge.getData(this.handle, path);
    }

    /** A bulk numeric result - a route's path, an elevation profile - flat and in one crossing. */
    getDoubles(): number[] {
        return bridge.getDoubles(this.handle);
    }

    // --- events -------------------------------------------------------------------------------

    /** The delivery options every later subscription on this object starts from. */
    eventOptions(options: SubscribeOptions): this {
        this.mDefaults = { ...this.mDefaults, ...options };
        return this;
    }

    /** Every facade event this object answers to, its own and its bases'. */
    get events(): string[] {
        return eventNames(this.className);
    }

    /**
     * Subscribes with options of its own, and hands back the subscription to remove.
     *
     * `on` is the Observable form and covers most cases; this is for a handler that needs a
     * different projection or delivery from the object's default.
     */
    subscribe<E extends EventName<C>>(event: E, handler: (data: MassifEventData<C, E>) => void, options?: SubscribeOptions): Subscription {
        const consumable = this.consumes(event as string);
        return this.subscribeRaw(
            event as string,
            (payload, snapshot) => {
                const data = this.eventData(event as string, payload, consumable, snapshot) as unknown as MassifEventData<C, E>;
                handler(data);
                warnIfIgnored(data, consumable, event as string);
                return consumable && data.consumed;
            },
            options
        );
    }

    /** Whether `consumed` will be honoured for this subscription. */
    private consumes(event: string): boolean {
        return bridge.canConsume && !!findEvent(this.className, event)?.consume;
    }

    private subscribeRaw(event: string, handler: (payload: number, snapshot?: Json) => boolean, options?: SubscribeOptions): Subscription {
        requireApi();
        this.beforeSubscribe(event);
        const merged = { ...this.mDefaults, ...options };
        const throttle = merged.throttle ?? 0;
        const debounce = merged.debounce ?? 0;
        const consumable = this.consumes(event);
        if (debounce && consumable) {
            throw new MassifApiError(`${event} is consumable, so it cannot be debounced - the SDK is waiting for the answer now, not in ${debounce} ms`);
        }
        let last = 0;
        let timer: any = null;

        const deliver = (payload: number) => {
            if (throttle) {
                // Date.now() rather than a timer: dropping is the point, and a queued handler
                // would read a payload the facade has already freed.
                const now = Date.now();
                if (now - last < throttle) {
                    return false;
                }
                last = now;
            }
            if (debounce) {
                // The payload dies when this returns, so the delayed handler gets a snapshot read
                // NOW. Trailing edge: each event replaces the pending one, and only the last of a
                // burst is delivered.
                const snapshot = payload ? parseJson(bridge.getString(payload as Handle, '')) : null;
                if (timer) {
                    clearTimeout(timer);
                }
                timer = setTimeout(() => {
                    timer = null;
                    try {
                        handler(0, snapshot);
                    } catch (error) {
                        console.error(`MassifMaps: debounced handler for '${event}' threw`, error);
                    }
                }, debounce);
                return false;
            }
            return handler(payload);
        };

        const id = bridge.on(
            this.handle,
            event,
            (_target, _event, payload) => {
                try {
                    return deliver(payload);
                } catch (error) {
                    // This returns through a SWIG director into C++, and an exception crossing
                    // that boundary aborts the PROCESS rather than unwinding it. A handler that
                    // throws must not take the app down with it.
                    console.error(`MassifMaps: handler for '${event}' threw`, error);
                    return false;
                }
            },
            DELIVERY_ORIGIN,
            false,
            merged.projection ?? '',
            consumable
        );
        if (!id) {
            throw new MassifApiError(`could not subscribe to ${event} on ${this.className} - a stale handle, or an unknown projection`);
        }
        const subscription = new Subscription(id);
        if (debounce) {
            subscription.onRemove(() => {
                if (timer) {
                    clearTimeout(timer);
                    timer = null;
                }
            });
        }
        return subscription;
    }

    /** Hook for a subclass that has to install a native listener before the first subscription. */
    protected beforeSubscribe(event: string) {
        // nothing by default
    }

    private eventData(event: string, payload: number, consumable: boolean, snapshot?: Json): MassifEventData {
        const info = findEvent(this.className, event);
        // A debounced delivery has no live payload left - see SubscribeOptions.debounce - so its
        // reads come out of the snapshot taken at emit time.
        const fromSnapshot = snapshot !== undefined && snapshot !== null;
        // Wrapped lazily: an event nobody reads should not allocate an Observable per delivery.
        let object: MassifObject | null | undefined;
        const payloadObject = () => {
            if (object === undefined) {
                object = payload && info?.payload ? new MassifObject(payload as Handle, info.payload as ClassName) : null;
            }
            return object;
        };
        // Only when a payload actually ARRIVED, not merely when the schema says the event has
        // one: an older SDK sends none, and a getter that read through would throw.
        const proto = (payload || fromSnapshot) && info?.payload ? payloadProto(info.payload) : null;
        const data = Object.assign(proto ? Object.create(proto) : {}, {
            eventName: event,
            object: this as MassifObject,
            consumed: false,
            consumable,
            get: (path: string) => {
                if (fromSnapshot) {
                    return path ? (snapshot as any)[path] : snapshot;
                }
                const target = payloadObject();
                if (!target) {
                    throw new MassifApiError(`${event} carries no payload`);
                }
                return target.get(path as never);
            },
            getPos: (path: string, projection?: ProjectionName) => {
                if (fromSnapshot) {
                    return ((snapshot as any)[path] ?? null) as never;
                }
                const target = payloadObject();
                return target ? target.getPos(path, projection) : null;
            }
        }) as MassifEventData;
        // `payload` is a getter of its own so the wrapper is still only built if it is asked for.
        Object.defineProperty(data, 'payload', { enumerable: true, configurable: true, get: payloadObject });
        return data;
    }

    /**
     * Observable's `on`, with the event name and the payload typed.
     *
     * ```ts
     * layer.on('vectortile.clicked', (e) => console.log(e.get('featureLayerName')));
     * ```
     *
     * Deliberately NOT widened to `string`: a typo in an event name is the failure this whole
     * layer exists to catch, and `addEventListener` is still there for a name of your own.
     */
    on<E extends EventName<C>>(event: E, callback: (data: MassifEventData<C, E>) => void, thisArg?: any): void {
        super.on(event, callback as never, thisArg);
    }

    once<E extends EventName<C>>(event: E, callback: (data: MassifEventData<C, E>) => void, thisArg?: any): void {
        super.once(event, callback as never, thisArg);
    }

    off<E extends EventName<C>>(event: E, callback?: any, thisArg?: any): void {
        super.off(event, callback, thisArg);
    }

    /**
     * Observable's own hook. A facade event takes its native subscription here, on the first
     * handler, so nothing is listening that nobody asked for.
     */
    addEventListener(eventNames: string, callback: (data: EventData) => void, thisArg?: any, ...rest: any[]) {
        (super.addEventListener as any)(eventNames, callback, thisArg, ...rest);
        for (const name of splitEvents(eventNames)) {
            if (!this.mSubscriptions[name] && findEvent(this.className, name) !== null) {
                const consumable = this.consumes(name);
                this.mSubscriptions[name] = this.subscribeRaw(name, (payload, snapshot) => {
                    // One data object for every listener, so whichever of them sets `consumed`
                    // claims the event - the same rule as a DOM handler calling preventDefault.
                    const data = this.eventData(name, payload, consumable, snapshot);
                    this.notify(data);
                    warnIfIgnored(data, consumable, name);
                    return consumable && data.consumed;
                });
            }
        }
    }

    removeEventListener(eventNames: string, callback?: any, thisArg?: any) {
        super.removeEventListener(eventNames, callback, thisArg);
        for (const name of splitEvents(eventNames)) {
            if (this.mSubscriptions[name] && !this.hasListeners(name)) {
                this.mSubscriptions[name].remove();
                delete this.mSubscriptions[name];
            }
        }
    }

    // --- lifetime -----------------------------------------------------------------------------

    /**
     * Drops the id and the context's reference to the object. Subscriptions and pending async
     * calls die with it, which is what stops a handler running against a freed object.
     */
    destroy(): boolean {
        if (this.mDestroyed) {
            return false;
        }
        this.mDestroyed = true;
        for (const name of Object.keys(this.mSubscriptions)) {
            this.mSubscriptions[name].remove();
        }
        this.mSubscriptions = {};
        return bridge.destroy(this.handle);
    }

    toString() {
        return `${this.constructor.name}(${this.className}#${this.handle}${this.id ? ` "${this.id}"` : ''})`;
    }
}

function splitEvents(eventNames: string): string[] {
    return eventNames
        .split(',')
        .map((n) => n.trim())
        .filter(Boolean);
}

let callId = 0;
function nextCallId() {
    return ++callId;
}

const warnedConsume: { [event: string]: boolean } = {};

/** Says once, per event, that a handler claimed something nothing will act on. */
function warnIfIgnored(data: { consumed: boolean }, consumable: boolean, event: string) {
    if (!data.consumed || consumable || warnedConsume[event]) {
        return;
    }
    warnedConsume[event] = true;
    const why = bridge.canConsume ? 'it is not a consumable event' : "this SDK's MassifApi.on does not take the consume flag";
    console.warn(`massif: a handler consumed ${event} but nothing will act on it - ${why}`);
}

function resultName(result: number): string {
    for (const name of Object.keys(Result)) {
        if ((Result as never)[name] === result) {
            return name;
        }
    }
    return `result ${result}`;
}

// ---------------------------------------------------------------------------------------------
// layers and sources
// ---------------------------------------------------------------------------------------------

/**
 * A layer.
 *
 * Its click events need a listener installed on the NATIVE layer, and that is done on the first
 * subscription rather than at creation - an app that never listens pays nothing, and the listener
 * chains whatever was already there.
 */
export class MassifLayer<C extends ClassName = any> extends MassifObject<C> {
    private mBridged: { [event: string]: boolean } = {};

    /** Set when the layer was built or adopted through a map; needed to reorder or detach it. */
    map?: MassifMap;

    protected beforeSubscribe(event: string) {
        if (this.mBridged[event] || !this.id) {
            return;
        }
        const native = bridge.getNativeLayer(this.id);
        if (!native) {
            return;
        }
        if (event === 'vectortile.clicked') {
            bridge.attachVectorTileEvents(native, this.handle);
        } else if (event === 'vectorelement.clicked') {
            bridge.attachVectorElementEvents(native, this.handle);
        } else {
            return;
        }
        this.mBridged[event] = true;
    }

    /** The native layer, to hand back to the object API - by id, or by handle when it has none. */
    get native(): any {
        return this.id ? bridge.getNativeLayer(this.id) : bridge.getNativeLayerByHandle(this.handle);
    }

    // --- the handful of properties every app touches ------------------------------------------
    //
    // A named accessor per property is a non-goal - there are 700 and the list grows with the
    // SDK, which is what `set`/`apply`/`group` are for. These are the closed set the Java sugar
    // settled on, in the same read/write shape.

    opacity(): number;
    opacity(value: number): this;
    opacity(value?: number) {
        return value === undefined ? (this.get('opacity' as never) as number) : this.set('opacity' as never, value as never);
    }

    visible(): boolean;
    visible(value: boolean): this;
    visible(value?: boolean) {
        return value === undefined ? (this.get('visible' as never) as boolean) : this.set('visible' as never, value as never);
    }

    /** The zoom band the layer draws in, as `[min, max]`. */
    zoomRange(): [number, number];
    zoomRange(range: [number, number]): this;
    zoomRange(range?: [number, number]) {
        return range === undefined ? (this.get('visibleZoomRange' as never) as [number, number]) : this.set('visibleZoomRange' as never, range as never);
    }

    // --- placement ----------------------------------------------------------------------------

    /** Moves the layer within the map's stack. 0 is the bottom. */
    moveTo(index: number): this {
        this.requireMap().layers().insert(index, this);
        return this;
    }

    /** Takes it off the map. The object stays registered until it is destroyed. */
    detach(): this {
        this.requireMap().layers().remove(this);
        return this;
    }

    // --- the layer methods, by name -----------------------------------------------------------

    refresh(): this {
        this.invoke('refresh', []);
        return this;
    }

    /**
     * The layer's data source, as an object the CALLER OWNS.
     *
     * `child('dataSource')` typed as a source, so `.native` is there: handing the base map's tiles
     * to the app's own native code is the one thing the facade cannot express, and this is the
     * escape hatch for it.
     */
    source(): MassifSource | null {
        const child = this.child('dataSource' as never);
        return child ? new MassifSource(child.handle, child.className) : null;
    }

    /** Drops this layer's tiles. `all` includes the preloading caches, not just the visible set. */
    clearTileCaches(all = false): this {
        this.invoke('clearTileCaches', [all]);
        return this;
    }

    /**
     * Elevations under a set of positions, flat and in one crossing - a profile over a track is
     * thousands of numbers and neither JSON nor a per-element proxy is an acceptable way to move
     * them. Only a hillshade layer answers; anything else gives an empty array.
     */
    elevations(positions: AnyPosition[]): number[] {
        try {
            return this.invoke('getElevations', [positions.map(toPosition)]) as number[];
        } catch (e) {
            return [];
        }
    }

    // --- clicks, by name ----------------------------------------------------------------------

    // The payload is named OUTRIGHT rather than as MassifEventData<C, EventName<C>>: each of
    // these subscribes to ONE event, and a layer whose class is not narrowed - the common case,
    // since `layers().get(i)` and `child('dataSource')` hand back a bare `MassifLayer` - resolved
    // that to MassifEventData<any, string>, which has no payload fields on it at all.

    /** `on('vectortile.clicked', …)`, named. Set `e.consumed` to claim the click. */
    onFeatureClick(handler: (data: MassifEventData<'massif::VectorTileLayer', 'vectortile.clicked'>) => void, options?: SubscribeOptions): Subscription {
        return this.subscribe('vectortile.clicked' as EventName<C>, handler as never, options);
    }

    /** `on('vectorelement.clicked', …)`, named - a marker or a popup the app added. */
    onElementClick(handler: (data: MassifEventData<'massif::VectorLayer', 'vectorelement.clicked'>) => void, options?: SubscribeOptions): Subscription {
        return this.subscribe('vectorelement.clicked' as EventName<C>, handler as never, options);
    }

    private requireMap(): MassifMap {
        if (!this.map) {
            throw new MassifApiError(`layer is not attached to a map: ${this}`);
        }
        return this.map;
    }
}

export class MassifSource<C extends ClassName = any> extends MassifObject<C> {
    /** The native source, to hand back to the object API - by id, or by handle when it has none. */
    get native(): any {
        return this.id ? bridge.getNativeSource(this.id) : bridge.getNativeSourceByHandle(this.handle);
    }

    /**
     * One tile, BLOCKING - an HTTP source fetches it on the calling thread.
     * `loadTileAsync` is the one to use from the UI thread.
     */
    loadTile(tile: Tile): ArrayBuffer | null {
        const result = this.invoke('loadTile', [tile]) as MassifObject;
        try {
            return result ? result.getData('data') : null;
        } finally {
            result?.destroy();
        }
    }

    /**
     * Declares a layer inside a `geojson` source and returns its index.
     *
     * The source re-tiles whatever it is given, so replacing the document later is one
     * `setGeoJSON` rather than rebuilding the layer above it.
     */
    createLayer(name: string): number {
        return this.invoke('createLayer', [name]) as number;
    }

    /**
     * Replaces a declared layer's document. The tiles are rebuilt on the tile thread, so this
     * costs the re-encode rather than a layer rebuild.
     *
     * A string is parsed here rather than passed through: the argument crosses as JSON already,
     * and sending text would arrive as a string-valued argument instead of a document.
     */
    setGeoJSON(layer: number, geojson: Json | string | object): this {
        this.invoke('setLayerGeoJSON', [layer, typeof geojson === 'string' ? JSON.parse(geojson) : geojson]);
        return this;
    }

    /** Drops a declared layer and everything in it. */
    deleteLayer(layer: number): this {
        this.invoke('deleteLayer', [layer]);
        return this;
    }

    /**
     * One feature at a time, for an app that edits what it is showing.
     *
     * `setGeoJSON` re-encodes and re-tiles the whole document; on a few hundred saved routes that is
     * the difference between instant and not. `update` matches on the feature's own `id`.
     */
    addFeature(layer: number, feature: Json | string | object): this {
        this.invoke('addFeature', [layer, feature]);
        return this;
    }

    updateFeature(layer: number, feature: Json | string | object): this {
        this.invoke('updateFeature', [layer, feature]);
        return this;
    }

    removeFeature(layer: number, id: string | number): this {
        this.invoke('removeFeature', [layer, id]);
        return this;
    }

    /** The same on a worker. The bytes are copied out while the result is still alive. */
    loadTileAsync(tile: Tile): CancellablePromise<ArrayBuffer | null> {
        return this.callAsync('loadTile' as never, [tile] as never, ((result: MassifObject) => result.getData('data')) as never) as CancellablePromise<ArrayBuffer | null>;
    }
}

// ---------------------------------------------------------------------------------------------
// the registry
// ---------------------------------------------------------------------------------------------

const WRAPPERS: { [kind: string]: new (handle: Handle, className: ClassName, id?: string) => MassifObject } = {
    layer: MassifLayer as never,
    source: MassifSource as never
};

function wrapperFor(kind: string) {
    return WRAPPERS[kind] ?? MassifObject;
}

/**
 * Builds an object from a spec and registers it under a kind and an id.
 *
 * The spec's keys are the constructor's parameters plus any writable property of the class it
 * builds; a nested `source`, `style` or `assets` is either an inline spec or the id something was
 * registered under. Creating an id that already exists with an IDENTICAL spec returns the same
 * object, which is how two maps share one source without coordinating - a DIFFERENT spec under
 * that id is refused rather than silently replacing it.
 *
 * ```ts
 * const layer = api.create('layer', 'base', {
 *     type: 'vector',
 *     opacity: 0.8,
 *     source: { type: 'http', minZoom: 0, maxZoom: 14, url: 'https://…/{z}/{x}/{y}.pbf' },
 *     style: { type: 'mbvt', cartocss: { type: 'cartocss', css: '#water{polygon-fill:#00f;}' } }
 * });
 * ```
 */
export function create<K extends Kind, T extends SpecType<K>>(kind: K, id: string, spec: SpecArg<K, T>): MassifObject<ClassOfSpec<K, T>>;
/**
 * The escape hatch for the kinds built by a HAND-WRITTEN factory, which the schema cannot
 * describe: `projection` (a name registry lookup), `data` (bytes from a URL), `geometry` (a
 * GeoJSON reader), `search`/`routing` requests.
 *
 * `className` is REQUIRED, and not only so the paths resolve: without it this overload also
 * matches a misspelt key in a kind the schema DOES describe, and the typo would be accepted
 * here instead of reported by the overload above.
 */
export function create<C extends ClassName>(kind: string, id: string, spec: { type: string } & { [key: string]: Json }, className: C): MassifObject<C>;
export function create(kind: string, id: string, spec: { type: string }, className?: ClassName): MassifObject {
    requireApi();
    let handle: number;
    try {
        handle = bridge.create(kind, id, JSON.stringify(spec));
    } catch (e) {
        throw new MassifApiError(`create ${kind}:${id}: ${e?.message ?? e}`);
    }
    if (!handle) {
        throw new MassifApiError(`create ${kind}:${id} returned no handle`);
    }
    // A kind the schema describes names its class; one with a hand-written factory does not, so
    // the caller may say - and a bare Layer is the least wrong fallback for a walk that has to
    // keep its footing rather than refuse.
    const resolved = className ?? classOfSpec(kind, spec.type) ?? 'massif::Layer';
    return new (wrapperFor(kind))(handle as Handle, resolved as ClassName, id) as never;
}

/** `create('layer', …)`, typed as a layer so its click events are there. */
export function createLayer<T extends SpecType<'layer'>>(id: string, spec: SpecArg<'layer', T>): MassifLayer<ClassOfSpec<'layer', T>> {
    return create('layer', id, spec) as never;
}

/** `create('source', …)`, typed as a source. */
export function createSource<T extends SpecType<'source'>>(id: string, spec: SpecArg<'source', T>): MassifSource<ClassOfSpec<'source', T>> {
    return create('source', id, spec) as never;
}

/**
 * The object registered under a kind and an id, or null.
 *
 * The class cannot be read back off a handle, so pass it when it is not one this plugin created
 * in the same session - it is what the property paths resolve against.
 */
export function find<C extends ClassName>(kind: Kind | string, id: string, className: C): MassifObject<C> | null {
    requireApi();
    const handle = bridge.findObject(kind, id);
    if (!handle) {
        return null;
    }
    return new (wrapperFor(kind))(handle as Handle, className, id) as never;
}

/** Drops an id and the context's reference to the object behind it. */
export function destroy(kind: Kind | string, id: string): boolean {
    return bridge.available ? bridge.unregisterObject(kind, id) : false;
}

/**
 * Wraps a handle the app already has - a payload kept for the length of a handler, a result from
 * a raw call. Nothing is checked; the class is what the paths resolve against.
 */
export function wrap<C extends ClassName>(handle: number, className: C, id?: string): MassifObject<C> {
    return new MassifObject(handle as Handle<C>, className, id);
}

/**
 * The same, as a LAYER - so `opacity()`, `visible()` and `zoomRange()` are there.
 *
 * For a layer the app did not build and cannot reach by id: a CompositeVectorTileLayer's external
 * children are owned by their parent, and `getExternalChildLayer` hands back a plain handle. Those
 * three are get/set on a property, so they work on a layer that is not in a map's stack; `moveTo`
 * and `detach` are the ones that need one, and they throw as they would on any unattached layer.
 */
export function wrapLayer<C extends ClassName>(handle: number, className: C, id?: string): MassifLayer<C> {
    return new MassifLayer(handle as Handle<C>, className, id);
}

/**
 * Gives an object built with the OBJECT api an id, and with it properties, methods and events.
 *
 * This is how an app moves over a piece at a time rather than rebuilding its map. The class is
 * read off the native object's runtime class, so an adopted `VectorTileLayer` answers to a vector
 * tile layer's properties rather than only to `Layer`'s.
 *
 * ```ts
 * const base = api.adoptLayer('base', myVectorTileLayer.getNative());
 * base.on('vectortile.clicked', (e) => console.log(e.get('featureLayerName')));
 * ```
 */
export function adoptLayer(id: string, nativeLayer: any, className?: ClassName): MassifLayer {
    requireApi();
    const handle = bridge.adoptLayer('layer', id, nativeLayer);
    if (!handle) {
        throw new MassifApiError(`could not adopt layer "${id}" - the id is taken, or the class is not a wrapped one`);
    }
    return new MassifLayer(handle as Handle, resolveNativeClass(nativeLayer, className, 'massif::Layer'), id);
}

/** @copydoc adoptLayer */
export function adoptSource(id: string, nativeSource: any, className?: ClassName): MassifSource {
    requireApi();
    const handle = bridge.adoptSource('source', id, nativeSource);
    if (!handle) {
        throw new MassifApiError(`could not adopt source "${id}" - the id is taken, or the class is not a wrapped one`);
    }
    return new MassifSource(handle as Handle, resolveNativeClass(nativeSource, className, 'massif::TileDataSource'), id);
}

/**
 * Adopts an asset package - including one this app SUBCLASSED, which is the case a spec cannot
 * express. Any spec taking an `assets` key then resolves the id instead of naming a type:
 *
 * ```ts
 * adoptAssets('shared', myAssetPackage.getNative());
 * style('osm', { type: 'cartocss', css, assets: 'shared' });
 * ```
 *
 * A Swig director - which is what a JavaScript or Java subclass of AssetPackage is - has no class
 * the SDK's registry knows, so it is adopted as `massif::AssetPackage`. That is the class the
 * `assets` key requires, so nothing is lost; pass `className` for a concrete SDK package whose own
 * properties are wanted.
 */
export function adoptAssets(id: string, nativeAssets: any, className?: ClassName): MassifObject {
    requireApi();
    const handle = bridge.adoptAssets('assets', id, nativeAssets);
    if (!handle) {
        throw new MassifApiError(`could not adopt assets "${id}" - the id is taken, or the class is not a wrapped one`);
    }
    return new MassifObject(handle as Handle, resolveNativeClass(nativeAssets, className, 'massif::AssetPackage'), id);
}

function resolveNativeClass(nativeObject: any, given: ClassName | undefined, fallback: ClassName): ClassName {
    if (given) {
        if (!isKnownClass(given)) {
            throw new MassifApiError(`${given} is not a class the typings know - regenerate them with \`npm run typings.api\``);
        }
        return given;
    }
    return (classOfShortName(bridge.nativeShortClassName(nativeObject)) as ClassName) ?? fallback;
}

// ---------------------------------------------------------------------------------------------
// markers and popups
// ---------------------------------------------------------------------------------------------

/**
 * The map's own markers and popups.
 *
 * An element AND its style are both a spec, so an app that wants a bigger pin changes a number
 * in JSON rather than reaching for a style builder. The layer and the source behind this are
 * created on first use and released with the map - `layer()` reaches the layer, so ordering,
 * opacity and visibility are the same properties as on any other.
 *
 * ```ts
 * const pin = map.elements().style('pin', { type: 'marker', size: 26, color: 0xffe5484d });
 * map.addMarker({ type: 'marker', position: [6.865, 45.832], style: 'pin' });
 * ```
 */
export class MassifElements {
    private mSource?: MassifObject<'massif::LocalVectorDataSource'>;
    private mLayer?: MassifLayer<'massif::VectorLayer'>;
    private mCounter = 0;

    constructor(
        private readonly map: MassifMap,
        private readonly id: string,
        /** Where the positions are read. lon/lat by default, matching the rest of this API. */
        private readonly sourceSpec?: SpecArg<'source', 'local'>
    ) {}

    /**
     * Adds an element - a `marker`, a `balloon`. The spec's `style` either names one registered
     * earlier or carries it inline, which is what most apps write.
     *
     * @returns the element, for `remove`. Its properties read and write by path like any other.
     */
    add<T extends SpecType<'element'>>(spec: SpecArg<'element', T>): MassifObject<ClassOfSpec<'element', T>> {
        const element = this.map.object('element', `${this.id}.el${++this.mCounter}`, spec);
        this.built().source.call('add', element.handle);
        return element;
    }

    /**
     * Registers a style under an id so many elements share ONE style object - which is what
     * matters once there are thousands of them.
     */
    style<T extends SpecType<'elementstyle'>>(styleId: string, spec: SpecArg<'elementstyle', T>): MassifObject<ClassOfSpec<'elementstyle', T>> & { readonly id: string } {
        // `id` narrowed to non-optional: a style is only useful as the `style` key of an element
        // spec, and an optional string would make every call site write `!`.
        return this.map.object('elementstyle', `${this.id}.${styleId}`, spec) as never;
    }

    /** Removes one element. It stays registered until it is destroyed. */
    remove(element: MassifObject | null): boolean {
        return this.mSource && element ? this.mSource.call('remove', element.handle) : false;
    }

    /** Removes every element this has added. */
    clear(): this {
        this.mSource?.call('clear');
        return this;
    }

    /**
     * Clicks on the elements themselves, with the element's position on the payload.
     *
     * Set `e.consumed` in the handler to claim the tap, which is what an app wants whenever
     * "tap a marker" and "tap the map" mean different things - otherwise one tap does both.
     */
    onClick(handler: (data: MassifEventData<'massif::VectorLayer', 'vectorelement.clicked'>) => void, options?: SubscribeOptions): Subscription {
        return this.built().layer.subscribe('vectorelement.clicked', handler, options);
    }

    /** The layer they are drawn on, for opacity, visibility and ordering. */
    layer(): MassifLayer<'massif::VectorLayer'> {
        return this.built().layer;
    }

    /** The source holding them, for anything else a local source reaches. */
    source(): MassifObject<'massif::LocalVectorDataSource'> {
        return this.built().source;
    }

    /** Built on first use, so a map that never adds an element pays nothing. */
    private built() {
        if (!this.mSource || !this.mLayer) {
            // The projection is a NESTED SPEC, not an id: a string here is looked up in the
            // registry, and a well-known name like EPSG:4326 is a type the projection factory
            // builds, not something registered under that id.
            this.mSource = this.map.object('source', this.id, this.sourceSpec ?? { type: 'local', projection: { type: 'EPSG:4326' } });
            this.mLayer = this.map.addLayer(this.id, { type: 'elements', source: this.id });
        }
        return { source: this.mSource, layer: this.mLayer };
    }
}

// ---------------------------------------------------------------------------------------------
// the map
// ---------------------------------------------------------------------------------------------

/**
 * The subset of this plugin's MassifMap view the surface API needs.
 *
 * Structural rather than the class itself, so `api/` does not import `ui/` - the view already
 * imports half the plugin and the surface API has no reason to.
 */
/**
 * The subset of the plugin's own map view this API needs.
 *
 * SHORT on purpose. The camera used to be declared here in full and delegated to - which meant
 * every camera call went through a MapPos proxy and could not be reproduced from the C ABI. It
 * goes through the facade now, so what is left is the native handles to adopt and the view's
 * pixel size, which is a property of the view and of nothing else.
 */
export interface MapViewLike {
    mapView: any;
    getOptions(): { getNative(): any } | null;
    getMeasuredWidth?(): number;
    getMeasuredHeight?(): number;
    /** A screenshot of the map. Platform code - there is no facade verb for a framebuffer read. */
    captureRendering?(wait?: boolean): Promise<any>;
    /** Asks for a frame. Also platform code: the renderer is not in the property table. */
    requestRedraw?(): void;
}

/**
 * The SDK's logging switches.
 *
 * `Log` has no instance - its properties are static - so the context registers it under
 * `static:Log` and this is the handle onto it. `log().apply({ showDebug: true, … })`.
 */
export function log(): MassifObject<'massif::Log'> {
    return find('static', 'Log', 'massif::Log')!;
}

/**
 * The camera.
 *
 * Every call is a `call` or a property read on the ADOPTED MAP VIEW - the facade, not the
 * plugin's own view wrapper. That is the point of putting the camera on the facade
 * (massif-maps/MassifMaps#159): the same code works for React Native and WASM, and no position
 * ever becomes a `MapPos` proxy on the way through.
 *
 * Positions are `[lng, lat]` in WGS84, the facade's own format. Set the duration once with
 * `animate` and every move after it uses that:
 *
 * ```ts
 * map.camera().animate(400).moveTo([5.72, 45.18], { zoom: 14, tilt: 45 });
 * ```
 */
export class MapCamera {
    private mDuration = 0;

    /**
     * @param view The ADOPTED map view - every call below is a facade call on its handle.
     * @param size The view's pixel size, for the fitBounds default. A size is a property of the
     *        VIEW, not of the camera, and BaseMapView does not expose one - so the map, which
     *        holds the NativeScript view, measures it.
     */
    constructor(
        private readonly view: MassifObject<'massif::BaseMapView'>,
        private readonly size: () => { width: number; height: number }
    ) {}

    /** Milliseconds every later move animates over. 0 is immediate, and is the default. */
    animate(ms: number): this {
        this.mDuration = ms;
        return this;
    }

    /** Seconds, which is what the facade takes; the public API is milliseconds. */
    private take(duration?: number): number {
        const ms = duration ?? this.mDuration;
        this.mDuration = 0;
        return ms / 1000;
    }

    position(): Position;
    position(value: AnyPosition): this;
    position(value?: AnyPosition) {
        if (value === undefined) {
            return this.view.getPos('focusPos') as Position;
        }
        return this.moveTo(value);
    }

    /** Where the camera IS, not what it looks at - kilometres apart at a low tilt. */
    eyePosition(): Position {
        return this.view.getPos('cameraPos') as Position;
    }

    zoom(): number;
    zoom(value: number, target?: AnyPosition): this;
    zoom(value?: number, target?: AnyPosition) {
        if (value === undefined) {
            return this.view.get('zoom') as number;
        }
        return this.moveTo(target ?? this.position(), { zoom: value });
    }

    rotation(): number;
    rotation(degrees: number): this;
    rotation(degrees?: number) {
        if (degrees === undefined) {
            return this.view.get('rotation') as number;
        }
        return this.moveTo(this.position(), { rotation: degrees });
    }

    tilt(): number;
    tilt(degrees: number): this;
    tilt(degrees?: number) {
        if (degrees === undefined) {
            return this.view.get('tilt') as number;
        }
        return this.moveTo(this.position(), { tilt: degrees });
    }

    /**
     * Position, zoom, rotation and tilt in ONE move.
     *
     * Four separate setters animate independently and visibly fight each other, which is the
     * whole reason this exists rather than a chain of the accessors above. With no duration it
     * is immediate, and works before the map has drawn - which is when a screen usually points
     * its camera.
     */
    moveTo(position: AnyPosition, options: { zoom?: number; rotation?: number; tilt?: number; climbHeight?: number; duration?: number } = {}): this {
        const pos = toPosition(position);
        const zoom = options.zoom ?? this.zoom();
        const rotation = options.rotation ?? this.rotation();
        const tilt = options.tilt ?? this.tilt();
        const seconds = this.take(options.duration);
        if (seconds > 0) {
            this.view.call('flyTo', pos, zoom, rotation, tilt, options.climbHeight ?? 0, seconds);
        } else {
            this.view.call('moveTo', pos, zoom, rotation, tilt);
        }
        return this;
    }

    /** Frames a `[min, max]` box. `screen` defaults to the whole view. */
    fitBounds(
        bounds: Bounds,
        options: { screen?: { min: { x: number; y: number }; max: { x: number; y: number } }; integerZoom?: boolean; resetRotation?: boolean; resetTilt?: boolean; duration?: number } = {}
    ): this {
        const { width, height } = this.size();
        const screen = options.screen
            ? [[options.screen.min.x, options.screen.min.y], [options.screen.max.x, options.screen.max.y]]
            : [[0, 0], [width, height]];
        this.view.call(
            'fitBounds',
            [toPosition(bounds[0]), toPosition(bounds[1])],
            screen,
            !!options.integerZoom,
            !!options.resetRotation,
            !!options.resetTilt,
            this.take(options.duration)
        );
        return this;
    }

    /** Whether a flight is running. */
    isMoving(): boolean {
        return this.view.get('flightActive') as boolean;
    }

    /** 0 to 1 through the current flight, or -1 when none is running. */
    progress(): number {
        return this.view.get('flightProgress') as number;
    }

    stop(): this {
        this.view.call('stopFlight');
        return this;
    }

    /** Screen point to map position. */
    screenToMap(x: number, y: number): Position {
        return this.view.call('screenToMap', x, y) as Position;
    }

    mapToScreen(position: AnyPosition): { x: number; y: number } {
        const point = this.view.call('mapToScreen', toPosition(position)) as [number, number];
        return { x: point[0], y: point[1] };
    }

    /**
     * What the view currently covers, as `[min, max]`.
     *
     * The two screen corners unprojected, which is what the answer is: there is no "bounds"
     * property, because a tilted or rotated camera does not have one - this is the axis-aligned box
     * around what it shows.
     */
    bounds(): Bounds {
        const { height, width } = this.size();
        const first = this.screenToMap(0, 0);
        const second = this.screenToMap(width, height);
        return [
            [Math.min(first[0], second[0]), Math.min(first[1], second[1])],
            [Math.max(first[0], second[0]), Math.max(first[1], second[1])]
        ];
    }
}

/**
 * The map's layer stack, as a list.
 *
 * The SDK draws layers in list order, so an app whose stack has a meaning of its own - base map,
 * overlays, then its own markers - works out an index and places a layer there. Everything here is
 * a facade call on the adopted `Layers`; nothing reaches the native list.
 */
export class MapLayers extends MassifObject<'massif::Layers'> {
    constructor(handle: Handle<'massif::Layers'>, id: string) {
        super(handle, 'massif::Layers', id);
    }

    /** How many layers the map is drawing, including any the object API put there. */
    count(): number {
        return this.get('count');
    }

    /** Appends. 0 is the bottom, so this puts the layer on top of everything else. */
    add(layer: MassifLayer | number): this {
        this.call('add', handleOf(layer) as Handle);
        return this;
    }

    /** Puts the layer at `index`, moving everything at or above it up. */
    insert(index: number, layer: MassifLayer | number): this {
        this.call('insert', index, handleOf(layer) as Handle);
        return this;
    }

    /**
     * Replaces the layer at `index` - what a rebuilt decoder needs, keeping the position.
     *
     * Not `set`: that is the property writer every object has, and a two-argument overload of it
     * would make every `layers.set('count', …)` typo compile.
     */
    replace(index: number, layer: MassifLayer | number): this {
        this.call('set', index, handleOf(layer) as Handle);
        return this;
    }

    /** The layer at `index`, as a bare `Layer` - the list does not record what each one is. */
    at(index: number): MassifLayer<'massif::Layer'> | null {
        const layer = this.call('get', index) as unknown as MassifObject<'massif::Layer'> | undefined;
        return layer ? new MassifLayer(layer.handle as Handle, 'massif::Layer') : null;
    }

    remove(layer: MassifLayer | number): boolean {
        return this.call('remove', handleOf(layer) as Handle);
    }

    /** Empties the stack. */
    clear(): this {
        this.call('clear');
        return this;
    }
}

export interface AttachOptions extends SubscribeOptions {
    /** The registry id the map's options take. Defaults to `map`. */
    id?: string;
}

/**
 * The map, addressed through its Options.
 *
 * Everything the map itself carries - fog, sky, light, terrain, the camera limits, the base
 * projection - is a property of Options or of an object reachable from it, so one handle covers
 * the lot and a new option appears the next time the typings are generated.
 *
 * Camera MOVEMENT is not a property: it is a flight with a duration, and the view already has
 * that code. The passthroughs below reach it rather than reimplementing it.
 */
export class MassifMap extends MassifObject<'massif::Options'> {
    private mLayers?: MapLayers;
    private mCamera?: MapCamera;
    private mElements?: MassifElements;
    private readonly mLayerById: { [id: string]: MassifLayer } = {};
    /** Every id this map built, released when it is destroyed rather than living on. */
    private readonly mOwned: [string, string][] = [];

    constructor(
        handle: Handle<'massif::Options'>,
        id: string,
        /** The NativeScript view this map is attached to. */
        readonly view: MapViewLike
    ) {
        super(handle, 'massif::Options', id);
    }

    /**
     * The map's layer list, ordered.
     *
     * `addLayer` covers the common case; this is for an app whose stack has an order of its own -
     * base map, then overlays, then its own markers - and so places a layer at an index, swaps one
     * in place when its decoder is rebuilt, or counts what is there.
     */
    layers(): MapLayers {
        if (!this.mLayers) {
            const id = `${this.id}:layers`;
            // Reused when it is already there. adopt() REFUSES a duplicate id, so a map attached
            // twice under the same id - re-entering an example screen, say - failed here with
            // nothing to say why, and the id outlives the MassifMap that made it.
            let handle = bridge.findObject('layers', id);
            if (!handle) {
                handle = bridge.adoptLayers('layers', id, this.view.mapView.getLayers());
                if (!handle) {
                    throw new MassifApiError(`could not register the layer list of "${this.id}" under "${id}"`);
                }
                this.mOwned.push(['layers', id]);
            }
            this.mLayers = new MapLayers(handle as Handle<'massif::Layers'>, id);
        }
        return this.mLayers;
    }

    /**
     * Builds a layer from a spec and puts it on the map.
     *
     * ```ts
     * map.addLayer('base', {
     *     type: 'vector',
     *     source: { type: 'http', maxZoom: 14, url: 'https://…/{z}/{x}/{y}.pbf' },
     *     style: { type: 'mbvt', project: { type: 'project', assets: { type: 'dir', path: '…' }, name: 'osm' } }
     * });
     * ```
     */
    addLayer<T extends SpecType<'layer'>>(id: string, spec: SpecArg<'layer', T>): MassifLayer<ClassOfSpec<'layer', T>> {
        // this.object, not the module-level createLayer: a layer the MAP built is released when
        // the map is, exactly like the sources and styles beside it. Registered globally it
        // outlived destroy(), so a screen reached a second time failed on a duplicate id - and
        // every other kind on this class already got this right.
        return this.add(this.object('layer', id, spec) as never) as never;
    }

    /**
     * Builds a layer this map OWNS but does not place it.
     *
     * For an app whose stack has an order of its own: it works out the index from the layers it has
     * already added, then calls `add(layer, index)` or `layers().insert(...)`. `addLayer` is the
     * one-liner for everything else.
     */
    buildLayer<T extends SpecType<'layer'>>(id: string, spec: SpecArg<'layer', T>): MassifLayer<ClassOfSpec<'layer', T>> {
        const layer = this.object('layer', id, spec) as never as MassifLayer<ClassOfSpec<'layer', T>>;
        layer.map = this;
        return layer;
    }

    /** Puts a layer built or adopted elsewhere on the map, optionally at a given index. */
    add<L extends MassifLayer>(layer: L, index?: number): L {
        const layers = this.layers();
        if (index === undefined) {
            layers.add(layer);
        } else {
            layers.insert(index, layer);
        }
        layer.map = this;
        if (layer.id) {
            this.mLayerById[layer.id] = layer;
        }
        return layer;
    }

    /** Takes a layer off the map. The layer stays registered until it is destroyed. */
    removeLayer(layer: MassifLayer | number): boolean {
        if (layer instanceof MassifLayer && layer.id) {
            delete this.mLayerById[layer.id];
        }
        return this.layers().remove(layer);
    }

    /** A layer this map added or adopted, by the id it was given. */
    layer(id: string): MassifLayer | null {
        return this.mLayerById[id] ?? null;
    }

    /** How many layers the map is drawing, including any the object API put there. */
    layerCount(): number {
        return this.layers().count();
    }

    /** Adopts a layer built with the object API, and puts it under this map. */
    adopt(id: string, nativeLayer: any, className?: ClassName): MassifLayer {
        const layer = adoptLayer(id, nativeLayer, className);
        layer.map = this;
        this.mLayerById[id] = layer;
        return layer;
    }

    // --- the registry, scoped to this map -----------------------------------------------------

    /**
     * Builds an object of any kind, OWNED by this map - released when the map is, rather than
     * living on under its id. `create` at module level is the one for something shared.
     */
    object<K extends Kind, T extends SpecType<K>>(kind: K, id: string, spec: SpecArg<K, T>): MassifObject<ClassOfSpec<K, T>>;
    /** @see create - `className` is required, for the same reason. */
    object<C extends ClassName>(kind: string, id: string, spec: { type: string } & { [key: string]: Json }, className: C): MassifObject<C>;
    object(kind: string, id: string, spec: { type: string }, className?: ClassName): MassifObject {
        const built = (create as (k: string, i: string, sp: unknown, c?: ClassName) => MassifObject)(kind, id, spec, className);
        this.mOwned.push([kind, id]);
        return built;
    }

    /** A source this map owns. */
    source<T extends SpecType<'source'>>(id: string, spec: SpecArg<'source', T>): MassifSource<ClassOfSpec<'source', T>> {
        return this.object('source', id, spec) as never;
    }

    /**
     * A style this map owns.
     *
     * Worth an id whenever the app talks to it afterwards - a style parameter, a theme switch -
     * because a layer's style property cannot be read back as a handle.
     */
    style<T extends SpecType<'style'>>(id: string, spec: SpecArg<'style', T>): MassifObject<ClassOfSpec<'style', T>> {
        return this.object('style', id, spec);
    }

    // --- markers and popups ---------------------------------------------------------------------

    /**
     * The map's own markers and popups, on a layer built the first time this is called.
     *
     * `sourceSpec` is how a map that does NOT work in lon/lat places its markers correctly; only
     * the first call builds, later ones return what it built.
     */
    elements(sourceSpec?: SpecArg<'source', 'local'>): MassifElements {
        if (!this.mElements) {
            this.mElements = new MassifElements(this, `${this.id}.elements`, sourceSpec);
        }
        return this.mElements;
    }

    /** Adds a marker. The spec carries its position AND its style, inline or by id. */
    addMarker(spec: SpecArg<'element', 'marker'>) {
        return this.elements().add(spec);
    }

    /** The same for a balloon popup - a title and a body anchored to a position. */
    addPopup(spec: SpecArg<'element', 'balloon'>) {
        return this.elements().add(spec);
    }

    /**
     * Releases the map's registration and every id it built - layers, sources, styles, elements.
     *
     * Not the view: the object API's map keeps working, which is the point of a facade that can
     * be adopted a piece at a time.
     */
    destroy(): boolean {
        for (let i = this.mOwned.length - 1; i >= 0; i--) {
            destroy(this.mOwned[i][0], this.mOwned[i][1]);
        }
        this.mOwned.length = 0;
        this.mElements = undefined;
        this.mCamera = undefined;
        return super.destroy();
    }

    /**
     * The view's size in pixels.
     *
     * A property of the VIEW, not of the map, but every caller that needs it has the map: framing a
     * route, working out how many metres a screen width covers.
     */
    size(): { width: number; height: number } {
        return { width: this.view.getMeasuredWidth?.() ?? 0, height: this.view.getMeasuredHeight?.() ?? 0 };
    }

    /**
     * A screenshot of the map.
     *
     * The one thing on this class that is not a facade call: reading the framebuffer is platform
     * code, and there is no verb for it. `wait` holds the capture until the tiles have settled.
     */
    capture(wait = false): Promise<any> {
        if (!this.view.captureRendering) {
            throw new MassifApiError('this map view cannot capture its rendering');
        }
        return this.view.captureRendering(wait);
    }

    /**
     * Asks for a frame.
     *
     * Rarely needed: the SDK redraws when a property that changes the picture is written. It is
     * here for the cases where an app knows better - a change made off the UI thread, or a view
     * that was hidden while the map moved.
     */
    requestRedraw(): this {
        this.view.requestRedraw?.();
        return this;
    }

    /** The camera. See MapCamera - a move is a flight, not a property. */
    camera(): MapCamera {
        if (!this.mCamera) {
            // The map view is adopted under its own kind: it is what carries the camera, and
            // going through the facade is also what gives moveTo the map's projection.
            const id = `${this.id}:view`;
            let handle = bridge.findObject('view', id);
            if (!handle) {
                handle = bridge.adoptView('view', id, this.view.mapView.getBaseMapView());
                if (!handle) {
                    throw new MassifApiError(`could not register the map view of "${this.id}"`);
                }
                this.mOwned.push(['view', id]);
            }
            const view = new MassifObject(handle as Handle<'massif::BaseMapView'>, 'massif::BaseMapView', id);
            this.mCamera = new MapCamera(view, () => this.size());
        }
        return this.mCamera;
    }

    /**
     * The projection every later handler's position reads default to.
     *
     * Shorthand for `eventOptions({ projection })`, and the one an app actually wants: ask once,
     * then read `e.getPos('clickPos')` plainly.
     */
    eventProjection(name: ProjectionName): this {
        return this.eventOptions({ projection: name });
    }

    // --- the map events, by name ---------------------------------------------------------------

    onClick(handler: (data: MassifEventData<'massif::Options', 'map.clicked'>) => void, options?: SubscribeOptions) {
        return this.subscribe('map.clicked', handler, options);
    }
    onMove(handler: (data: MassifEventData<'massif::Options', 'map.moved'>) => void, options?: SubscribeOptions) {
        return this.subscribe('map.moved', handler, options);
    }
    onIdle(handler: (data: MassifEventData<'massif::Options', 'map.idle'>) => void, options?: SubscribeOptions) {
        return this.subscribe('map.idle', handler, options);
    }
    onStable(handler: (data: MassifEventData<'massif::Options', 'map.stable'>) => void, options?: SubscribeOptions) {
        return this.subscribe('map.stable', handler, options);
    }
    onInteraction(handler: (data: MassifEventData<'massif::Options', 'map.interaction'>) => void, options?: SubscribeOptions) {
        return this.subscribe('map.interaction', handler, options);
    }

    // --- the option groups, which is what most of an app touches -------------------------------

    /*
     * One accessor per options CLASS, not per option - that list is closed and does not grow with
     * the SDK, while the properties inside them are data.
     *
     * With no argument they scope onto what is already there. With a spec they BUILD it first,
     * which is what a map needs on the way in: Options starts with these properties empty, and
     * writing through an empty one is an error rather than a silent no-op.
     */

    /** `fogOptions.*` - fog on the mapbox model, independent of the terrain. */
    fog(spec?: SpecArg<'options', 'fog'>) {
        return this.optionGroup('fogOptions', 'fog', spec);
    }
    /** `skyOptions.*` - the sky dome behind the map. */
    sky(spec?: SpecArg<'options', 'sky'>) {
        return this.optionGroup('skyOptions', 'sky', spec);
    }
    /** `lightOptions.*` - sun direction and colour, which the terrain shades from. */
    light(spec?: SpecArg<'options', 'light'>) {
        return this.optionGroup('lightOptions', 'light', spec);
    }
    /**
     * `terrainOptions.*` - 3D terrain from an elevation source.
     *
     * The elevation decoder comes from the source's own `encoding`, so nothing here names one.
     */
    terrain(spec?: SpecArg<'options', 'terrain'>) {
        return this.optionGroup('terrainOptions', 'terrain', spec);
    }

    private optionGroup<P extends ObjectPath<'massif::Options'>>(property: P, type: SpecType<'options'>, spec?: SpecArg<'options', SpecType<'options'>>) {
        if (spec) {
            const built = this.object('options', `${this.id}.${type}`, spec);
            this.set(property as never, built.handle as never);
        }
        return this.group(property);
    }
}

/**
 * Attaches the surface API to a map view that is already loaded.
 *
 * The map's own event listener is CHAINED, not replaced, so whatever the view (or the app) had
 * installed keeps being called - adopting this API never silently disconnects an existing
 * handler.
 *
 * ```ts
 * const map = api.attach(mapView);
 * map.fog().set('rangeStart', 2.5);
 * map.on('map.clicked', (e) => console.log(e.getPos('clickPos')));   // [lng, lat]
 * ```
 */
export function attach(view: MapViewLike, options: AttachOptions = {}): MassifMap {
    requireApi();
    // The view attaches itself when it loads, to raise its own events. Registering the same map
    // a second time under another id would give the SDK two handles onto one object and leave
    // whichever is destroyed first dangling - so reuse it, and let the options through.
    const existing = (view as any).facadeMap?.();
    if (existing) {
        const { projection: existingProjection, throttle: existingThrottle, debounce: existingDebounce } = options;
        if (existingProjection || existingThrottle || existingDebounce) {
            existing.eventOptions({ projection: existingProjection, throttle: existingThrottle, debounce: existingDebounce });
            // Re-subscribes the view's own events, which were made before these options existed.
            (view as any).enableFacadeEvents?.();
        }
        return existing;
    }
    const nativeOptions = view.getOptions()?.getNative();
    if (!nativeOptions) {
        throw new MassifApiError('the map is not ready yet - attach from the mapReady event');
    }
    const id = options.id ?? 'map';
    const handle = bridge.adoptOptions('options', id, nativeOptions);
    if (!handle) {
        throw new MassifApiError(`could not register the map under "${id}" - the id is taken`);
    }
    bridge.attachMapEvents(view.mapView, handle);

    const map = new MassifMap(handle as Handle<'massif::Options'>, id, view);
    const { projection, throttle, debounce } = options;
    if (projection || throttle || debounce) {
        map.eventOptions({ projection, throttle, debounce });
    }
    return map;
}
