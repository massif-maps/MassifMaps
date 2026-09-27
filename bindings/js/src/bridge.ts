/**
 * @internal
 * @module
 */
/** The one place the surface API touches a platform: each platform supplies a NativeBridge. */

/** What a native event listener hands back to JavaScript. Return true to consume the event. */
export type NativeEventHandler = (target: number, event: string, payload: number) => boolean;

/** 0 the producing thread, 1 the UI thread, 2 a background thread. */
export type Delivery = 0 | 1 | 2;

export interface NativeBridge {
    /** Whether the linked SDK carries the surface API at all. False on an SDK built without it. */
    readonly available: boolean;

    // --- registry -----------------------------------------------------------------------------
    create(kind: string, id: string, json: string): number;
    destroy(handle: number): boolean;
    isValid(handle: number): boolean;
    findObject(kind: string, id: string): number;
    unregisterObject(kind: string, id: string): boolean;

    // --- properties ---------------------------------------------------------------------------
    setFloat(handle: number, path: string, value: number): number;
    setInt(handle: number, path: string, value: number): number;
    setBool(handle: number, path: string, value: boolean): number;
    setString(handle: number, path: string, value: string): number;
    setObject(handle: number, path: string, value: number): number;
    /**
     * Several properties in ONE crossing, from a JSON object of path to value.
     *
     * Undefined on an SDK built before it, so a caller falls back to one call per key rather than
     * failing - see MassifObject.apply.
     */
    setAll?(handle: number, json: string, projection: string): number;
    /** The object an object property points at, as a handle the caller owns. 0 when there is none. */
    getObject(handle: number, path: string): number;

    getFloat(handle: number, path: string, defaultValue: number): number;
    getInt(handle: number, path: string, defaultValue: number): number;
    getBool(handle: number, path: string, defaultValue: boolean): boolean;
    /** Returns null when the path does not resolve, rather than the sentinel the SDK needs. */
    getString(handle: number, path: string): string | null;
    /** JSON for a coordinate, converted when a projection is named. Null when it does not resolve. */
    getPos(handle: number, path: string, projection: string): string | null;

    // --- methods ------------------------------------------------------------------------------
    /** Throws on a stale handle, an unknown method or arguments that do not fit it. */
    call(handle: number, method: string, argsJson: string): number;
    callAsync(handle: number, method: string, argsJson: string, event: string): number;
    cancelCall(call: number): boolean;
    cancelCalls(handle: number): number;

    /** A bulk numeric result, in one crossing. */
    getDoubles(handle: number): number[];
    /** A blob, without turning it into a string. Null when the path is not one. */
    getData(handle: number, path: string): ArrayBuffer | null;

    // --- events -------------------------------------------------------------------------------
    /**
     * Subscribes, keeping the native listener alive for the life of the subscription.
     *
     * The director is held on the C++ side as a raw pointer, so dropping the reference here
     * silently stops the handler ever running - on iOS ARC collects it the moment `on` returns.
     *
     * `consume` says the handler's return value may stop the event. The SDK ignores what a
     * non-consuming handler returns, so this has to be declared at subscribe time even though
     * the decision is made per event.
     */
    on(handle: number, event: string, handler: NativeEventHandler, delivery: Delivery, coalesce: boolean, projection: string, consume: boolean): number;

    /**
     * Whether the linked SDK's `MassifApi.on` takes the consume flag.
     *
     * False on an SDK whose `on` hardcodes it, where `MassifEventData.consumed` is accepted and
     * has no effect. Reported once rather than per subscription.
     */
    readonly canConsume: boolean;
    off(subscription: number): boolean;
    offAll(handle: number): number;

    // --- adoption -----------------------------------------------------------------------------
    //
    // One method per adoptable base, not one taking `any`: the SDK's `adopt` is overloaded on the
    // C++ type, which is a distinct Objective-C selector per overload and needs the right Java
    // overload picked. `kind` stays the id namespace - it never names the class.
    /** Gives an object built with the object API an id, and with it properties, methods, events. */
    adoptOptions(kind: string, id: string, options: any): number;
    adoptLayer(kind: string, id: string, layer: any): number;
    adoptLayers(kind: string, id: string, layers: any): number;
    adoptSource(kind: string, id: string, source: any): number;
    /**
     * The map view, which is what carries the CAMERA - moveTo, flyTo, fitBounds, screenToMap,
     * mapToScreen and stopFlight become ordinary `call`s on the handle, with focusPos, zoom,
     * rotation, tilt and flightActive read as properties beside them.
     */
    adoptView(kind: string, id: string, view: any): number;
    /**
     * The one an app SUBCLASSES rather than adopts in stages - a package reading styles from the
     * NativeScript app folder, or from anywhere the SDK has no factory for. Once adopted, any spec
     * taking an `assets` key resolves the id.
     */
    adoptAssets(kind: string, id: string, assets: any): number;
    /** The escape hatch: the native object behind an id, to hand back to the object API. */
    getNativeLayer(id: string): any;
    getNativeSource(id: string): any;
    /** The same by handle, for an object with no id - a child read off a property. */
    getNativeLayerByHandle(handle: number): any;
    getNativeSourceByHandle(handle: number): any;

    // --- event bridges ------------------------------------------------------------------------
    //
    // Each chains whatever listener was already installed, so adopting the surface API never
    // disconnects the handlers an app (or this plugin's own MassifMap view) put there first.
    attachMapEvents(mapView: any, handle: number): void;
    attachVectorTileEvents(layer: any, handle: number): void;
    attachVectorElementEvents(layer: any, handle: number): void;

    /**
     * The leaf class name of a native object, e.g. `VectorTileLayer`.
     *
     * Read off the runtime class - `com.massifmaps.layers.VectorTileLayer` on Android,
     * `MSFVectorTileLayer` on iOS - so an object built with the object API can be adopted
     * without the caller naming its class. The shared layer checks it against the generated
     * table; an unknown name is not the bridge's problem.
     */
    nativeShortClassName(nativeObject: any): string | null;
}

const UNAVAILABLE = { available: false, canConsume: false } as NativeBridge;

export let bridge: NativeBridge = UNAVAILABLE;

/** Installs the platform's bridge; the NativeScript plugin and @massif-maps/web each call it once. */
export function setBridge(platform: NativeBridge) {
    bridge = platform;
}
