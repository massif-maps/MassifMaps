/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_MASSIFAPI_H_
#define _MASSIF_API_MASSIFAPI_H_

#include "api/Context.h"
#include "api/EventListener.h"
#include "api/UiDispatcher.h"

#include <memory>
#include <string>
#include <vector>

namespace massif {
    class AssetPackage;
    class BaseMapView;
    class Options;
    class TileDataSource;
    class Layer;
    class Layers;
    class MapEventListener;
    class VectorTileEventListener;
    class VectorElementEventListener;

    namespace api {

    /**
     * The facade API, as an app sees it.
     *
     * Experimental and incomplete: only the property verbs exist so far, and they address the
     * default context. See https://github.com/massif-maps/MassifMaps/issues/146.
     *
     * Every call returns a result code rather than throwing, because this is a verification
     * surface rather than the final binding.
     */
    class MassifApi {
    public:
        /**
         * Adopts an object built with the object API, so it can be addressed by id and handle.
         *
         * The TYPE picks what is being adopted, not the kind string - `kind` is only the id
         * namespace, and the same Options is legitimately adopted as "map" by one app and
         * "options" by another. One overload per adoptable base class, and that set is closed:
         * SWIG emits one thunk per signature, and the SDK's bases share no common root to
         * declare a single parameter as.
         *
         * Not named `register`: that is a C++ keyword, and a C one, so it is not a legal
         * Objective-C selector piece either.
         *
         * @param kind The namespace, e.g. "options". Ids only collide within a kind.
         * @param objectId The caller's name for the object. "id" is a keyword in Objective-C.
         * @param options The object.
         * @return The handle, or 0 when the id is already taken.
         */
        static int adopt(const std::string& kind, const std::string& objectId,
                         const std::shared_ptr<Options>& options);

        /**
         * The same for a layer or a source built with the object API.
         *
         * This is what lets an app adopt the facade a piece at a time: everything it already
         * built keeps working, and gains an id, properties, methods and events. The CONCRETE
         * class is recovered at runtime, so an adopted VectorTileLayer answers to a vector tile
         * layer's properties rather than only to Layer's.
         *
         * @return The handle, or 0 when the id is taken or the class is not a wrapped one.
         */
        static int adopt(const std::string& kind, const std::string& objectId,
                         const std::shared_ptr<Layer>& layer);

        /**
         * Registers the map's layer list, so a layer built from a spec can be PUT on the map.
         *
         * A spec builds an object, it does not place it - the same reason LocalVectorDataSource
         * needs add(). This is the one for layers; call add/remove on the handle it returns.
         */
        static int adopt(const std::string& kind, const std::string& objectId,
                         const std::shared_ptr<Layers>& layers);

        /**
         * @copydoc MassifApi::adopt
         */
        static int adopt(const std::string& kind, const std::string& objectId,
                         const std::shared_ptr<TileDataSource>& source);

        /**
         * The same for an asset package, which is the one a binding cannot express as a spec.
         *
         * An app that reads its styles from somewhere the SDK has no factory for - a NativeScript
         * app folder, an app's own decryption - subclasses AssetPackage in Java, Objective-C or
         * TypeScript and adopts the instance here. Every spec that takes an `assets` key resolves
         * a string as an id of this kind, so `{"type":"cartocss","css":…,"assets":"shared"}`
         * then reaches it.
         */
        static int adopt(const std::string& kind, const std::string& objectId,
                         const std::shared_ptr<AssetPackage>& assets);

        /**
         * The map view, which is what carries the CAMERA.
         *
         * Adopt it and moveTo, flyTo, fitBounds, screenToMap, mapToScreen and stopFlight become
         * ordinary facade calls, with focusPos, zoom, rotation, tilt and flightActive as read-only
         * properties beside them. Until that existed the typed sugar on each platform called the
         * map view directly, so the camera was the one part of the surface a binding could not
         * reach through the C ABI - see #159.
         */
        static int adopt(const std::string& kind, const std::string& objectId,
                         const std::shared_ptr<BaseMapView>& view);

        /**
         * Builds an object from a JSON spec and registers it under a kind and id.
         *
         * Creating an id that already exists with an IDENTICAL spec returns the existing handle,
         * so two maps can share one source without coordinating. A different spec under the same
         * id fails. Keys the factory does not need are applied as properties, and a key the SDK
         * does not know is dropped with a warning.
         *
         * @param kind The object kind: "source", "style" or "layer".
         * @param objectId The caller's name for the object.
         * @param json The spec.
         * @return The handle.
         * @throws std::runtime_error If the spec does not parse, names no known type, or the id is
         *         taken by a different spec.
         */
        static int create(const std::string& kind, const std::string& objectId, const std::string& json);

        /**
         * Returns a source built or adopted earlier, so it can be handed to the object API.
         * This is the escape hatch: anything the facade cannot express yet is still reachable.
         */
        static std::shared_ptr<TileDataSource> getSource(const std::string& objectId);

        /**
         * Returns a layer built earlier, so it can be added to a map with the object API. Layers
         * are not attached by create - that needs the map verbs.
         */
        static std::shared_ptr<Layer> getLayer(const std::string& objectId);

        /**
         * Builds the listener that turns a map's callbacks into facade events on a target.
         *
         * The app installs it with the map view's own setMapEventListener, which is also why it
         * takes the listener that was already there: a single slot means adopting the facade
         * would otherwise disconnect the app's existing handlers.
         *
         *   int handle = MassifApi.adopt("map", "main", mapView.getOptions());
         *   mapView.setMapEventListener(
         *       MassifApi.createEventBridge(handle, mapView.getMapEventListener()));
         *
         * @param handle The target events are emitted on.
         * @param chained The listener already installed, or null.
         * @return The bridge.
         */
        static std::shared_ptr<MapEventListener> createEventBridge(
            int handle, const std::shared_ptr<MapEventListener>& chained);

        /**
         * The same for a vector tile layer's clicks, which is where a feature payload comes from.
         * Install it with the layer's setVectorTileEventListener.
         *
         * The click is claimed if either the chained listener or a consuming subscriber claims it.
         */
        static std::shared_ptr<VectorTileEventListener> createVectorTileEventBridge(
            int handle, const std::shared_ptr<VectorTileEventListener>& chained);

        /**
         * The same for a vector layer's element clicks.
         */
        static std::shared_ptr<VectorElementEventListener> createVectorElementEventBridge(
            int handle, const std::shared_ptr<VectorElementEventListener>& chained);

        /**
         * Subscribes to an event on an object.
         * @param handle The target, from create or registerMapView.
         * @param event The event name, e.g. "map.clicked".
         * @param listener Called when it fires.
         * @param consume Whether the listener's return value can claim the event, stopping it
         *        reaching later handlers and telling the SDK the gesture was handled. The SDK asks
         *        that question synchronously, so a consuming subscription must be delivery 0.
         * @param delivery 0 origin, 1 UI, 2 background.
         * @param projection The well-known name of the projection this handler's position reads
         *        default to, e.g. "EPSG:4326". Empty leaves them in the map's own projection. It
         *        applies for the duration of the call, so a payload kept and read afterwards has
         *        to name the projection per read - see getPos.
         * @return The subscription, or 0 when the handle is stale, the projection is unknown, or a
         *         consuming subscription asked for another thread.
         */
        static int on(int handle, const std::string& event,
                      const std::shared_ptr<EventListener>& listener, bool consume, int delivery,
                      bool coalesce, const std::string& projection = std::string());

        /**
         * Registers how to reach the app's UI thread, for subscriptions that asked for it.
         *
         * The dispatcher's post() is called from whatever thread produced the event, and must get
         * onto the UI thread and call drain. Without one, UI subscriptions run inline on the
         * producing thread and the facade warns once.
         *
         * @param dispatcher The dispatcher, or null to go back to inline delivery.
         */
        static void setUiDispatcher(const std::shared_ptr<UiDispatcher>& dispatcher);

        /**
         * Runs the handlers waiting for this thread. Called by whatever the dispatcher posted.
         * @return How many were delivered.
         */
        static int drain();

        /**
         * Removes one subscription.
         */
        static bool off(int subscription);

        /**
         * Removes every handler of one event on one object.
         */
        static int offEvent(int handle, const std::string& event);

        /**
         * Removes every handler on one object.
         */
        static int offAll(int handle);

        /**
         * Drops an id and the context's reference to the object behind it.
         * @return True when the id existed.
         */
        static bool unregisterObject(const std::string& kind, const std::string& objectId);

        /**
         * Returns the handle registered under a kind and id, or 0.
         */
        static int findObject(const std::string& kind, const std::string& objectId);

        /**
         * Whether a handle still resolves. A binding needs this to tell "destroyed" from "never
         * existed" without a property read that might legitimately fail for another reason.
         */
        static bool isValid(int handle);

        /**
         * Writes a property. The path may walk object properties: "fogOptions.rangeStart".
         * @return 0 on success, see the Result enum otherwise.
         */
        static int setFloat(int handle, const std::string& path, double value);
        /**
         * @copydoc MassifApi::setFloat
         */
        static int setInt(int handle, const std::string& path, long long value);
        /**
         * @copydoc MassifApi::setFloat
         */
        static int setBool(int handle, const std::string& path, bool value);
        /**
         * @copydoc MassifApi::setFloat
         */
        static int setString(int handle, const std::string& path, const std::string& value);

        /**
         * Points an object property at another registered object - a layer's data source, a
         * decoder's style. Pass 0 to clear it.
         *
         * The value's class is checked against the property's before anything is cast, so pointing
         * a style property at a source is an error rather than a crash.
         */
        static int setObject(int handle, const std::string& path, int value);

        /**
         * Reads a property. Returns the fallback when the path does not resolve, so a caller
         * that does not care about the reason does not have to check twice.
         */
        static double getFloat(int handle, const std::string& path, double defaultValue);
        /**
         * @copydoc MassifApi::getFloat
         */
        static long long getInt(int handle, const std::string& path, long long defaultValue);
        /**
         * @copydoc MassifApi::getFloat
         */
        static bool getBool(int handle, const std::string& path, bool defaultValue);
        /**
         * @copydoc MassifApi::getFloat
         */
        static std::string getString(int handle, const std::string& path, const std::string& defaultValue);

        /**
         * Reads a position property as JSON, in the projection asked for.
         *
         * A position is `[x, y]` or `[x, y, z]` and bounds are a pair of them, so one call covers
         * clickPos, featurePos and dataExtent alike.
         * @param projection The well-known name, e.g. "EPSG:4326". Empty leaves the value in the
         *        object's own projection, or in the one the running event handler asked for.
         * @return The JSON, or an empty string when the path does not resolve.
         */
        static std::string getPos(int handle, const std::string& path,
                                  const std::string& projection = std::string());

        /**
         * Runs a method on an object.
         *
         * The result is ALWAYS a handle the CALLER OWNS - pass it to destroy, or it stays
         * registered. An object result is that object; anything else is a JSON document, read
         * with an empty path:
         *
         *   int tile = MassifApi.call(source, "loadTile", "[[8467,5852,14]]");
         *   byte[] bytes = MassifApi.getData(tile, "data");
         *   MassifApi.destroy(tile);
         *
         *   int result = MassifApi.call(layer, "getElevations", "[[[5.76,45.24],[5.77,45.25]]]");
         *   double first = MassifApi.getFloat(result, "0", 0);
         *
         * @param method The method name, e.g. "loadTile".
         * @param argsJson The arguments as a JSON array, e.g. "[[8467,5852,14]]". Empty for none.
         * @return The result handle.
         * @throws std::runtime_error If the handle is stale, the method is unknown, the arguments
         *         do not fit it, or the method failed.
         */
        static int call(int handle, const std::string& method,
                        const std::string& argsJson = std::string());

        /**
         * The same, on a worker thread, with the result delivered as an event on the object.
         *
         * Subscribe to `event` with `on` first; the payload is the result - an object handle
         * directly, or a JSON document a path reads out of ("" for the whole thing). A payload of
         * 0 means the call failed. The payload is freed once the handlers have run, exactly like
         * a map event's, so nothing has to be destroyed by hand.
         *
         *   MassifApi.on(source, "loadTile.done", listener, 1, false);
         *   int call = MassifApi.callAsync(source, "loadTile", "[[8467,5852,14]]", "loadTile.done");
         *
         * @return The call's id, for cancelCall.
         * @throws std::runtime_error If the handle is stale, the method is unknown, or the
         *         argument JSON does not parse. A failure while running is reported as a payload
         *         of 0, since the call has returned by then.
         */
        static int callAsync(int handle, const std::string& method, const std::string& argsJson,
                             const std::string& event);

        /**
         * Cancels a queued or running async call.
         *
         * Cancelling stops the call being STARTED and stops its result being DELIVERED, but
         * cannot abort one already running - loadTile has no cancellation token to pass on. Either
         * way no event fires.
         * @return True when the call was queued or running, false when it had already finished.
         */
        static bool cancelCall(int call);

        /**
         * Cancels every queued or running call on an object.
         * @return How many were cancelled.
         */
        static int cancelCalls(int handle);

        /**
         * Reads a bulk numeric result as a flat array.
         *
         * A profile over a track is thousands of numbers, so they arrive as one array rather than
         * as JSON or as a proxy read an element at a time - `double[]` in Java, `NSData` over the
         * raw doubles in Objective-C:
         *
         *   int result = MassifApi.call(layer, "getElevations", "[[[5.76,45.24],[5.77,45.25]]]");
         *   double[] metres = MassifApi.getDoubles(result);
         *   MassifApi.destroy(result);
         *
         * @return The values, or empty when the handle is not a numeric result.
         */
        static std::vector<double> getDoubles(int handle);

        /**
         * Reads a binary property without turning it into a string.
         *
         * The blob crosses as RAW BYTES - `byte[]` in Java, `NSData` in Objective-C - not as the
         * SDK's BinaryData. That is the point: this class names no SDK type, so a hand-written JNI,
         * @objc, N-API or dart:ffi layer could carry the whole of it (#159).
         * @param path The path to the property, e.g. "data" on a tile. Empty when the handle is
         *             the blob itself.
         * @return The data, empty when the path does not resolve to one.
         */
        static std::vector<unsigned char> getData(int handle, const std::string& path);

        /**
         * Drops a handle's id, and with it the context's reference to the object. Addressed by
         * handle rather than by kind and id, which is what a caller holding a result has.
         * @return True when the handle was live.
         */
        static bool destroy(int handle);

    private:
        MassifApi();
    };

} }

#endif
