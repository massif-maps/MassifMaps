#include "api/DownloadMethods.h"

#ifdef _MASSIF_OFFLINE_SUPPORT

#include "api/MapEventBridge.h"
#include "api/Methods.h"
#include "api/StructCodec.h"
#include "core/MapBounds.h"
#include "datasources/PersistentCacheTileDataSource.h"
#include "datasources/TileDownloadInfo.h"
#include "datasources/TileDownloadListener.h"
#include "utils/Log.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace massif { namespace api {

    namespace {

        /**
         * Turns the download listener's four callbacks into `download.*` events on the source.
         *
         * The listener is passed to the CALL rather than installed on a slot, so nothing here has to
         * chain: each download owns its bridge, and the bridge lives as long as the SDK holds it.
         */
        class DownloadBridge : public TileDownloadListener {
        public:
            DownloadBridge(const std::shared_ptr<Context>& context, Handle target) :
                _emitter(context, target) {}

            virtual void onDownloadStarting(int tileCount) {
                _emitter.emit("download.started",
                              std::make_shared<TileDownloadInfo>(tileCount, 0, MapTile(0, 0, 0, 0)),
                              "massif::TileDownloadInfo");
            }

            virtual void onDownloadProgress(float progress) {
                _emitter.emit("download.progress",
                              std::make_shared<TileDownloadInfo>(-1, progress, MapTile(0, 0, 0, 0)),
                              "massif::TileDownloadInfo");
            }

            virtual void onDownloadCompleted() {
                _emitter.emit("download.completed", nullptr, nullptr);
            }

            virtual void onDownloadFailed(const MapTile& tile) {
                _emitter.emit("download.failed",
                              std::make_shared<TileDownloadInfo>(-1, -1, tile),
                              "massif::TileDownloadInfo");
            }

        private:
            PayloadEmitter _emitter;
        };

        /**
         * The bridges, one per source that has been asked to download.
         *
         * Held here rather than by the caller: the SDK keeps a weak reference to the listener, so a
         * bridge that only the call site owned would be collected as soon as the call returned and
         * the download would report nothing.
         */
        std::mutex& bridgeMutex() {
            static std::mutex instance;
            return instance;
        }

        std::unordered_map<void*, std::shared_ptr<DownloadBridge> >& bridges() {
            static std::unordered_map<void*, std::shared_ptr<DownloadBridge> > instance;
            return instance;
        }

        /**
         * startDownloadArea([[minLng, minLat], [maxLng, maxLat]], minZoom, maxZoom, fetchDelay).
         *
         * The bounds are the facade's own `[min, max]` pair, in the SOURCE's projection - the same
         * rule every other position on this API follows.
         */
        Result startDownloadArea(Context& context, void* obj, const CallArgs& args, PropertyValue&) {
            MapBounds bounds;
            long long minZoom = 0;
            long long maxZoom = 0;
            long long fetchDelay = 0;
            if (!StructCodec::decode(args.get(0).toString(), bounds) ||
                !args.getLong(1, minZoom) || !args.getLong(2, maxZoom)) {
                return RESULT_BAD_SPEC;
            }
            args.getLong(3, fetchDelay);
            auto source = static_cast<PersistentCacheTileDataSource*>(obj);
            std::shared_ptr<DownloadBridge> bridge;
            {
                std::lock_guard<std::mutex> lock(bridgeMutex());
                // The default context: a method thunk is handed the context by reference, and the
                // emitter needs to hold one - which is what the static bindings all address anyway.
                bridge = std::make_shared<DownloadBridge>(Context::GetDefault(),
                                                          context.handleOf(obj));
                bridges()[obj] = bridge;
            }
            try {
                source->startDownloadArea(bounds, static_cast<int>(minZoom),
                                          static_cast<int>(maxZoom),
                                          static_cast<int>(fetchDelay), bridge);
            } catch (const std::exception& ex) {
                Log::Errorf("api startDownloadArea: %s", ex.what());
                return RESULT_FAILED;
            }
            return RESULT_OK;
        }

        Result stopAllDownloads(Context&, void* obj, const CallArgs&, PropertyValue&) {
            static_cast<PersistentCacheTileDataSource*>(obj)->stopAllDownloads();
            std::lock_guard<std::mutex> lock(bridgeMutex());
            bridges().erase(obj);
            return RESULT_OK;
        }

        Result clearCache(Context&, void* obj, const CallArgs&, PropertyValue&) {
            static_cast<PersistentCacheTileDataSource*>(obj)->clear();
            return RESULT_OK;
        }

    }

    void registerDownloadMethods() {
        Methods::registerMethod("massif::PersistentCacheTileDataSource", "startDownloadArea",
                                &startDownloadArea);
        Methods::registerMethod("massif::PersistentCacheTileDataSource", "stopAllDownloads",
                                &stopAllDownloads);
        Methods::registerMethod("massif::PersistentCacheTileDataSource", "clear", &clearCache);
    }

} }

#endif
