#include "CompositeVectorTileLayer.h"
#include "datasources/ContourTileDataSource.h"
#include "layers/RasterTileLayer.h"
#include "layers/HillshadeRasterTileLayer.h"
#include "vectortiles/MBVectorTileDecoder.h"
#include "vectortiles/StyleConfigZoom.h"
#include "rastertiles/ElevationDecoder.h"
#include "rastertiles/TerrariumElevationDataDecoder.h"
#include "rastertiles/MapBoxElevationDataDecoder.h"
#include "renderers/MapRenderer.h"
#include "renderers/TileRenderer.h"
#include "graphics/ViewState.h"
#include "graphics/Color.h"
#include "core/MapRange.h"
#include "core/MapVec.h"
#include "components/Exceptions.h"
#include "utils/FrameProfiler.h"
#include "utils/Log.h"

#include <algorithm>
#include <cmath>
#include <variant>

#include <mapnikvt/Value.h>
#include <mapnikvt/LayerConfigResolver.h>
#include <mapnikvt/ParserUtils.h>

namespace massif {

    namespace {
        float valueToFloat(const mvt::Value& value, float defaultValue) {
            if (auto v = std::get_if<double>(&value))     { return static_cast<float>(*v); }
            if (auto v = std::get_if<long long>(&value))  { return static_cast<float>(*v); }
            if (auto v = std::get_if<bool>(&value))       { return *v ? 1.0f : 0.0f; }
            return defaultValue;
        }

        // Every colour a style sheet can write: hex only turned an hsl() or @variable shade black.
        Color parseColorValue(const mvt::Value& value, const Color& defaultValue) {
            vt::Color color;
            if (auto packed = std::get_if<long long>(&value)) {
                color = vt::Color::fromValue(static_cast<unsigned int>(*packed));
            } else if (auto str = std::get_if<std::string>(&value); !str || !mvt::tryParseColor(*str, color)) {
                return defaultValue;
            }
            // mvt returns rgba() - the translator's spelling of a translucent colour - premultiplied.
            auto str = std::get_if<std::string>(&value);
            if (str && str->compare(0, 5, "rgba(") == 0 && color.alpha() > 0.0f) {
                color = vt::Color(color[0] / color.alpha(), color[1] / color.alpha(), color[2] / color.alpha(), color.alpha());
            }
            std::array<std::uint8_t, 4> rgba = color.rgba8();
            return Color(rgba[0], rgba[1], rgba[2], rgba[3]);
        }

        RasterTileFilterMode::RasterTileFilterMode parseFilterMode(const std::string& mode) {
            if (mode == "nearest") { return RasterTileFilterMode::RASTER_TILE_FILTER_MODE_NEAREST; }
            if (mode == "bicubic") { return RasterTileFilterMode::RASTER_TILE_FILTER_MODE_BICUBIC; }
            return RasterTileFilterMode::RASTER_TILE_FILTER_MODE_BILINEAR;
        }

        HillshadeMethod::HillshadeMethod parseHillshadeMethod(const std::string& method) {
            if (method == "combined")         { return HillshadeMethod::HillshadeMethod::COMBINED; }
            if (method == "igor")             { return HillshadeMethod::HillshadeMethod::IGOR; }
            if (method == "multidirectional") { return HillshadeMethod::HillshadeMethod::MULTIDIRECTIONAL; }
            if (method == "basic")            { return HillshadeMethod::HillshadeMethod::BASIC; }
            return HillshadeMethod::HillshadeMethod::STANDARD;
        }
    }

    CompositeVectorTileLayer::CompositeVectorTileLayer(const std::shared_ptr<TileDataSource>& dataSource, const std::shared_ptr<VectorTileDecoder>& decoder) :
        VectorTileLayer(dataSource, decoder),
        _externalSources(),
        _drawItems(),
        _lastVectorConfig(),
        _componentsSet(false),
        _childOptions(),
        _childMapRenderer(),
        _childTouchHandler(),
        _sourceMutex()
    {
        rebuildDrawItems();
    }

    CompositeVectorTileLayer::~CompositeVectorTileLayer() {
    }

    void CompositeVectorTileLayer::addExternalDataSource(const std::string& name, const std::shared_ptr<TileDataSource>& dataSource, CompositeSourceType::CompositeSourceType type, const std::shared_ptr<ElevationDecoder>& elevationDecoder) {
        if (!dataSource) {
            throw NullArgumentException("Null dataSource");
        }
        if (type == CompositeSourceType::COMPOSITE_SOURCE_TYPE_VECTOR) {
            addVectorDataSource(name, dataSource);
            return;
        }

        std::shared_ptr<Layer> childLayer;
        if (type == CompositeSourceType::COMPOSITE_SOURCE_TYPE_RASTER) {
            childLayer = std::make_shared<RasterTileLayer>(dataSource);
        } else { // HILLSHADE
            std::shared_ptr<ElevationDecoder> elevDecoder = elevationDecoder;
            if (!elevDecoder) {
                elevDecoder = resolveElevationDecoder(dataSource);
            }
            childLayer = elevDecoder ? std::make_shared<HillshadeRasterTileLayer>(dataSource, elevDecoder)
                                     : std::make_shared<HillshadeRasterTileLayer>(dataSource);
        }

        {
            std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
            ExternalSource source { name, type, dataSource, childLayer };
            if (const ExternalSource* previous = findExternalSource(name)) {
                // Replacing a source keeps whatever per-source tile properties were set on it.
                source.zoomLevelBiasSet = previous->zoomLevelBiasSet;
                source.zoomLevelBias = previous->zoomLevelBias;
                source.maxOverzoomLevelSet = previous->maxOverzoomLevelSet;
                source.maxOverzoomLevel = previous->maxOverzoomLevel;
            }
            removeExternalDataSource(name);
            _externalSources.push_back(source);
            applyChildTileProperties(_externalSources.back());
            if (_componentsSet) {
                wireChild(childLayer);
            }
            rebuildDrawItems();
        }
        refresh();
    }

    void CompositeVectorTileLayer::addVectorDataSource(const std::string& name, const std::shared_ptr<TileDataSource>& dataSource) {
        if (!dataSource) {
            throw NullArgumentException("Null dataSource");
        }

        // Its own child layer so it can overzoom independently, e.g. contours rendering z13+ from z12 DEM data.
        auto childVectorLayer = std::make_shared<VectorTileLayer>(dataSource, getTileDecoder());
        // Style names, not layer names: attachments included (see buildFilterString).
        childVectorLayer->setRendererLayerFilter("^(" + name + ")(::.*)?$");
        childVectorLayer->setMaxOverzoomLevel(dataSource->getMaxOverzoomLevel());
        childVectorLayer->setLabelRenderOrder(getLabelRenderOrder());
        childVectorLayer->setBuildingRenderOrder(getBuildingRenderOrder());
        std::shared_ptr<Layer> childLayer = childVectorLayer;

        {
            std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
            ExternalSource source { name, CompositeSourceType::COMPOSITE_SOURCE_TYPE_VECTOR, dataSource, childLayer };
            if (const ExternalSource* previous = findExternalSource(name)) {
                source.zoomLevelBiasSet = previous->zoomLevelBiasSet;
                source.zoomLevelBias = previous->zoomLevelBias;
                source.maxOverzoomLevelSet = previous->maxOverzoomLevelSet;
                source.maxOverzoomLevel = previous->maxOverzoomLevel;
            }
            removeExternalDataSource(name);
            _externalSources.push_back(source);
            applyChildTileProperties(_externalSources.back());
            if (_componentsSet) {
                wireChild(childLayer);
            }
            rebuildDrawItems();
        }
        applyVectorSourceConfigs();
        refresh();
    }

    bool CompositeVectorTileLayer::removeExternalDataSource(const std::string& name) {
        bool removed = false;
        {
            std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
            auto it = std::find_if(_externalSources.begin(), _externalSources.end(), [&](const ExternalSource& s) { return s.name == name; });
            if (it != _externalSources.end()) {
                if (it->childLayer && _componentsSet) {
                    unwireChild(it->childLayer);
                }
                _externalSources.erase(it);
                _lastVectorConfig.erase(name);
                _lastChildConfig.erase(name);
                rebuildDrawItems();
                removed = true;
            }
        }
        if (removed) {
            refresh();
        }
        return removed;
    }

    std::vector<std::string> CompositeVectorTileLayer::getExternalDataSourceNames() const {
        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        std::vector<std::string> names;
        for (const ExternalSource& s : _externalSources) {
            names.push_back(s.name);
        }
        return names;
    }

    std::shared_ptr<Layer> CompositeVectorTileLayer::getExternalChildLayer(const std::string& name) const {
        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        if (const ExternalSource* source = findExternalSource(name)) {
            return source->childLayer;
        }
        return std::shared_ptr<Layer>();
    }

    void CompositeVectorTileLayer::setZoomLevelBias(float bias) {
        VectorTileLayer::setZoomLevelBias(bias);

        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        for (const ExternalSource& s : _externalSources) {
            applyChildTileProperties(s);
        }
        for (const DrawItem& item : _drawItems) {
            if (auto groupLayer = std::dynamic_pointer_cast<TileLayer>(item.groupLayer)) {
                groupLayer->setZoomLevelBias(bias);
            }
        }
    }

    void CompositeVectorTileLayer::setPreloading(bool preloading) {
        VectorTileLayer::setPreloading(preloading);

        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        for (const ExternalSource& s : _externalSources) {
            applyChildTileProperties(s);
        }
        for (const DrawItem& item : _drawItems) {
            if (auto groupLayer = std::dynamic_pointer_cast<TileLayer>(item.groupLayer)) {
                groupLayer->setPreloading(preloading);
            }
        }
    }

    void CompositeVectorTileLayer::setOpacity(float opacity) {
        VectorTileLayer::setOpacity(opacity);

        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        for (const DrawItem& item : _drawItems) {
            if (item.groupLayer) {
                item.groupLayer->setOpacity(opacity);
            }
            if (item.slotLayer) {
                item.slotLayer->setOpacity(opacity);
            }
        }
    }

    // Group layers (everything above the first external slot) answer no click without a listener.
    // External children are left out: a click on them is on their own source's features.
    void CompositeVectorTileLayer::setVectorTileEventListener(const std::shared_ptr<VectorTileEventListener>& eventListener) {
        VectorTileLayer::setVectorTileEventListener(eventListener);

        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        for (const DrawItem& item : _drawItems) {
            if (auto groupLayer = std::dynamic_pointer_cast<VectorTileLayer>(item.groupLayer)) {
                groupLayer->setVectorTileEventListener(eventListener);
            }
        }
    }

    void CompositeVectorTileLayer::setClickRadius(float radius) {
        VectorTileLayer::setClickRadius(radius);

        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        for (const DrawItem& item : _drawItems) {
            if (auto groupLayer = std::dynamic_pointer_cast<VectorTileLayer>(item.groupLayer)) {
                groupLayer->setClickRadius(radius);
            }
        }
    }

    void CompositeVectorTileLayer::setClickHandlerLayerFilter(const std::string& filter) {
        VectorTileLayer::setClickHandlerLayerFilter(filter);

        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        for (const DrawItem& item : _drawItems) {
            if (auto groupLayer = std::dynamic_pointer_cast<VectorTileLayer>(item.groupLayer)) {
                groupLayer->setClickHandlerLayerFilter(filter);
            }
        }
    }

    void CompositeVectorTileLayer::setExternalDataSourceZoomLevelBias(const std::string& name, float bias) {
        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        ExternalSource& source = getExternalSource(name);
        source.zoomLevelBiasSet = true;
        source.zoomLevelBias = bias;
        applyChildTileProperties(source);
    }

    float CompositeVectorTileLayer::getExternalDataSourceZoomLevelBias(const std::string& name) const {
        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        const ExternalSource& source = getExternalSource(name);
        return source.zoomLevelBiasSet ? source.zoomLevelBias : getZoomLevelBias();
    }

    void CompositeVectorTileLayer::clearExternalDataSourceZoomLevelBias(const std::string& name) {
        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        ExternalSource& source = getExternalSource(name);
        source.zoomLevelBiasSet = false;
        source.zoomLevelBias = 0.0f;
        applyChildTileProperties(source);
    }

    void CompositeVectorTileLayer::setExternalDataSourceMaxOverzoomLevel(const std::string& name, int level) {
        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        ExternalSource& source = getExternalSource(name);
        source.maxOverzoomLevelSet = true;
        source.maxOverzoomLevel = level;
        applyChildTileProperties(source);
    }

    int CompositeVectorTileLayer::getExternalDataSourceMaxOverzoomLevel(const std::string& name) const {
        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        const ExternalSource& source = getExternalSource(name);
        if (source.maxOverzoomLevelSet) {
            return source.maxOverzoomLevel;
        }
        auto childLayer = std::dynamic_pointer_cast<TileLayer>(source.childLayer);
        return childLayer ? childLayer->getMaxOverzoomLevel() : getMaxOverzoomLevel();
    }

    void CompositeVectorTileLayer::applyChildTileProperties(const ExternalSource& source) {
        std::vector<std::shared_ptr<TileLayer> > tileLayers { std::dynamic_pointer_cast<TileLayer>(source.childLayer) };
        for (const DrawItem& item : _drawItems) {
            if (item.slotLayer && item.slot == source.name) {
                tileLayers.push_back(std::dynamic_pointer_cast<TileLayer>(item.slotLayer));
            }
        }
        for (const std::shared_ptr<TileLayer>& tileLayer : tileLayers) {
            if (!tileLayer) {
                continue;
            }
            tileLayer->setPreloading(isPreloading());
            tileLayer->setZoomLevelBias(source.zoomLevelBiasSet ? source.zoomLevelBias : getZoomLevelBias());
            if (source.maxOverzoomLevelSet) {
                tileLayer->setMaxOverzoomLevel(source.maxOverzoomLevel);
            }
        }
    }

    std::shared_ptr<ElevationDecoder> CompositeVectorTileLayer::resolveElevationDecoder(const std::shared_ptr<TileDataSource>& dataSource) {
        if (!dataSource || !dataSource->containsMetaDataKey(ElevationDecoder::ENCODING_KEY)) {
            return std::shared_ptr<ElevationDecoder>(); // let HillshadeRasterTileLayer infer per tile
        }
        return ElevationDecoder::Resolve(std::shared_ptr<TileData>(), dataSource, std::shared_ptr<ElevationDecoder>());
    }

    std::string CompositeVectorTileLayer::buildFilterString(const std::vector<std::string>& group, bool includeBackground) {
        // Full regex_match; the background layer has an empty name, which non-bottom groups must not match
        // or they paint the background over earlier groups.
        if (group.empty()) {
            // "[^\\s\\S]" matches no string, not even "".
            return includeBackground ? "^$" : "[^\\s\\S]";
        }
        // Matched against style names: each CartoCSS attachment is its own "layer::attachment" style.
        std::string pattern = "^((";
        for (std::size_t i = 0; i < group.size(); i++) {
            pattern += (i ? "|" : "") + group[i];
        }
        pattern += ")(::.*)?";
        if (includeBackground) {
            pattern += "|"; // matches the empty-named background layer
        }
        pattern += ")$";
        return pattern;
    }

    void CompositeVectorTileLayer::wireChild(const std::shared_ptr<Layer>& child) {
        if (!child) {
            return;
        }
        child->setComponents(_envelopeThreadPool, _tileThreadPool, _childOptions, _childMapRenderer, _childTouchHandler);
        child->registerDataSourceListener();
    }

    void CompositeVectorTileLayer::unwireChild(const std::shared_ptr<Layer>& child) {
        if (!child) {
            return;
        }
        child->unregisterDataSourceListener();
    }

    std::shared_ptr<Layer> CompositeVectorTileLayer::makeGroupLayer(const std::string& filter) {
        // Shares the master data source so a source change reloads all groups.
        auto groupLayer = std::make_shared<VectorTileLayer>(getDataSource(), getTileDecoder());
        groupLayer->setRendererLayerFilter(filter);
        groupLayer->setLabelRenderOrder(getLabelRenderOrder());
        groupLayer->setBuildingRenderOrder(getBuildingRenderOrder());
        // The groups render the same source as this layer, so they must select the same tiles.
        groupLayer->setZoomLevelBias(getZoomLevelBias());
        groupLayer->setPreloading(isPreloading());
        groupLayer->setOpacity(getOpacity());
        // Groups are rebuilt on every style or slot change, long after the app set its click state.
        groupLayer->setClickRadius(getClickRadius());
        groupLayer->setClickHandlerLayerFilter(getClickHandlerLayerFilter());
        groupLayer->setVectorTileEventListener(getVectorTileEventListener());
        std::shared_ptr<Layer> child = groupLayer;
        if (_componentsSet) {
            wireChild(child);
        }
        return child;
    }

    std::shared_ptr<Layer> CompositeVectorTileLayer::makeSlotLayer(const ExternalSource& source, const std::vector<std::string>& styleNames) {
        auto slotLayer = std::make_shared<VectorTileLayer>(source.dataSource, getTileDecoder());
        std::string filter = "^(";
        for (std::size_t i = 0; i < styleNames.size(); i++) {
            filter += (i ? "|" : "") + styleNames[i];
        }
        slotLayer->setRendererLayerFilter(filter + ")$");
        slotLayer->setMaxOverzoomLevel(source.dataSource->getMaxOverzoomLevel());
        slotLayer->setLabelRenderOrder(getLabelRenderOrder());
        slotLayer->setBuildingRenderOrder(getBuildingRenderOrder());
        slotLayer->setVisibleZoomRange(source.childLayer->getVisibleZoomRange());
        slotLayer->setOpacity(getOpacity());
        std::shared_ptr<Layer> child = slotLayer;
        if (_componentsSet) {
            wireChild(child);
        }
        return child;
    }

    void CompositeVectorTileLayer::applyExternalChildZoomRange(const ExternalSource& source) {
        auto decoder = std::dynamic_pointer_cast<MBVectorTileDecoder>(getTileDecoder());
        if (!decoder || !source.childLayer) {
            return;
        }
        std::vector<int> range = decoder->getStyleLayerZoomRange(source.name);
        if (range.size() == 2) {
            source.childLayer->setVisibleZoomRange(MapRange(static_cast<float>(range[0]), static_cast<float>(range[1])));
        }
    }

    // Memoised: resolving walks the style under the decoder's mutex, twice a frame per draw item.
    // Caller holds _sourceMutex; the version covers live parameter changes.
    mvt::ResolvedLayerConfig CompositeVectorTileLayer::resolveLayerConfigCached(const std::shared_ptr<MBVectorTileDecoder>& decoder, const std::string& slot, float viewZoom) {
        // Resolved at the quantised zoom so the cached value matches its key. See StyleConfigZoom.
        float zoom = StyleConfigZoom::quantise(viewZoom);
        unsigned int version = decoder->getConfigVersion();
        auto it = _resolvedConfigCache.find(slot);
        if (it != _resolvedConfigCache.end() && it->second.version == version && it->second.viewZoom == zoom) {
            return it->second.config;
        }
        mvt::ResolvedLayerConfig config = decoder->resolveLayerConfig(slot, zoom);
        _resolvedConfigCache[slot] = ResolvedConfigEntry { version, zoom, config };
        return config;
    }


    void CompositeVectorTileLayer::rebuildDrawItems() {
        // Caller holds _sourceMutex (or is the constructor).
        _resolvedConfigCache.clear();

        for (const DrawItem& item : _drawItems) {
            if (item.groupLayer && _componentsSet) {
                unwireChild(item.groupLayer);
            }
            if (item.slotLayer && _componentsSet) {
                unwireChild(item.slotLayer);
            }
        }
        _drawItems.clear();

        auto decoder = std::dynamic_pointer_cast<MBVectorTileDecoder>(getTileDecoder());
        if (!decoder) {
            VectorTileLayer::setRendererLayerFilter("");
            snapshotChildTileLayers();
            return;
        }
        std::vector<std::string> order = decoder->getStyleLayerNames();

        for (const ExternalSource& s : _externalSources) {
            if (s.childLayer) {
                applyExternalChildZoomRange(s);
            }
        }

        auto isChildSlot = [&](const std::string& layerName) {
            const ExternalSource* s = findExternalSource(layerName);
            return s && s->childLayer;
        };

        // A vector slot listed at several depths (lines under the roads, labels with the names) draws each
        // entry's own styles where that entry stands; its child would draw all of them at every one.
        std::vector<std::vector<std::string> > styleNames = decoder->getStyleLayerStyleNames();
        std::map<std::string, int> occurrences;
        for (const std::string& layerName : order) {
            occurrences[layerName]++;
        }

        std::vector<std::string> group;
        bool firstSlotSeen = false;
        std::map<std::size_t, std::vector<std::string> > depthStyleNames; // by draw item
        for (std::size_t i = 0; i < order.size(); i++) {
            const std::string& layerName = order[i];
            if (isChildSlot(layerName)) {
                const ExternalSource* source = findExternalSource(layerName);
                bool atDepths = source->type == CompositeSourceType::COMPOSITE_SOURCE_TYPE_VECTOR && occurrences[layerName] > 1 && i < styleNames.size();
                // Consecutive entries of one slot are one depth: one layer, one decode.
                if (atDepths && group.empty() && !_drawItems.empty() && depthStyleNames.count(_drawItems.size() - 1) > 0 && _drawItems.back().slot == layerName) {
                    std::vector<std::string>& names = depthStyleNames[_drawItems.size() - 1];
                    names.insert(names.end(), styleNames[i].begin(), styleNames[i].end());
                    continue;
                }
                if (!firstSlotSeen) {
                    // Group 0 renders on this layer and alone draws the style background, once at the bottom.
                    VectorTileLayer::setRendererLayerFilter(buildFilterString(group, /*includeBackground=*/true));
                    firstSlotSeen = true;
                } else if (!group.empty()) {
                    // Empty intermediate groups get no layer at all: nothing to fetch, decode or overpaint.
                    _drawItems.push_back({ DRAW_ITEM_VT_GROUP, std::string(), makeGroupLayer(buildFilterString(group)) });
                }
                if (atDepths) {
                    depthStyleNames[_drawItems.size()] = styleNames[i];
                }
                _drawItems.push_back({ DRAW_ITEM_EXTERNAL, layerName, std::shared_ptr<Layer>() });
                group.clear();
            } else {
                group.push_back(layerName);
            }
        }
        // Reverse, so erasing a depth that draws nothing leaves the indexes still to visit in place.
        for (auto it = depthStyleNames.rbegin(); it != depthStyleNames.rend(); it++) {
            if (it->second.empty()) {
                _drawItems.erase(_drawItems.begin() + it->first);
            } else {
                _drawItems[it->first].slotLayer = makeSlotLayer(*findExternalSource(_drawItems[it->first].slot), it->second);
            }
        }
        if (!firstSlotSeen) {
            VectorTileLayer::setRendererLayerFilter("");
        } else if (!group.empty()) {
            _drawItems.push_back({ DRAW_ITEM_VT_GROUP, std::string(), makeGroupLayer(buildFilterString(group)) });
        }

        for (const ExternalSource& s : _externalSources) {
            if (s.childLayer && std::find(order.begin(), order.end(), s.name) == order.end()) {
                Log::Warnf("CompositeVectorTileLayer: external source '%s' is not listed in the style 'layers' - it will not be drawn", s.name.c_str());
            }
            applyChildTileProperties(s);
        }
        // A slot between the buildings and the POIs puts them in different groups, each its own renderer.
        std::vector<std::weak_ptr<TileRenderer>> peers { _tileRenderer };
        for (const DrawItem& item : _drawItems) {
            if (std::shared_ptr<TileLayer> groupLayer = std::dynamic_pointer_cast<TileLayer>(item.groupLayer)) {
                peers.push_back(groupLayer->_tileRenderer);
            }
        }
        for (const std::weak_ptr<TileRenderer>& peer : peers) {
            peer.lock()->setExtrusionPeers(peers);
        }
        snapshotChildTileLayers();
    }

    void CompositeVectorTileLayer::snapshotChildTileLayers() {
        // Caller holds _sourceMutex. Readers hold MapRenderer::_mutex, which a cull under _sourceMutex
        // also takes, so readers must not take _sourceMutex (deadlock).
        std::vector<std::shared_ptr<TileLayer> > children;
        for (const ExternalSource& s : _externalSources) {
            if (auto childTileLayer = std::dynamic_pointer_cast<TileLayer>(s.childLayer)) {
                children.push_back(childTileLayer);
            }
        }
        for (const DrawItem& item : _drawItems) {
            if (auto groupTileLayer = std::dynamic_pointer_cast<TileLayer>(item.groupLayer)) {
                children.push_back(groupTileLayer);
            }
            if (auto slotTileLayer = std::dynamic_pointer_cast<TileLayer>(item.slotLayer)) {
                children.push_back(slotTileLayer);
            }
        }
        std::lock_guard<std::mutex> lock(_childTileLayersMutex);
        _childTileLayers.swap(children);
    }

    void CompositeVectorTileLayer::setComponents(const std::shared_ptr<CancelableThreadPool>& envelopeThreadPool,
                                                 const std::shared_ptr<CancelableThreadPool>& tileThreadPool,
                                                 const std::weak_ptr<Options>& options,
                                                 const std::weak_ptr<MapRenderer>& mapRenderer,
                                                 const std::weak_ptr<TouchHandler>& touchHandler) {
        VectorTileLayer::setComponents(envelopeThreadPool, tileThreadPool, options, mapRenderer, touchHandler);

        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        _childOptions = options;
        _childMapRenderer = mapRenderer;
        _childTouchHandler = touchHandler;
        _componentsSet = true;
        for (const ExternalSource& s : _externalSources) {
            if (s.childLayer) {
                wireChild(s.childLayer);
            }
        }
        for (const DrawItem& item : _drawItems) {
            if (item.groupLayer) {
                wireChild(item.groupLayer);
            }
            if (item.slotLayer) {
                wireChild(item.slotLayer);
            }
        }
    }

    void CompositeVectorTileLayer::loadData(const std::shared_ptr<CullState>& cullState) {
        VectorTileLayer::loadData(cullState);

        // A style or parameter change reloads tiles through here, so re-apply the contour generation parameters.
        applyVectorSourceConfigs();

        // _sourceMutex guards which children to load, not the loading, which would block the render thread.
        std::vector<std::shared_ptr<Layer> > loadLayers;
        {
            std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
            for (const ExternalSource& s : _externalSources) {
                // A source the style's 'layers' never mentions is not drawn, so it must not fetch either.
                if (s.childLayer && isDrawnByChild(s.name)) {
                    loadLayers.push_back(s.childLayer);
                }
            }
            for (const DrawItem& item : _drawItems) {
                if (item.groupLayer) {
                    loadLayers.push_back(item.groupLayer);
                }
                if (item.slotLayer) {
                    loadLayers.push_back(item.slotLayer);
                }
            }
        }
        for (const std::shared_ptr<Layer>& loadLayer : loadLayers) {
            loadLayer->loadData(cullState);
        }
    }

    void CompositeVectorTileLayer::offsetLayerHorizontally(double offset) {
        VectorTileLayer::offsetLayerHorizontally(offset);

        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        for (const ExternalSource& s : _externalSources) {
            if (s.childLayer) {
                s.childLayer->offsetLayerHorizontally(offset);
            }
        }
        for (const DrawItem& item : _drawItems) {
            if (item.groupLayer) {
                item.groupLayer->offsetLayerHorizontally(offset);
            }
            if (item.slotLayer) {
                item.slotLayer->offsetLayerHorizontally(offset);
            }
        }
    }

    bool CompositeVectorTileLayer::isUpdateInProgress() const {
        if (VectorTileLayer::isUpdateInProgress()) {
            return true;
        }
        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        for (const ExternalSource& s : _externalSources) {
            if (s.childLayer && s.childLayer->isUpdateInProgress()) {
                return true;
            }
        }
        for (const DrawItem& item : _drawItems) {
            if ((item.groupLayer && item.groupLayer->isUpdateInProgress()) || (item.slotLayer && item.slotLayer->isUpdateInProgress())) {
                return true;
            }
        }
        return false;
    }

    bool CompositeVectorTileLayer::isTerrainDecodeSettled() {
        // Children re-decode on the same switch but are not in Layers themselves.
        bool settled = VectorTileLayer::isTerrainDecodeSettled();
        std::vector<std::shared_ptr<TileLayer> > children;
        {
            std::lock_guard<std::mutex> lock(_childTileLayersMutex);
            children = _childTileLayers;
        }
        for (const std::shared_ptr<TileLayer>& childTileLayer : children) {
            settled = childTileLayer->isTerrainDecodeSettled() && settled;
        }
        return settled;
    }

    int CompositeVectorTileLayer::getTerrainDecodePendingCount() const {
        int count = VectorTileLayer::getTerrainDecodePendingCount();
        std::vector<std::shared_ptr<TileLayer> > children;
        {
            std::lock_guard<std::mutex> lock(_childTileLayersMutex);
            children = _childTileLayers;
        }
        for (const std::shared_ptr<TileLayer>& childTileLayer : children) {
            int childCount = childTileLayer->getTerrainDecodePendingCount();
            count = childCount < 0 || count < 0 ? -1 : count + childCount;
        }
        return count;
    }

    void CompositeVectorTileLayer::calculateRayIntersectedElements(const cglib::ray3<double>& ray, const ViewState& viewState, std::vector<RayIntersectedElement>& results) const {
        VectorTileLayer::calculateRayIntersectedElements(ray, viewState, results);

        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        for (const ExternalSource& s : _externalSources) {
            if (s.childLayer) {
                s.childLayer->calculateRayIntersectedElements(ray, viewState, results);
            }
        }
        for (const DrawItem& item : _drawItems) {
            if (item.groupLayer) {
                item.groupLayer->calculateRayIntersectedElements(ray, viewState, results);
            }
            if (item.slotLayer) {
                item.slotLayer->calculateRayIntersectedElements(ray, viewState, results);
            }
        }
    }

    bool CompositeVectorTileLayer::onDrawFrame(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState) {
        return renderComposite(deltaSeconds, billboardSorter, viewState, false);
    }

    bool CompositeVectorTileLayer::onDrawFrame3D(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState) {
        return renderComposite(deltaSeconds, billboardSorter, viewState, true);
    }

    const CompositeVectorTileLayer::ExternalSource* CompositeVectorTileLayer::findExternalSource(const std::string& name) const {
        auto it = std::find_if(_externalSources.begin(), _externalSources.end(), [&](const ExternalSource& s) { return s.name == name; });
        return it != _externalSources.end() ? &(*it) : nullptr;
    }

    CompositeVectorTileLayer::ExternalSource* CompositeVectorTileLayer::findExternalSource(const std::string& name) {
        auto it = std::find_if(_externalSources.begin(), _externalSources.end(), [&](const ExternalSource& s) { return s.name == name; });
        return it != _externalSources.end() ? &(*it) : nullptr;
    }

    CompositeVectorTileLayer::ExternalSource& CompositeVectorTileLayer::getExternalSource(const std::string& name) {
        if (ExternalSource* source = findExternalSource(name)) {
            return *source;
        }
        throw InvalidArgumentException("No external data source named " + name);
    }

    const CompositeVectorTileLayer::ExternalSource& CompositeVectorTileLayer::getExternalSource(const std::string& name) const {
        if (const ExternalSource* source = findExternalSource(name)) {
            return *source;
        }
        throw InvalidArgumentException("No external data source named " + name);
    }

    bool CompositeVectorTileLayer::isDrawnSlot(const std::string& name) const {
        // Caller holds _sourceMutex. A source with no draw item is not in the style's 'layers'.
        for (const DrawItem& item : _drawItems) {
            if (item.kind == DRAW_ITEM_EXTERNAL && item.slot == name) {
                return true;
            }
        }
        return false;
    }

    bool CompositeVectorTileLayer::isDrawnByChild(const std::string& name) const {
        for (const DrawItem& item : _drawItems) {
            if (item.kind == DRAW_ITEM_EXTERNAL && item.slot == name && !item.slotLayer) {
                return true;
            }
        }
        return false;
    }

    void CompositeVectorTileLayer::applyConfig(const ExternalSource& source, const mvt::ResolvedLayerConfig& config, const ViewState& viewState) {
        auto getValue = [&](const std::string& key) -> const mvt::Value* {
            auto it = config.values.find(key);
            return it != config.values.end() ? &it->second : nullptr;
        };
        // Setters baked into the normal map re-decode the tile, so they apply only at integer zoom changes
        // (where tiles reload anyway) or a zoom-interpolated value would never settle.
        std::map<std::string, double>& applied = _lastChildConfig[source.name];
        int intZoom = static_cast<int>(std::floor(viewState.getZoom()));
        bool decodeZoomChanged = (applied.find("__izoom") == applied.end()) || (static_cast<int>(applied["__izoom"]) != intZoom);
        applied["__izoom"] = static_cast<double>(intZoom);

        // Every setter below calls Layer::redraw(): applying unchanged values would keep the map from going idle.
        auto changed = [&applied](const std::string& key, double value) {
            auto it = applied.find(key);
            if (it != applied.end() && it->second == value) {
                return false;
            }
            applied[key] = value;
            return true;
        };

        // A style value wins over setExternalDataSourceZoomLevelBias / MaxOverzoomLevel while present.
        const mvt::Value* biasValue = getValue("zoom-level-bias");
        const mvt::Value* overzoomValue = getValue("max-overzoom-level");
        if (biasValue || overzoomValue) {
            auto childTileLayer = std::dynamic_pointer_cast<TileLayer>(source.childLayer);
            if (childTileLayer && biasValue) {
                float bias = valueToFloat(*biasValue, 0.0f);
                if (changed("zoom-level-bias", bias)) { childTileLayer->setZoomLevelBias(bias); }
            }
            if (childTileLayer && overzoomValue) {
                int level = static_cast<int>(valueToFloat(*overzoomValue, 0.0f));
                if (changed("max-overzoom-level", level)) { childTileLayer->setMaxOverzoomLevel(level); }
            }
        }

        if (source.type == CompositeSourceType::COMPOSITE_SOURCE_TYPE_RASTER) {
            auto raster = std::static_pointer_cast<RasterTileLayer>(source.childLayer);
            if (const mvt::Value* v = getValue("opacity")) {
                float opacity = valueToFloat(*v, 1.0f);
                if (changed("opacity", opacity)) { raster->setOpacity(opacity); }
            }
            if (const mvt::Value* v = getValue("filter-mode")) {
                if (auto str = std::get_if<std::string>(v)) {
                    RasterTileFilterMode::RasterTileFilterMode mode = parseFilterMode(*str);
                    if (changed("filter-mode", static_cast<double>(mode))) { raster->setTileFilterMode(mode); }
                }
            }
        } else if (source.type == CompositeSourceType::COMPOSITE_SOURCE_TYPE_HILLSHADE) {
            auto hillshade = std::static_pointer_cast<HillshadeRasterTileLayer>(source.childLayer);
            if (const mvt::Value* v = getValue("opacity")) {
                float opacity = valueToFloat(*v, 1.0f);
                if (changed("opacity", opacity)) { hillshade->setOpacity(opacity); }
            }
            if (const mvt::Value* v = getValue("exaggeration")) {
                float exaggeration = valueToFloat(*v, 1.0f);
                if (changed("exaggeration", exaggeration)) { hillshade->setExaggeration(exaggeration); }
            }

            if (decodeZoomChanged) {
                if (const mvt::Value* v = getValue("height-scale")) {
                    float heightScale = valueToFloat(*v, 1.0f);
                    if (changed("height-scale", heightScale)) { hillshade->setHeightScale(heightScale); }
                }
                if (const mvt::Value* v = getValue("contrast")) {
                    float contrast = valueToFloat(*v, 0.5f);
                    if (changed("contrast", contrast)) { hillshade->setContrast(contrast); }
                }
                if (const mvt::Value* v = getValue("contour-interval")) {
                    float interval = valueToFloat(*v, 0.0f);
                    hillshade->setContourEnabled(interval > 0.0f);
                    if (interval > 0.0f) { hillshade->setContourInterval(interval); }
                }
            }

            if (const mvt::Value* v = getValue("shadow-color")) {
                Color color = parseColorValue(*v, Color(0, 0, 0, 255));
                if (changed("shadow-color", color.getARGB())) { hillshade->setShadowColor(color); }
            }
            if (const mvt::Value* v = getValue("highlight-color")) {
                Color color = parseColorValue(*v, Color(255, 255, 255, 255));
                if (changed("highlight-color", color.getARGB())) { hillshade->setHighlightColor(color); }
            }
            if (const mvt::Value* v = getValue("accent-color")) {
                Color color = parseColorValue(*v, Color(0, 0, 0, 255));
                if (changed("accent-color", color.getARGB())) { hillshade->setAccentColor(color); }
            }
            if (const mvt::Value* v = getValue("method")) {
                if (auto str = std::get_if<std::string>(v)) {
                    HillshadeMethod::HillshadeMethod method = parseHillshadeMethod(*str);
                    if (changed("method", static_cast<double>(method))) { hillshade->setHillshadeMethod(method); }
                }
            }
            if (const mvt::Value* v = getValue("contour-color")) {
                Color color = parseColorValue(*v, Color(0xC5, 0x60, 0x08, 0xff));
                if (changed("contour-color", color.getARGB())) { hillshade->setContourColor(color); }
            }
            if (const mvt::Value* v = getValue("contour-width")) {
                float width = valueToFloat(*v, 1.0f);
                if (changed("contour-width", width)) { hillshade->setContourWidth(width); }
            }
            if (const mvt::Value* v = getValue("illumination-direction")) {
                // Azimuth in degrees (0 = north, clockwise) at a fixed 45 deg altitude: (sin az, cos az, -sin alt).
                float azimuthDegrees = valueToFloat(*v, 335.0f);
                if (changed("illumination-direction", azimuthDegrees)) {
                    double azimuth = azimuthDegrees * M_PI / 180.0;
                    hillshade->setIlluminationDirection(MapVec(std::sin(azimuth), std::cos(azimuth), -0.70710678));
                }
            }
        }
    }

    void CompositeVectorTileLayer::applyVectorSourceConfigs() {
        auto decoder = std::dynamic_pointer_cast<MBVectorTileDecoder>(getTileDecoder());
        if (!decoder) {
            return;
        }

        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        for (const ExternalSource& s : _externalSources) {
            if (s.type != CompositeSourceType::COMPOSITE_SOURCE_TYPE_VECTOR) {
                continue;
            }
            auto contour = std::dynamic_pointer_cast<ContourTileDataSource>(s.dataSource);
            if (!contour) {
                continue;
            }
            // Changing generation parameters regenerates tiles, so they are evaluated at a neutral zoom, not per frame.
            mvt::ResolvedLayerConfig config = decoder->resolveLayerConfig(s.name, 0.0f);
            std::map<std::string, float>& applied = _lastVectorConfig[s.name];

            auto changed = [&](const std::string& key, float& outValue) {
                auto it = config.values.find(key);
                if (it == config.values.end()) {
                    return false;
                }
                float value = valueToFloat(it->second, 0.0f);
                auto ait = applied.find(key);
                if (ait != applied.end() && ait->second == value) {
                    return false;
                }
                applied[key] = value;
                outValue = value;
                return true;
            };

            float value = 0.0f;
            if (changed("base-interval", value))      { contour->setBaseInterval(value); }
            if (changed("resolution", value))         { contour->setResolution(static_cast<int>(value)); }
            if (changed("min-visible-zoom", value))   { contour->setMinVisibleZoom(static_cast<int>(value)); }
            if (changed("simplify-tolerance", value)) { contour->setSimplifyTolerance(value); }
            if (changed("label-stubs", value))        { contour->setLabelStubsEnabled(value != 0.0f); }
            if (changed("label-interval", value))     { contour->setLabelInterval(value); }
        }
    }

    void CompositeVectorTileLayer::collectDrapeLayers(std::vector<std::shared_ptr<TileLayer> >& drapeLayers, const ViewState& viewState) {
        // Same order and gating as renderComposite, or the children keep their own pre-pass and depth domain.
        TileLayer::collectDrapeLayers(drapeLayers, viewState);
        if (!isVisible()) {
            return;
        }

        auto decoder = std::dynamic_pointer_cast<MBVectorTileDecoder>(getTileDecoder());

        // Timed apart: a cull holds _sourceMutex for a whole tile-set refresh.
        FRAME_PROF_NOW(profLayerLockStart);
        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        FRAME_PROF_ADD(prePaintLayerLockMs, profLayerLockStart);
        for (const DrawItem& item : _drawItems) {
            std::shared_ptr<Layer> childLayer;
            if (item.kind == DRAW_ITEM_VT_GROUP) {
                childLayer = item.groupLayer;
            } else if (const ExternalSource* source = findExternalSource(item.slot)) {
                // The drape runs before the frame: a child skipped only in renderComposite would still be baked.
                if (!source->childLayer || !source->childLayer->isVisible()) {
                    continue;
                }
                // Config gating and applying as in renderComposite, since the bake runs before it.
                if (source->type != CompositeSourceType::COMPOSITE_SOURCE_TYPE_VECTOR && decoder) {
                    FRAME_PROF_NOW(profConfigStart);
                    mvt::ResolvedLayerConfig config = resolveLayerConfigCached(decoder, item.slot, viewState.getZoom());
                    FRAME_PROF_ADD(prePaintConfigMs, profConfigStart);
                    FRAME_PROF_NOW(profApplyStart);
                    applyConfig(*source, config, viewState);
                    FRAME_PROF_ADD(prePaintApplyMs, profApplyStart);
                    if (!config.visible) {
                        continue;
                    }
                }
                childLayer = item.slotLayer ? item.slotLayer : source->childLayer;
            }
            if (childLayer) {
                childLayer->collectDrapeLayers(drapeLayers, viewState);
            }
        }
    }

    void CompositeVectorTileLayer::collectLabelLayers(std::vector<std::shared_ptr<VectorTileLayer> >& labelLayers) {
        // Same order as renderComposite: the culler grid accumulates across layers, so order decides which labels win.
        VectorTileLayer::collectLabelLayers(labelLayers);
        if (!isVisible()) {
            return;
        }

        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        for (const DrawItem& item : _drawItems) {
            std::shared_ptr<Layer> childLayer;
            if (item.kind == DRAW_ITEM_VT_GROUP) {
                childLayer = item.groupLayer;
            } else if (const ExternalSource* source = findExternalSource(item.slot)) {
                // A hidden child's labels would otherwise stay on screen and win culler slots.
                if (source->childLayer && source->childLayer->isVisible()) {
                    childLayer = item.slotLayer ? item.slotLayer : source->childLayer;
                }
            }
            if (childLayer) {
                childLayer->collectLabelLayers(labelLayers);
            }
        }
    }

    bool CompositeVectorTileLayer::renderComposite(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState, bool terrain) {
        // Hiding only stops culling; group layers and external children would keep drawing their tiles.
        if (!isVisible() || getOpacity() <= 0) {
            return false;
        }

        auto decoder = std::dynamic_pointer_cast<MBVectorTileDecoder>(getTileDecoder());

        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);

        // Group 0 (filter set in rebuildDrawItems).
        bool refresh = terrain ? VectorTileLayer::onDrawFrame3D(deltaSeconds, billboardSorter, viewState)
                               : VectorTileLayer::onDrawFrame(deltaSeconds, billboardSorter, viewState);

        for (const DrawItem& item : _drawItems) {
            if (item.kind == DRAW_ITEM_VT_GROUP) {
                if (item.groupLayer) {
                    refresh = (terrain ? item.groupLayer->onDrawFrame3D(deltaSeconds, billboardSorter, viewState)
                                       : item.groupLayer->onDrawFrame(deltaSeconds, billboardSorter, viewState)) || refresh;
                }
                continue;
            }
            const ExternalSource* source = findExternalSource(item.slot);
            if (!source || !source->childLayer) {
                continue;
            }
            // Hiding a child only stops it loading; without this it keeps drawing the tiles it already had.
            bool visible = source->childLayer->isVisible();
            // Vector children have no config symbolizer: their own decode already zoom-filters them.
            if (visible && source->type != CompositeSourceType::COMPOSITE_SOURCE_TYPE_VECTOR && decoder) {
                mvt::ResolvedLayerConfig config = resolveLayerConfigCached(decoder, item.slot, viewState.getZoom());
                applyConfig(*source, config, viewState);
                visible = config.visible;
            }
            if (visible) {
                const std::shared_ptr<Layer>& drawLayer = item.slotLayer ? item.slotLayer : source->childLayer;
                refresh = (terrain ? drawLayer->onDrawFrame3D(deltaSeconds, billboardSorter, viewState)
                                   : drawLayer->onDrawFrame(deltaSeconds, billboardSorter, viewState)) || refresh;
            }
        }
        return refresh;
    }

}
