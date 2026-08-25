#include "CompositeVectorTileLayer.h"
#include "datasources/ContourTileDataSource.h"
#include "layers/RasterTileLayer.h"
#include "layers/HillshadeRasterTileLayer.h"
#include "vectortiles/MBVectorTileDecoder.h"
#include "rastertiles/ElevationDecoder.h"
#include "rastertiles/TerrariumElevationDataDecoder.h"
#include "rastertiles/MapBoxElevationDataDecoder.h"
#include "renderers/MapRenderer.h"
#include "graphics/ViewState.h"
#include "graphics/Color.h"
#include "core/MapRange.h"
#include "core/MapVec.h"
#include "components/Exceptions.h"
#include "utils/Log.h"

#include <algorithm>
#include <cmath>
#include <variant>

#include <mapnikvt/Value.h>
#include <mapnikvt/LayerConfigResolver.h>

namespace massif {

    namespace {
        float valueToFloat(const mvt::Value& value, float defaultValue) {
            if (auto v = std::get_if<double>(&value))     { return static_cast<float>(*v); }
            if (auto v = std::get_if<long long>(&value))  { return static_cast<float>(*v); }
            if (auto v = std::get_if<bool>(&value))       { return *v ? 1.0f : 0.0f; }
            return defaultValue;
        }

        Color parseColorValue(const mvt::Value& value, const Color& defaultValue) {
            const std::string* str = std::get_if<std::string>(&value);
            if (!str || str->empty() || (*str)[0] != '#') {
                return defaultValue;
            }
            std::string hex = str->substr(1);
            unsigned long packed = 0;
            try {
                packed = std::stoul(hex, nullptr, 16);
            } catch (const std::exception&) {
                return defaultValue;
            }
            if (hex.size() == 6) {
                return Color(static_cast<unsigned char>((packed >> 16) & 0xff),
                             static_cast<unsigned char>((packed >> 8) & 0xff),
                             static_cast<unsigned char>(packed & 0xff), 255);
            }
            if (hex.size() == 8) {
                return Color(static_cast<unsigned char>((packed >> 24) & 0xff),
                             static_cast<unsigned char>((packed >> 16) & 0xff),
                             static_cast<unsigned char>((packed >> 8) & 0xff),
                             static_cast<unsigned char>(packed & 0xff));
            }
            return defaultValue;
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
        _singlePassRenderingEnabled(false),
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
            removeExternalDataSource(name); // replace if it exists
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

        // A vector source is drawn as its own child VectorTileLayer over its own source, using the
        // master decoder and filtered to its own layer name. Kept separate (not merged) so it can
        // overzoom independently - e.g. a ContourTileDataSource renders z13+ from z12 DEM data via
        // the child layer's MaxOverzoomLevel, without needing the DEM at the target zoom.
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
        auto childLayer = std::dynamic_pointer_cast<TileLayer>(source.childLayer);
        if (!childLayer) {
            return;
        }
        childLayer->setPreloading(isPreloading());
        childLayer->setZoomLevelBias(source.zoomLevelBiasSet ? source.zoomLevelBias : getZoomLevelBias());
        if (source.maxOverzoomLevelSet) {
            childLayer->setMaxOverzoomLevel(source.maxOverzoomLevel);
        }
    }

    bool CompositeVectorTileLayer::isSinglePassRenderingEnabled() const {
        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        return _singlePassRenderingEnabled;
    }

    void CompositeVectorTileLayer::setSinglePassRenderingEnabled(bool enabled) {
        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        _singlePassRenderingEnabled = enabled;
    }

    std::shared_ptr<ElevationDecoder> CompositeVectorTileLayer::resolveElevationDecoder(const std::shared_ptr<TileDataSource>& dataSource) {
        if (!dataSource || !dataSource->containsMetaDataKey(ElevationDecoder::ENCODING_KEY)) {
            return std::shared_ptr<ElevationDecoder>(); // let HillshadeRasterTileLayer infer per tile
        }
        return ElevationDecoder::Resolve(std::shared_ptr<TileData>(), dataSource, std::shared_ptr<ElevationDecoder>());
    }

    std::string CompositeVectorTileLayer::buildFilterString(const std::vector<std::string>& group, bool includeBackground) {
        // The filter is tested with std::regex_match (full match) and the per-tile background layer
        // has an EMPTY name (TileReader). So "^$" matches ONLY the background, and a trailing empty
        // alternative "(...|)" additionally matches it. Non-bottom groups must NOT match "" or they
        // would paint the opaque background over earlier groups.
        if (group.empty()) {
            // Bottom group with no style layers still draws the background; other empty groups match
            // nothing ("[^\\s\\S]" requires one impossible char, so it matches no string, not even "").
            return includeBackground ? "^$" : "[^\\s\\S]";
        }
        // The filter is matched against the name of every rendered vt tile layer, and that name is
        // the STYLE name, not the map layer name: a layer with CartoCSS attachments produces one
        // style per attachment, named "layer::attachment" (and nested, "layer::a::b"). Matching the
        // bare layer name alone therefore keeps only the default attachment and silently drops the
        // rest - all of "transportation::casing*", "poi::icon", "landcover::wood", "place::label".
        // So accept the layer name followed by any attachment suffix.
        std::string pattern = "^((";
        for (std::size_t i = 0; i < group.size(); i++) {
            pattern += (i ? "|" : "") + group[i];
        }
        pattern += ")(::.*)?";
        if (includeBackground) {
            pattern += "|"; // empty alternative -> also matches the empty-named background layer
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
        // Internal group layer: same merged source + decoder as this layer, with a fixed
        // rendererLayerFilter. Shares the master data source so a source change reloads all groups.
        auto groupLayer = std::make_shared<VectorTileLayer>(getDataSource(), getTileDecoder());
        groupLayer->setRendererLayerFilter(filter);
        groupLayer->setLabelRenderOrder(getLabelRenderOrder());
        groupLayer->setBuildingRenderOrder(getBuildingRenderOrder());
        // The groups render the same source as this layer, so they must select the same tiles.
        groupLayer->setZoomLevelBias(getZoomLevelBias());
        groupLayer->setPreloading(isPreloading());
        std::shared_ptr<Layer> child = groupLayer;
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

    void CompositeVectorTileLayer::rebuildDrawItems() {
        // Caller holds _sourceMutex (or is the constructor).

        // Drop previous internal group layers.
        for (const DrawItem& item : _drawItems) {
            if (item.groupLayer && _componentsSet) {
                unwireChild(item.groupLayer);
            }
        }
        _drawItems.clear();

        auto decoder = std::dynamic_pointer_cast<MBVectorTileDecoder>(getTileDecoder());
        if (!decoder) {
            VectorTileLayer::setRendererLayerFilter("");
            return;
        }
        std::vector<std::string> order = decoder->getStyleLayerNames();

        // Constrain each external child's visible zoom range to the style's config-rule range.
        for (const ExternalSource& s : _externalSources) {
            if (s.childLayer) {
                applyExternalChildZoomRange(s);
            }
        }

        auto isChildSlot = [&](const std::string& layerName) {
            const ExternalSource* s = findExternalSource(layerName);
            return s && s->childLayer; // raster / hillshade / vector children all occupy a slot
        };

        std::vector<std::string> group;
        bool firstSlotSeen = false;
        for (const std::string& layerName : order) {
            if (isChildSlot(layerName)) {
                if (!firstSlotSeen) {
                    // Group 0 renders on this layer itself. It also draws the style background
                    // (includeBackground), so the Map background-color appears once at the bottom.
                    VectorTileLayer::setRendererLayerFilter(buildFilterString(group, /*includeBackground=*/true));
                    firstSlotSeen = true;
                } else if (!group.empty()) {
                    // A non-empty intermediate group gets its own stable-filtered layer. Empty
                    // intermediate groups are skipped entirely (no layer to fetch/decode/overpaint).
                    _drawItems.push_back({ DRAW_ITEM_VT_GROUP, std::string(), makeGroupLayer(buildFilterString(group)) });
                }
                _drawItems.push_back({ DRAW_ITEM_EXTERNAL, layerName, std::shared_ptr<Layer>() });
                group.clear();
            } else {
                group.push_back(layerName);
            }
        }
        if (!firstSlotSeen) {
            // No external child slots: render everything on this layer, no interleaving.
            VectorTileLayer::setRendererLayerFilter("");
        } else if (!group.empty()) {
            _drawItems.push_back({ DRAW_ITEM_VT_GROUP, std::string(), makeGroupLayer(buildFilterString(group)) });
        }

        // Warn about registered sources that have no slot in the style order.
        for (const ExternalSource& s : _externalSources) {
            if (s.childLayer && std::find(order.begin(), order.end(), s.name) == order.end()) {
                Log::Warnf("CompositeVectorTileLayer: external source '%s' is not listed in the style 'layers' - it will not be drawn", s.name.c_str());
            }
        }
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
        }
    }

    void CompositeVectorTileLayer::loadData(const std::shared_ptr<CullState>& cullState) {
        VectorTileLayer::loadData(cullState);

        // Re-apply merged-vector (contour) generation parameters in case the style / parameter
        // parameters changed (a change triggers a decoder update -> tile reload -> loadData).
        applyVectorSourceConfigs();

        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        for (const ExternalSource& s : _externalSources) {
            // Only sources the style actually gives a slot to. A registered source the style's
            // 'layers' never mentions has no draw item (rebuildDrawItems warns about it), so
            // nothing would ever draw it - but it was still fetching and decoding its tiles, and
            // for a hillshade that means downloading DEM tiles and building normal maps for a
            // layer the map does not show. rebuildDrawItems runs on every style change, so a
            // source picked up by a later style starts loading then.
            if (s.childLayer && isDrawnSlot(s.name)) {
                s.childLayer->loadData(cullState);
            }
        }
        for (const DrawItem& item : _drawItems) {
            if (item.groupLayer) {
                item.groupLayer->loadData(cullState);
            }
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
            if (item.groupLayer && item.groupLayer->isUpdateInProgress()) {
                return true;
            }
        }
        return false;
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

    void CompositeVectorTileLayer::applyConfig(const ExternalSource& source, const mvt::ResolvedLayerConfig& config, const ViewState& viewState) {
        auto getValue = [&](const std::string& key) -> const mvt::Value* {
            auto it = config.values.find(key);
            return it != config.values.end() ? &it->second : nullptr;
        };
        // Some hillshade setters bake into the normal map and re-decode the tile
        // (setHeightScale/setContrast/setContourEnabled call updateTiles). Applying those every frame
        // with a zoom-interpolated value would re-decode continuously and the tiles would never
        // settle, so the value appears static. Instead they are applied only when the INTEGER zoom
        // changes - which is exactly when the hillshade tiles reload for the new zoom level anyway, so
        // the re-decode is aligned and free. Result: per-zoom-level animation of exaggeration/contrast.
        // Cheap redraw-only setters (opacity, colors, method, illumination, contour width/color, filter
        // mode) are applied every frame so they stay smooth.
        std::map<std::string, double>& applied = _lastChildConfig[source.name];
        int intZoom = static_cast<int>(std::floor(viewState.getZoom()));
        bool decodeZoomChanged = (applied.find("__izoom") == applied.end()) || (static_cast<int>(applied["__izoom"]) != intZoom);
        applied["__izoom"] = static_cast<double>(intZoom);

        // EVERY setter below ends in Layer::redraw(), i.e. a request for another frame - so
        // applying them unconditionally every frame means the map asks for a new frame for ever
        // and never goes idle, whatever the value. Measured: 4 requests per frame, the only
        // source in a standing-still 3D view, CPU pinned. Apply only what actually changed; a
        // zoom-interpolated value still changes (and still animates) whenever the zoom moves.
        auto changed = [&applied](const std::string& key, double value) {
            auto it = applied.find(key);
            if (it != applied.end() && it->second == value) {
                return false;
            }
            applied[key] = value;
            return true;
        };

        // Tile selection, common to every source type. A style value takes precedence over
        // setExternalDataSourceZoomLevelBias / setExternalDataSourceMaxOverzoomLevel while it is
        // present, the same way the other per-source config values win over programmatic setters.
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
            // exaggeration is a per-frame shader uniform (no re-decode) -> animates smoothly with zoom.
            if (const mvt::Value* v = getValue("exaggeration")) {
                float exaggeration = valueToFloat(*v, 1.0f);
                if (changed("exaggeration", exaggeration)) { hillshade->setExaggeration(exaggeration); }
            }

            if (decodeZoomChanged) { // decode-bound: only at integer zoom crossings (aligned with tile reload)
                // height-scale is the raw normal-map height scale (baked at decode; steps with zoom).
                if (const mvt::Value* v = getValue("height-scale")) { hillshade->setHeightScale(valueToFloat(*v, 1.0f)); }
                if (const mvt::Value* v = getValue("contrast")) { hillshade->setContrast(valueToFloat(*v, 0.5f)); }
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
                // Azimuth in degrees (0 = north, clockwise) at a fixed 45 deg altitude, matching the
                // HillshadeRasterTileLayer default direction convention (sin az, cos az, -sin alt).
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
            // Contour generation parameters are not per-frame (changing them regenerates
            // tiles), so evaluate at a neutral zoom; style parameters still apply.
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
            // Label stubs: the geometry becomes a few short polylines to carry the elevation text,
            // for a stack that draws the contour LINES in the terrain shader
            // (hillshade-contour-interval) instead of from this source.
            if (changed("label-stubs", value))        { contour->setLabelStubsEnabled(value != 0.0f); }
            if (changed("label-interval", value))     { contour->setLabelInterval(value); }
        }
    }

    void CompositeVectorTileLayer::collectDrapeLayers(std::vector<std::shared_ptr<TileLayer> >& drapeLayers, const ViewState& viewState) {
        // Same order AND the same gating as renderComposite: group 0 is this layer itself, then
        // every draw item. Without the children the cross-layer drape sees a single layer holding
        // only group 0: the hillshade, the raster slots and every later style-layer group are then
        // neither baked into the drape texture nor suppressed from the 3D pass, so they keep their
        // own terrain pre-pass and depth domain - exactly the split the shared drape exists to
        // remove.
        TileLayer::collectDrapeLayers(drapeLayers, viewState);
        if (!isVisible()) {
            return;
        }

        auto decoder = std::dynamic_pointer_cast<MBVectorTileDecoder>(getTileDecoder());

        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);
        for (const DrawItem& item : _drawItems) {
            std::shared_ptr<Layer> childLayer;
            if (item.kind == DRAW_ITEM_VT_GROUP) {
                childLayer = item.groupLayer;
            } else if (const ExternalSource* source = findExternalSource(item.slot)) {
                // Raster/hillshade children are gated by their config symbolizer exactly as in
                // renderComposite. Collecting one the style hides at this zoom bakes it into the
                // terrain texture, which is how a hillshade the style never draws still ended up
                // on the map. The config is applied here as well, because the bake runs BEFORE
                // renderComposite in the frame and a texture baked with last frame's settings is
                // not re-baked afterwards - the fingerprint does not see style parameters.
                if (source->type != CompositeSourceType::COMPOSITE_SOURCE_TYPE_VECTOR && decoder) {
                    mvt::ResolvedLayerConfig config = decoder->resolveLayerConfig(item.slot, viewState.getZoom());
                    applyConfig(*source, config, viewState);
                    if (!config.visible) {
                        continue;
                    }
                }
                childLayer = source->childLayer;
            }
            if (childLayer) {
                childLayer->collectDrapeLayers(drapeLayers, viewState);
            }
        }
    }

    void CompositeVectorTileLayer::collectLabelLayers(std::vector<std::shared_ptr<VectorTileLayer> >& labelLayers) {
        // Same order as renderComposite: group 0 is this layer itself, then every draw item. The
        // label culler grid accumulates across the layers it is given, so the order decides which
        // labels win a slot. Without the children, every style layer after the first external slot
        // (contours and every other vector slot included) is never culled - and a label that is
        // never culled is never placed, so it never becomes visible.
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
                childLayer = source->childLayer;
            }
            if (childLayer) {
                childLayer->collectLabelLayers(labelLayers);
            }
        }
    }

    bool CompositeVectorTileLayer::renderComposite(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState, bool terrain) {
        auto decoder = std::dynamic_pointer_cast<MBVectorTileDecoder>(getTileDecoder());

        std::lock_guard<std::recursive_mutex> lock(_sourceMutex);

        // Group 0 renders on this layer itself (with the group-0 rendererLayerFilter set in
        // rebuildDrawItems). When there are no external child slots, this draws everything.
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
            bool visible = true;
            // Raster/hillshade children are gated by their config symbolizer's zoom/param:: visibility.
            // Vector children have no config symbolizer (they are styled by normal line/text rules,
            // which the child's own decode already zoom-filters), so they always draw.
            if (source->type != CompositeSourceType::COMPOSITE_SOURCE_TYPE_VECTOR && decoder) {
                mvt::ResolvedLayerConfig config = decoder->resolveLayerConfig(item.slot, viewState.getZoom());
                applyConfig(*source, config, viewState);
                visible = config.visible;
            }
            if (visible) {
                refresh = (terrain ? source->childLayer->onDrawFrame3D(deltaSeconds, billboardSorter, viewState)
                                   : source->childLayer->onDrawFrame(deltaSeconds, billboardSorter, viewState)) || refresh;
            }
        }
        return refresh;
    }

}
