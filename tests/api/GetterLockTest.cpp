/*
 * A property getter runs OUTSIDE the context lock, as a setter and a call do.
 *
 * The render thread takes the renderer's mutex for a frame and, from inside it, reaches the
 * context: the auto-flatten rule writes an option, the option notifies a map-moved listener, and
 * an app's listener reads the camera back. An app thread reading the camera at the same moment
 * held the context lock while its getter waited for the renderer's mutex - and the two threads
 * never met again. The day-cycle-light example hung at startup about one launch in sixty.
 *
 * Modelled with one lock standing in for the renderer's: a getter that takes it, and a thread
 * that holds it while it reads the context.
 */

#include "api/Context.h"
#include "core/MapTile.h"
#include "datasources/TileDataSource.h"
#include "datasources/components/TileData.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "TestCheck.h"

using namespace massif;
using namespace massif::api;

namespace {
    struct Renderer {
        std::mutex mutex;              // the renderer's, held for a whole frame

        std::mutex stateMutex;
        std::condition_variable changed;
        bool inGetter = false;
        int finished = 0;
        Result renderResult = RESULT_OK;
        Result appResult = RESULT_OK;
        long long appValue = 0;

        void enteredGetter() {
            std::lock_guard<std::mutex> lock(stateMutex);
            inGetter = true;
            changed.notify_all();
        }

        void waitInGetter() {
            std::unique_lock<std::mutex> lock(stateMutex);
            changed.wait(lock, [this] { return inGetter; });
        }

        void done() {
            std::lock_guard<std::mutex> lock(stateMutex);
            finished++;
            changed.notify_all();
        }
    };

    /** The camera: its getter needs the renderer's lock. */
    class LockedSource : public TileDataSource {
    public:
        explicit LockedSource(Renderer& renderer) : TileDataSource(4, 15), _renderer(renderer) { }

        virtual int getMinZoom() const override {
            _renderer.enteredGetter();
            std::lock_guard<std::mutex> lock(_renderer.mutex);
            return TileDataSource::getMinZoom();
        }

        virtual std::shared_ptr<TileData> loadTile(const MapTile& tile) override {
            return std::shared_ptr<TileData>();
        }

    private:
        Renderer& _renderer;
    };
}

void testGetterLock() {
    // Leaked on purpose when the test fails: two deadlocked threads still hold them.
    Context* context = new Context();
    Renderer* renderer = new Renderer();
    auto source = std::make_shared<LockedSource>(*renderer);
    Handle handle = NULL_HANDLE;
    TEST_CHECK(context->registerObject("source", "locked", source, "massif::TileDataSource", handle) == RESULT_OK,
               "a source with a locking getter registers");

    // The render thread: holds its lock for the frame, and once the app thread is inside a getter
    // reads the context itself - the map-moved listener reading the camera back.
    std::thread render([context, renderer, handle] {
        std::lock_guard<std::mutex> frame(renderer->mutex);
        renderer->waitInGetter();
        PropertyValue value;
        renderer->renderResult = context->getProperty(handle, "maxZoom", value);
        renderer->done();
    });
    // The app thread: reads the property whose getter waits for the renderer.
    std::thread app([context, renderer, handle] {
        PropertyValue value;
        renderer->appResult = context->getProperty(handle, "minZoom", value);
        renderer->appValue = value.intValue;
        renderer->done();
    });

    bool finished = false;
    {
        std::unique_lock<std::mutex> lock(renderer->stateMutex);
        finished = renderer->changed.wait_for(lock, std::chrono::seconds(10), [renderer] { return renderer->finished == 2; });
    }
    TEST_CHECK(finished, "a getter waiting for the renderer does not hold the context lock against the render thread");
    if (!finished) {
        render.detach();
        app.detach();
        return;
    }
    render.join();
    app.join();
    TEST_CHECK(renderer->renderResult == RESULT_OK, "the render thread's read went through");
    TEST_CHECK(renderer->appResult == RESULT_OK && renderer->appValue == 4, "and the app thread's read returned the value");
    delete context;
    delete renderer;
}
