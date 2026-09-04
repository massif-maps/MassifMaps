/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_MASSIFINTEROP_H_
#define _MASSIF_API_MASSIFINTEROP_H_

#include <memory>
#include <string>

namespace massif {
    class AssetPackage;
    class BaseMapView;
    class Options;
    class TileDataSource;
    class VectorDataSource;
    class Layer;
    class Layers;
    class MapEventListener;
    class VectorTileEventListener;
    class VectorElementEventListener;

    namespace api {

    /**
     * The bridge between the object API and the facade.
     *
     * Split off MassifApi so that class names no SDK type at all: a binding written by hand -
     * JNI, @objc, N-API, dart:ffi - can carry the whole of MassifApi, and the C ABI already
     * does. Everything here names an SDK class in its signature, so it is only callable from a
     * platform that already holds the object API, which is exactly what "interop" means (#159).
     *
     * A binding built on the facade alone never needs this class. An app migrating to the facade
     * a piece at a time needs it for as long as the migration lasts.
     */
    class MassifInterop {
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
         * @copydoc MassifInterop::adopt
         */
        static int adopt(const std::string& kind, const std::string& objectId,
                         const std::shared_ptr<TileDataSource>& source);

        /**
         * The same for a vector data source, which is what an EXTENSION adopts.
         *
         * Both data source bases are SWIG directors, so a source the SDK has no factory for -
         * GDAL/OGR, a proprietary format, anything with its own native library - is subclassed in
         * Java, Objective-C or TypeScript and adopted here. That is the whole extension mechanism:
         * the SDK ships no dependency, and the source is a facade object like any other.
         *
         * TileDataSource has had this since the facade landed; this is its vector counterpart.
         *
         * The parameter is `vectorSource`, not `source`: Objective-C has no overloading and
         * builds its selector from the parameter names, so two `source` overloads are one
         * duplicate `adopt:objectId:source:`.
         */
        static int adopt(const std::string& kind, const std::string& objectId,
                         const std::shared_ptr<VectorDataSource>& vectorSource);

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
         * Returns a source built or adopted earlier, so it can be handed to the object API.
         * This is the escape hatch: anything the facade cannot express yet is still reachable.
         */
        static std::shared_ptr<TileDataSource> getSource(const std::string& objectId);

        /**
         * The same by HANDLE, for an object that was never given an id - a child read off a
         * property, a call result. An app handing the base map's tiles to its own native code has
         * one of those and no id to name it by.
         */
        static std::shared_ptr<TileDataSource> getSourceByHandle(int handle);

        /**
         * Returns a layer built earlier, so it can be added to a map with the object API. Layers
         * are not attached by create - that needs the map verbs.
         */
        static std::shared_ptr<Layer> getLayer(const std::string& objectId);

        /** @copydoc MassifInterop::getSourceByHandle */
        static std::shared_ptr<Layer> getLayerByHandle(int handle);

        /**
         * Builds the listener that turns a map's callbacks into facade events on a target.
         *
         * The app installs it with the map view's own setMapEventListener, which is also why it
         * takes the listener that was already there: a single slot means adopting the facade
         * would otherwise disconnect the app's existing handlers.
         *
         *   int handle = MassifInterop.adopt("map", "main", mapView.getOptions());
         *   mapView.setMapEventListener(
         *       MassifInterop.createEventBridge(handle, mapView.getMapEventListener()));
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

    private:
        MassifInterop();
    };

} }

#endif
