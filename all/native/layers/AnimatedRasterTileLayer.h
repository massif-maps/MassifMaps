#ifndef _MASSIF_ANIMATEDRASTERTILELAYER_H_
#define _MASSIF_ANIMATEDRASTERTILELAYER_H_

#include "layers/CustomRasterTileLayer.h"

#include <future>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace massif {

    /**
     * A loop of time steps, e.g. a weather radar, each a tile of its own ({time} in an HTTPTileDataSource URL). A step and
     * the next are drawn in one pass and mixed in the shader; see docs/features/animated-raster.md. Experimental.
     */
    class AnimatedRasterTileLayer : public CustomRasterTileLayer {
    public:
        /**
         * Constructs an AnimatedRasterTileLayer object from a raster data source.
         * @param dataSource The raster data source, e.g. an HTTPTileDataSource with {time} in its URL,
         * optionally behind a cache. The layer gives it the timestamps.
         */
        explicit AnimatedRasterTileLayer(const std::shared_ptr<TileDataSource>& dataSource);
        virtual ~AnimatedRasterTileLayer();

        /**
         * Returns the timestamps of the time steps.
         * @return The timestamps, oldest first.
         */
        std::vector<std::string> getFrameTimestamps() const;
        /**
         * Sets the time steps, oldest first: the strings {time} takes in the source's URL, epoch seconds ideally.
         * @param timestamps The timestamps, at most 255.
         */
        void setFrameTimestamps(const std::vector<std::string>& timestamps);

        /**
         * Returns the animation position.
         * @return The position, in steps.
         */
        float getAnimationPosition() const;
        /**
         * Sets the animation position: the whole part picks the step, the fraction is the mix towards the next.
         * @param position The position, in steps; clamped to the first and last step.
         */
        void setAnimationPosition(float position);

        /**
         * Returns how many steps ahead of the shown one are fetched, for every visible tile.
         * @return The number of steps.
         */
        int getPrefetchFrames() const;
        /**
         * Sets how many steps ahead of the shown one are fetched before they are needed. The default is 2; a loop
         * that must not stall on its first pass wants all of them, at the cost of memory and traffic.
         * @param frames The number of steps, 0 to 255.
         */
        void setPrefetchFrames(int frames);

        /**
         * Returns the memory the prefetched steps may take.
         * @return The budget in megabytes.
         */
        int getPrefetchBudget() const;
        /**
         * Sets the memory the prefetched steps of the visible tiles may take: a view that needs more prefetches fewer
         * steps, and the rest load from the data source when they come up. Lower it on a device short of memory.
         * The default is 192.
         * @param megabytes The budget in megabytes, 16 to 1024.
         */
        void setPrefetchBudget(int megabytes);

    protected:
        virtual std::string getEffectiveShaderSource() const;
        virtual std::vector<Input> getExtraInputs(const MapTile& dataSourceTile) const;
        virtual bool loadExtraBitmaps(const MapTile& dataSourceTile, const CancelableTask& task, std::vector<std::shared_ptr<vt::TileBitmap> >& extraBitmaps) const;
        virtual std::shared_ptr<TileData> loadTileData(const std::shared_ptr<TileDataSource>& dataSource, const MapTile& tile) const;
        virtual std::shared_ptr<Bitmap> decodeTileData(const MapTile& tile, const std::shared_ptr<TileData>& tileData) const;
        virtual std::vector<MapTile> collectFramePrefetchTiles(const std::vector<MapTile>& visibleTiles);

        static const std::string SHADER_HELPERS;
        static const std::string MIX_SHADER;
        static const std::size_t MAX_FRAME_COUNT = 255;
        // What a tile of two steps holds, as the tile cache counts it, and the most it may spend on steps nobody sees yet.
        static const std::size_t TILE_BYTES = 540672;
        static const int DEFAULT_PREFETCH_BUDGET_MB = 192;
        static const std::size_t MIN_CACHE_BYTES = 10 * 1024 * 1024;

    private:
        // The step and the mix towards the next one, and the frame number of the step; false without time steps.
        bool positionState(int& step, float& fraction, int& frameNr) const;
        std::shared_ptr<vt::TileBitmap> stepBitmap(int step) const;
        bool hasStep(const MapTile& tile) const;

        std::vector<std::string> _timestamps;
        std::vector<int> _frameNrs;
        float _position;
        int _prefetchFrames;
        int _prefetchBudget;
        mutable WeakSharedMap<int, vt::TileBitmap> _stepBitmaps;
        // A step is wanted by its own tile and by the previous step's: one request, not two at once.
        mutable std::map<std::pair<const TileDataSource*, long long>, std::shared_future<std::shared_ptr<TileData> > > _inFlight;
        mutable std::mutex _inFlightMutex;
        mutable std::size_t _prefetchCacheBytes;
        mutable std::shared_ptr<Bitmap> _placeholder;
        mutable std::mutex _animationMutex;
    };

}

#endif
