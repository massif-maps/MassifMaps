/*
 * Tests for mm_source_create_custom - a tile source the CALLER implements, reached over the C ABI.
 *
 * This is the extension seam for native code: another shared library cannot subclass
 * massif::TileDataSource, because the SDK exports no C++ symbols, so a function pointer is the
 * only way in. What matters and is checked here: the callback is really invoked with the tile the
 * SDK asked for, both tile formats survive the crossing, the bytes are COPIED (so a caller's
 * scratch buffer is legal), and the destroy callback runs exactly once when the id is dropped.
 *
 * The layer that would draw the tile is out of scope - it needs the renderer. See tests/README.md.
 */

#include "api/Context.h"
#include "api/MassifApiC.h"
#include "core/BinaryData.h"
#include "core/MapTile.h"
#include "datasources/TileDataSource.h"
#include "datasources/components/TileData.h"

#include <cstring>
#include <memory>
#include <string>

using namespace massif;
using namespace massif::api;

#include "TestCheck.h"

namespace {

    struct Recorder {
        int calls = 0;
        int zoom = -1, x = -1, y = -1;
        int destroyed = 0;
        int format = MM_TILE_ENCODED;
        bool answer = true;
    };

    int loadTile(void* userData, int zoom, int x, int y, mm_tile_sink sink, void* sinkData) {
        Recorder* recorder = static_cast<Recorder*>(userData);
        recorder->calls++;
        recorder->zoom = zoom;
        recorder->x = x;
        recorder->y = y;
        if (!recorder->answer) {
            return MM_OK;  // never calling the sink means "no such tile"
        }

        // Deliberately a LOCAL buffer, which is the case the sink shape exists for: the SDK copies
        // inside the sink call, so this is legal and needs no free. Filling a struct and returning
        // would hand the SDK a dangling pointer.
        unsigned char pixels[2 * 2 * 4];
        std::memset(pixels, 0xAB, sizeof(pixels));
        sink(sinkData, pixels, sizeof(pixels), recorder->format, 2, 2);
        return MM_OK;
    }

    int failTile(void*, int, int, int, mm_tile_sink, void*) {
        return MM_FAILED;
    }

    void destroy(void* userData) {
        static_cast<Recorder*>(userData)->destroyed++;
    }

    std::shared_ptr<TileDataSource> sourceOf(Context& context, mm_handle handle) {
        return std::static_pointer_cast<TileDataSource>(
            context.getObject(static_cast<Handle>(handle), "massif::TileDataSource"));
    }

}

void testCustomSource() {
    Context context;
    Recorder recorder;

    mm_tile_source spec = { 3, 14, &loadTile, &destroy, &recorder };
    mm_handle handle = MM_NULL_HANDLE;
    TEST_CHECK(mm_source_create_custom(&context, "dem", &spec, &handle) == MM_OK,
               "a custom source registers under an id");
    TEST_CHECK(handle != MM_NULL_HANDLE, "and hands back a handle");

    // Registered as the base class, or a layer spec's `source` argument would refuse it - that
    // lookup is the whole reason the source needs an id.
    std::shared_ptr<TileDataSource> source = sourceOf(context, handle);
    TEST_CHECK(source != nullptr, "and resolves as a TileDataSource, which is what a spec wants");
    if (!source) {
        return;  // everything below dereferences it; a null here is the whole story
    }
    TEST_CHECK(source->getMinZoom() == 3 && source->getMaxZoom() == 14,
               "carrying the zoom range the caller declared");

    std::shared_ptr<TileData> tile = source->loadTile(MapTile(5, 6, 7, 0));
    TEST_CHECK(recorder.calls == 1, "loading a tile calls the caller's function");
    TEST_CHECK(recorder.zoom == 7 && recorder.x == 5 && recorder.y == 6,
               "with the tile coordinates the SDK asked for");
    TEST_CHECK(tile != nullptr && tile->getData() != nullptr, "and produces tile data");
    TEST_CHECK(tile->isRawPixels() == false,
               "an MM_TILE_ENCODED tile is not marked as raw pixels");

    // The copy: the callback's buffer was on its stack and is long gone.
    TEST_CHECK(tile->getData()->size() == 16, "the bytes are copied, not referenced");
    TEST_CHECK(tile->getData()->data()[0] == 0xAB, "and copied intact");

    recorder.format = MM_TILE_RGBA8;
    std::shared_ptr<TileData> raw = source->loadTile(MapTile(0, 0, 3, 0));
    TEST_CHECK(raw != nullptr && raw->isRawPixels(), "an MM_TILE_RGBA8 tile is marked raw");
    TEST_CHECK(raw->getWidth() == 2 && raw->getHeight() == 2,
               "and carries the dimensions, which is what tells a consumer not to decode it");

    recorder.answer = false;
    TEST_CHECK(source->loadTile(MapTile(1, 1, 3, 0)) == nullptr,
               "a tile the caller does not have is null, not an error");
    recorder.answer = true;

    // A source that fails is not a source that lies: no TileData rather than an empty one, or the
    // layer would cache a blank tile.
    Recorder failing;
    mm_tile_source badSpec = { 0, 5, &failTile, nullptr, &failing };
    mm_handle badHandle = MM_NULL_HANDLE;
    mm_source_create_custom(&context, "failing", &badSpec, &badHandle);
    TEST_CHECK(sourceOf(context, badHandle)->loadTile(MapTile(0, 0, 0, 0)) == nullptr,
               "MM_FAILED from the callback produces no tile");

    TEST_CHECK(mm_source_create_custom(&context, "dem", &spec, nullptr) == MM_DUPLICATE_ID,
               "a taken id is refused");
    mm_tile_source noLoader = { 0, 1, nullptr, nullptr, nullptr };
    TEST_CHECK(mm_source_create_custom(&context, "other", &noLoader, nullptr) == MM_BAD_SPEC,
               "a source with no loader is refused rather than crashing on the first tile");
    TEST_CHECK(mm_source_create_custom(nullptr, "other", &spec, nullptr) == MM_BAD_CONTEXT,
               "and a null context is refused");

    TEST_CHECK(mm_source_notify_changed(&context, handle, 1) == MM_OK,
               "a custom source can be told its tiles changed");
    TEST_CHECK(mm_source_notify_changed(&context, 987654, 0) == MM_BAD_HANDLE,
               "an unknown handle is refused");

    // The caller's user_data outlives the SDK's interest in it only if destroy says so. Dropping
    // the id drops the last reference, which is what has to run it - exactly once.
    TEST_CHECK(recorder.destroyed == 0, "destroy has not run while the source is registered");
    source.reset();
    tile.reset();
    raw.reset();
    TEST_CHECK(context.unregisterObject("source", "dem"), "the id drops");
    TEST_CHECK(recorder.destroyed == 1, "and the caller's destroy callback runs exactly once");
}
