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
        return NULL;
    }


    std::shared_ptr<TileData> MergedMBVTTileDataSource::loadTile(const MapTile& mapTile) {
        int zoom = mapTile.getZoom();
        std::shared_ptr<TileData> result1;
        std::shared_ptr<TileData> result2;
        if (zoom <= _dataSource1->getMaxZoom() && zoom >= _dataSource1->getMinZoom()) {
            result1 = _dataSource1->loadTile(mapTile);
        }
        if (zoom <= _dataSource2->getMaxZoom() && zoom >= _dataSource2->getMinZoom()) {
            result2 = _dataSource2->loadTile(mapTile);
        }

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
            std::shared_ptr<std::vector<unsigned char>> data1 = result1->getData()->getDataPtr();
            std::shared_ptr<std::vector<unsigned char>> data2 = result2->getData()->getDataPtr();

            std::vector<unsigned char> mergedData;
            mergedData.reserve(data1->size() + data2->size());

            // Try to decompress data1 with various compression formats
            std::vector<unsigned char> uncompressedData1;
            // Use fast header-based detection where possible to avoid expensive trial decompression.
            const unsigned char* bytes1 = data1->empty() ? nullptr : data1->data();
            std::size_t size1 = data1->size();
            if (bytes1 && massif::mvt::compression::is_gzip(bytes1, size1)) {
                zlib::inflate_gzip(bytes1, size1, uncompressedData1);
                mergedData.insert(mergedData.end(), uncompressedData1.begin(), uncompressedData1.end());
#ifdef HAVE_ZSTD
            } else if (bytes1 && massif::mvt::compression::is_zstd(bytes1, size1)) {
                massif::mvt::compression::inflate_zstd(bytes1, size1, uncompressedData1);
                mergedData.insert(mergedData.end(), uncompressedData1.begin(), uncompressedData1.end());
#endif
#ifdef HAVE_BROTLI
            } else if (massif::mvt::compression::is_brotli(bytes1, size1)) {
                massif::mvt::compression::inflate_brotli(bytes1, size1, uncompressedData1);
                mergedData.insert(mergedData.end(), uncompressedData1.begin(), uncompressedData1.end());
#endif
            } else {
                mergedData.insert(mergedData.end(), data1->begin(), data1->end());
            }
            
            // Try to decompress data2 with various compression formats
            std::vector<unsigned char> uncompressedData2;
            // Use fast header-based detection where possible to avoid expensive trial decompression.
            const unsigned char* bytes2 = data2->empty() ? nullptr : data2->data();
            std::size_t size2 = data2->size();
            if (bytes2 && massif::mvt::compression::is_gzip(bytes2, size2)) {
                zlib::inflate_gzip(bytes2, size2, uncompressedData2);
                mergedData.insert(mergedData.end(), uncompressedData2.begin(), uncompressedData2.end());
#ifdef HAVE_ZSTD
            } else if (bytes2 && massif::mvt::compression::is_zstd(bytes2, size2)) {
                massif::mvt::compression::inflate_zstd(bytes2, size2, uncompressedData2);
                mergedData.insert(mergedData.end(), uncompressedData2.begin(), uncompressedData2.end());
#endif
#ifdef HAVE_BROTLI
            } else if (massif::mvt::compression::is_brotli(bytes2, size2)) {
                massif::mvt::compression::inflate_brotli(bytes2, size2, uncompressedData1);
                mergedData.insert(mergedData.end(), uncompressedData2.begin(), uncompressedData2.end());
#endif
            } else {
                mergedData.insert(mergedData.end(), data2->begin(), data2->end());
            }

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
