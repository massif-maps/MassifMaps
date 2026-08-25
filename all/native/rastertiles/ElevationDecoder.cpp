#include "ElevationDecoder.h"
#include "core/Variant.h"
#include "datasources/TileDataSource.h"
#include "datasources/components/TileData.h"
#include "rastertiles/MapBoxElevationDataDecoder.h"
#include "rastertiles/TerrariumElevationDataDecoder.h"
#include "utils/Log.h"
#include "utils/TileUtils.h"

#include <algorithm>

namespace massif
{
    ElevationDecoder::~ElevationDecoder()
    {
    }

    ElevationDecoder::ElevationDecoder()
    {
    }

    std::shared_ptr<ElevationDecoder> ElevationDecoder::Resolve(const std::shared_ptr<TileData>& tileData,
                                                                const std::shared_ptr<TileDataSource>& dataSource,
                                                                const std::shared_ptr<ElevationDecoder>& preferred)
    {
        static const std::shared_ptr<ElevationDecoder> mapBoxDecoder = std::make_shared<MapBoxElevationDataDecoder>();
        static const std::shared_ptr<ElevationDecoder> terrariumDecoder = std::make_shared<TerrariumElevationDataDecoder>();

        Variant encoding;
        if (tileData) {
            encoding = tileData->getMetaDataElement(ENCODING_KEY);
        }
        if (encoding.getType() != VariantType::VARIANT_TYPE_STRING && dataSource) {
            encoding = dataSource->getMetaDataElement(ENCODING_KEY);
        }
        if (encoding.getType() == VariantType::VARIANT_TYPE_STRING) {
            std::string value = encoding.getString();
            if (value == "terrarium") {
                return terrariumDecoder;
            }
            if (value == "mapbox") {
                return mapBoxDecoder;
            }
            Log::Errorf("ElevationDecoder::Resolve: Unknown %s '%s'", ENCODING_KEY.c_str(), value.c_str());
        }
        return preferred ? preferred : mapBoxDecoder;
    }

    const std::string ElevationDecoder::ENCODING_KEY = "dem_encoding";
} // namespace massif
