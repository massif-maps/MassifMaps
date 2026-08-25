#include "api/CallbackTileDataSource.h"
#include "core/BinaryData.h"
#include "core/MapTile.h"
#include "datasources/components/TileData.h"
#include "utils/Log.h"

#include <string>

namespace massif { namespace api {

    CallbackTileDataSource::CallbackTileDataSource(int minZoom, int maxZoom,
                                                   const mm_tile_source& source) :
        TileDataSource(minZoom, maxZoom),
        _loadTile(source.load_tile),
        _destroy(source.destroy),
        _userData(source.user_data)
    {
    }

    CallbackTileDataSource::~CallbackTileDataSource() {
        if (_destroy) {
            _destroy(_userData);
        }
    }

    namespace {
        /** What the sink writes into, for the duration of one loadTile call. */
        struct Sink {
            std::shared_ptr<TileData> tileData;
            std::string tile;
        };

        void receive(void* sinkData, const void* data, std::size_t size,
                     int format, int width, int height) {
            Sink* sink = static_cast<Sink*>(sinkData);
            if (!data || size == 0) {
                return;
            }
            // Copied HERE, inside the loader's own call, which is what makes the caller's stack
            // buffer legal and leaves the ABI with no ownership rule at all.
            auto bytes = std::make_shared<BinaryData>(static_cast<const unsigned char*>(data), size);

            if (format == MM_TILE_RGBA8) {
                if (width <= 0 || height <= 0) {
                    Log::Errorf("CallbackTileDataSource: %s is RGBA8 with no dimensions",
                                sink->tile.c_str());
                    return;
                }
                sink->tileData = std::make_shared<TileData>(bytes, width, height);
                return;
            }
            sink->tileData = std::make_shared<TileData>(bytes);
        }
    }

    std::shared_ptr<TileData> CallbackTileDataSource::loadTile(const MapTile& tile) {
        Sink sink;
        sink.tile = tile.toString();

        int status = _loadTile(_userData, tile.getZoom(), tile.getX(), tile.getY(), &receive,
                               &sink);
        if (status != MM_OK) {
            Log::Errorf("CallbackTileDataSource::loadTile: %s failed with %d",
                        sink.tile.c_str(), status);
            return std::shared_ptr<TileData>();
        }
        // A loader that never called the sink is saying the tile does not exist, which is a hole
        // in the coverage rather than an error.
        return sink.tileData;
    }

    void CallbackTileDataSource::disown() {
        _destroy = nullptr;
    }

} }
