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

namespace massif { namespace api {

    /**
     * The facade API, as an app sees it (experimental, #146). No SDK type may appear in a signature
     * (#159), so the C ABI can carry the whole class: anything naming one belongs in MassifInterop,
     * and scripts/check-facade-abi.sh fails the build otherwise.
     */
    class MassifApi {
    public:
        /**
         * Builds an object from a JSON spec and registers it under a kind and id. An existing id
         * with an identical spec returns its handle; a different spec fails. Keys the factory does
         * not take are applied as properties, unknown ones dropped with a warning.
         * @param kind The object kind: "source", "style" or "layer".
         * @param objectId The caller's name for the object.
         * @param json The spec.
         * @return The handle.
         * @throws std::runtime_error If the spec does not parse, names no known type, or the id is
         *         taken by a different spec.
         */
        static int create(const std::string& kind, const std::string& objectId, const std::string& json);

        /**
         * Subscribes to an event on an object.
         * @param handle The target, from create or MassifInterop.adopt.
         * @param event The event name, e.g. "map.clicked".
         * @param listener Called when it fires.
         * @param delivery 0 origin, 1 UI, 2 background.
         * @param coalesce Whether a queued event is replaced rather than joined by the next one.
         * @param projection The well-known name position reads default to during the handler, e.g.
         *        "EPSG:4326". Empty leaves the map's own projection; a payload read later must name
         *        it per read (see getPos).
         * @param consume Whether the listener's return value can claim the event, stopping later
         *        handlers and marking the gesture handled. Requires delivery 0: the SDK asks
         *        synchronously.
         * @param throttleMs Drops events within this many milliseconds of the last one delivered to
         *        this handler; 0 is off. Refused on a consuming subscription.
         * @return The subscription, or 0 when the handle is stale, the projection is unknown, a
         *         consuming subscription asked for another thread, or a consuming one asked to be
         *         throttled.
         */
        static int on(int handle, const std::string& event,
                      const std::shared_ptr<EventListener>& listener, int delivery, bool coalesce,
                      const std::string& projection = std::string(), bool consume = false,
                      int throttleMs = 0);

        /**
         * Registers how to reach the app's UI thread. post() is called on the producing thread and
         * must get onto the UI thread and call drain; without a dispatcher, UI subscriptions run
         * inline on the producing thread.
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
         * Writes a property. The path may walk object properties ("fogOptions.rangeStart") and end in
         * a bag key ("params.water_color"); a bag also takes every key at once as a JSON object.
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
         * Writes several properties from one JSON object of path to value, in one crossing. Every key
         * is attempted and the first failure is returned.
         * @return 0 when every key applied, see the Result enum otherwise.
         */
        static int setAll(int handle, const std::string& json,
                          const std::string& projection = std::string());

        /**
         * Writes a position property in a named projection - the write counterpart of getPos.
         * @param json The position as `[x, y]` or `[x, y, z]`, bounds as a pair of them.
         * @param projection The well-known name the value is in, e.g. "EPSG:3857". Empty behaves
         *        exactly like setString.
         */
        static int setPos(int handle, const std::string& path, const std::string& json,
                          const std::string& projection = std::string());

        /**
         * Points an object property at another registered object - a layer's data source, a
         * decoder's style. Pass 0 to clear it. A value of the wrong class is an error, not a crash.
         */
        static int setObject(int handle, const std::string& path, int value);

        /**
         * The object an object property points at, as a handle the caller owns - how a child is
         * shared. Pass it to destroy when done: it is a reference, so the object is untouched.
         * @return 0 when the path does not resolve, is not an object property, or is null.
         */
        static int getObject(int handle, const std::string& path);

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
         * Reads a position property as JSON, in the projection asked for: `[x, y]` or `[x, y, z]`,
         * bounds as a pair of them.
         * @param projection The well-known name, e.g. "EPSG:3857". Empty means the running event
         *        handler's projection, else WGS84 degrees (#159); setString takes positions the same
         *        way, so a read/write round trip is safe.
         * @return The JSON, or an empty string when the path does not resolve.
         */
        static std::string getPos(int handle, const std::string& path,
                                  const std::string& projection = std::string());

        /**
         * Runs a method on an object. The result is always a handle the caller owns - pass it to
         * destroy. An object result is that object; anything else is a JSON document, read with an
         * empty path.
         * @param method The method name, e.g. "loadTile".
         * @param argsJson The arguments as a JSON array, e.g. "[[8467,5852,14]]". Empty for none.
         * @return The result handle.
         * @throws std::runtime_error If the handle is stale, the method is unknown, the arguments
         *         do not fit it, or the method failed.
         */
        static int call(int handle, const std::string& method,
                        const std::string& argsJson = std::string());

        /**
         * The same, on a worker thread, with the result delivered as `event` on the object (subscribe
         * first). The payload is the result - an object handle or a JSON document, 0 on failure - and
         * is freed once the handlers have run.
         * @return The call's id, for cancelCall.
         * @throws std::runtime_error If the handle is stale, the method is unknown, or the
         *         argument JSON does not parse. A failure while running is a payload of 0.
         */
        static int callAsync(int handle, const std::string& method, const std::string& argsJson,
                             const std::string& event);

        /**
         * Cancels a queued or running async call: it will not start and no event fires, but a call
         * already running is not aborted.
         * @return True when the call was queued or running, false when it had already finished.
         */
        static bool cancelCall(int call);

        /**
         * Cancels every queued or running call on an object.
         * @return How many were cancelled.
         */
        static int cancelCalls(int handle);

        /**
         * Reads a bulk numeric result as a flat array: `double[]` in Java, `NSData` over the raw
         * doubles in Objective-C.
         * @return The values, or empty when the handle is not a numeric result.
         */
        static std::vector<double> getDoubles(int handle);

        /**
         * Reads a binary property as raw bytes - `byte[]` in Java, `NSData` in Objective-C - not as
         * the SDK's BinaryData, so this class names no SDK type (#159).
         * @param path The path to the property, e.g. "data" on a tile. Empty when the handle is
         *             the blob itself.
         * @return The data, empty when the path does not resolve to one.
         */
        static std::vector<unsigned char> getData(int handle, const std::string& path);

        /**
         * Drops a handle's id, and with it the context's reference to the object.
         * @return True when the handle was live.
         */
        static bool destroy(int handle);

    private:
        MassifApi();
    };

} }

#endif
