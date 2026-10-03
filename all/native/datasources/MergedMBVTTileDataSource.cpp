#include "MergedMBVTTileDataSource.h"
#include "core/BinaryData.h"
#include "core/MapTile.h"
#include "core/Variant.h"
#include "components/Exceptions.h"
#include "utils/Log.h"

#ifdef _MASSIF_OFFLINE_SUPPORT
#include "datasources/MBTilesTileDataSource.h"
#endif

#include <algorithm>

#include <stdext/zlib.h>

#include <mapnikvt/CompressionUtils.h>
#include <mapnikvt/MBVTSubtile.h>

namespace massif {
    
    MergedMBVTTileDataSource::MergedMBVTTileDataSource(const std::shared_ptr<TileDataSource>& dataSource1, const std::shared_ptr<TileDataSource>& dataSource2) :
        TileDataSource(),
        _dataSource1(dataSource1),
        _dataSource2(dataSource2)
    {
        if (!dataSource1) {
            throw NullArgumentException("Null dataSource1");
        }
        if (!dataSource2) {
            throw NullArgumentException("Null dataSource2");
        }

        _dataSourceListener = std::make_shared<DataSourceListener>(*this);
        _dataSource1->registerOnChangeListener(_dataSourceListener);
        _dataSource2->registerOnChangeListener(_dataSourceListener);
    }
    
    MergedMBVTTileDataSource::~MergedMBVTTileDataSource() {
        _dataSource2->unregisterOnChangeListener(_dataSourceListener);
        _dataSource1->unregisterOnChangeListener(_dataSourceListener);
        _dataSourceListener.reset();
    }

   int MergedMBVTTileDataSource::getMinZoom() const {
        return std::min(_dataSource1->getMinZoom(), _dataSource2->getMinZoom());
    }

    int MergedMBVTTileDataSource::getMaxZoom() const {
        return std::max(_dataSource1->getMaxZoom(), _dataSource2->getMaxZoom());
    }

    MapBounds MergedMBVTTileDataSource::getDataExtent() const {
        MapBounds bounds = _dataSource1->getDataExtent();
        bounds.expandToContain(_dataSource2->getDataExtent());
        return bounds;
    }


    std::string MergedMBVTTileDataSource::getTileMask() const {
#ifdef _MASSIF_OFFLINE_SUPPORT
        if (auto mbtilesDatasource = std::dynamic_pointer_cast<MBTilesTileDataSource>(_dataSource1.get())) {
            return mbtilesDatasource->getTileMask();
        }
        if (auto mbtilesDatasource = std::dynamic_pointer_cast<MBTilesTileDataSource>(_dataSource2.get())) {
            return mbtilesDatasource->getTileMask();
        }
#endif
        // not NULL: a std::string built from it reads a null pointer
        return std::string();
    }


    namespace {
        std::vector<unsigned char> inflate(const std::vector<unsigned char>& data) {
            // header detection, not trial decompression
            const unsigned char* bytes = data.empty() ? nullptr : data.data();
            std::vector<unsigned char> uncompressed;
            if (bytes && massif::mvt::compression::is_gzip(bytes, data.size())) {
                zlib::inflate_gzip(bytes, data.size(), uncompressed);
                return uncompressed;
            }
#ifdef HAVE_ZSTD
            if (bytes && massif::mvt::compression::is_zstd(bytes, data.size())) {
                massif::mvt::compression::inflate_zstd(bytes, data.size(), uncompressed);
                return uncompressed;
            }
#endif
#ifdef HAVE_BROTLI
            if (massif::mvt::compression::is_brotli(bytes, data.size())) {
                massif::mvt::compression::inflate_brotli(bytes, data.size(), uncompressed);
                return uncompressed;
            }
#endif
            return data;
        }

        // past its max zoom a source is cut out of its last tile, as the layer does for the whole
        // tile: merged tiles hold one zoom, so the layer cannot do it for one source
        std::shared_ptr<TileData> loadSourceTile(TileDataSource& dataSource, const MapTile& mapTile) {
            int zoom = mapTile.getZoom();
            if (zoom < dataSource.getMinZoom()) {
                return std::shared_ptr<TileData>();
            }
            int dz = zoom - dataSource.getMaxZoom();
            if (dz <= 0) {
                return dataSource.loadTile(mapTile);
            }
            if (dataSource.isMaxOverzoomLevelSet() && zoom > dataSource.getMaxZoomWithOverzoom()) {
                return std::shared_ptr<TileData>();
            }
            MapTile parentTile(mapTile.getX() >> dz, mapTile.getY() >> dz, dataSource.getMaxZoom(), mapTile.getFrameNr());
            std::shared_ptr<TileData> parent = dataSource.loadTile(parentTile);
            if (!parent || !parent->getData() || parent->isReplaceWithParent()) {
                return std::shared_ptr<TileData>();
            }
            std::vector<unsigned char> child = mvt::subtileMBVT(inflate(*parent->getData()->getDataPtr()), dz, mapTile.getX(), mapTile.getY());
            if (child.empty()) {
                return std::shared_ptr<TileData>();
            }
            return std::make_shared<TileData>(std::make_shared<BinaryData>(std::move(child)));
        }
    }

    std::shared_ptr<TileData> MergedMBVTTileDataSource::loadTile(const MapTile& mapTile) {
        std::shared_ptr<TileData> result1 = loadSourceTile(*_dataSource1, mapTile);
        std::shared_ptr<TileData> result2 = loadSourceTile(*_dataSource2, mapTile);

        if (result1 && result2) {
            // If either result contains 'replace with parent' then the only option is to pass this result on.
            // Otherwise we would need to do request the parent ourselves, do unpacking, scaling, clipping and packing.
            if (result1->isReplaceWithParent()) {
                return result2;
            }
            if (result2->isReplaceWithParent()) {
                return result1;
            }
            
            // We have data for both sources, we can merge them. Note that we may need to decompress the data first.
            std::vector<unsigned char> mergedData = inflate(*result1->getData()->getDataPtr());
            std::vector<unsigned char> uncompressedData2 = inflate(*result2->getData()->getDataPtr());
            mergedData.insert(mergedData.end(), uncompressedData2.begin(), uncompressedData2.end());

            auto mergedBinaryData = std::make_shared<BinaryData>(std::move(mergedData));
            auto mergedTileData = std::make_shared<TileData>(mergedBinaryData);
            
            // Merge the meta data of both sources. When keys conflict, source1 wins (applied last).
            mergedTileData->setMetaData(_dataSource2->getMetaDataPtr());
            if (std::shared_ptr<const std::map<std::string, Variant> > metaData1 = _dataSource1->getMetaDataPtr()) {
                for (const auto& entry : *metaData1) {
                    mergedTileData->setMetaDataElement(entry.first, entry.second);
                }
            }


            return mergedTileData;
        }

        // Return either result that is not null.
        return result1 ? result1 : result2;
    }

    MergedMBVTTileDataSource::DataSourceListener::DataSourceListener(MergedMBVTTileDataSource& combinedDataSource) :
        _combinedDataSource(combinedDataSource)
    {
    }
    
    void MergedMBVTTileDataSource::DataSourceListener::onTilesChanged(bool removeTiles) {
        _combinedDataSource.notifyTilesChanged(removeTiles);
    }
    
}
