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
    class CelestialEventListener;

    namespace api {

    /**
     * The bridge between the object API and the facade; split off so MassifApi names no SDK type
     * and a hand-written binding can carry it whole (#159). Only needed while migrating to the facade.
     */
    class MassifInterop {
    public:
        /**
         * Adopts an object built with the object API, so it can be addressed by id and handle.
         * The parameter type picks what is adopted; `kind` is only the id namespace.
         * Not named `register`: a C/C++ keyword, so not a legal Objective-C selector piece.
         *
         * @param kind The namespace, e.g. "options". Ids only collide within a kind.
         * @param objectId The caller's name for the object. "id" is a keyword in Objective-C.
         * @param options The object.
         * @return The handle, or 0 when the id is already taken.
         */
        static int adopt(const std::string& kind, const std::string& objectId,
                         const std::shared_ptr<Options>& options);

        /**
         * The same for a layer or a source built with the object API. The concrete class is
         * recovered at runtime, so an adopted VectorTileLayer answers to its own properties.
         *
         * @return The handle, or 0 when the id is taken or the class is not a wrapped one.
         */
        static int adopt(const std::string& kind, const std::string& objectId,
                         const std::shared_ptr<Layer>& layer);

        /**
         * Registers the map's layer list, so a layer built from a spec can be put on the map:
         * a spec builds an object but does not place it. Call add/remove on the returned handle.
         */
        static int adopt(const std::string& kind, const std::string& objectId,
                         const std::shared_ptr<Layers>& layers);

        /**
         * @copydoc MassifInterop::adopt
         */
        static int adopt(const std::string& kind, const std::string& objectId,
                         const std::shared_ptr<TileDataSource>& source);

        /**
         * The same for a vector data source: a binding-side subclass (the extension mechanism) is adopted here.
         * Named `vectorSource`, not `source`: Objective-C builds selectors from parameter names, and two
         * `source` overloads would collide as `adopt:objectId:source:`.
         */
        static int adopt(const std::string& kind, const std::string& objectId,
                         const std::shared_ptr<VectorDataSource>& vectorSource);

        /**
         * The same for an asset package, typically a binding-side subclass. A spec's `assets` key
         * resolves a string as an id of this kind, e.g. `{"type":"cartocss","css":…,"assets":"shared"}`.
         */
        static int adopt(const std::string& kind, const std::string& objectId,
                         const std::shared_ptr<AssetPackage>& assets);

        /**
         * The map view, which carries the camera: once adopted, moveTo, flyTo, fitBounds, screenToMap,
         * mapToScreen and stopFlight are facade calls, and focusPos, zoom, rotation, tilt and
         * flightActive read-only properties.
         */
        static int adopt(const std::string& kind, const std::string& objectId,
                         const std::shared_ptr<BaseMapView>& view);

        /**
         * Returns a source built or adopted earlier, so it can be handed to the object API.
         */
        static std::shared_ptr<TileDataSource> getSource(const std::string& objectId);

        /**
         * The same by handle, for an object never given an id (a child read off a property, a call result).
         */
        static std::shared_ptr<TileDataSource> getSourceByHandle(int handle);

        /**
         * Returns a layer built earlier, so it can be added to a map with the object API.
         */
        static std::shared_ptr<Layer> getLayer(const std::string& objectId);

        /** @copydoc MassifInterop::getSourceByHandle */
        static std::shared_ptr<Layer> getLayerByHandle(int handle);

        /**
         * Builds the listener that turns a map's callbacks into facade events on a target.
         * Install it with setMapEventListener; it chains the previous listener since that slot is single:
         * `mapView.setMapEventListener(MassifInterop.createEventBridge(handle, mapView.getMapEventListener()))`.
         *
         * @param handle The target events are emitted on.
         * @param chained The listener already installed, or null.
         * @return The bridge.
         */
        static std::shared_ptr<MapEventListener> createEventBridge(
            int handle, const std::shared_ptr<MapEventListener>& chained);

        /**
         * The same for a vector tile layer's clicks; install with setVectorTileEventListener.
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
         * The same for a celestial layer's object clicks; install with setCelestialEventListener.
         */
        static std::shared_ptr<CelestialEventListener> createCelestialEventBridge(
            int handle, const std::shared_ptr<CelestialEventListener>& chained);

    private:
        MassifInterop();
    };

} }

#endif
