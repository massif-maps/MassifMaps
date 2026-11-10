/*
 * An SDK object's destructor runs OUTSIDE the context lock.
 *
 * A destroyed object can own anything: a map view joins its threads, a layer takes its own locks.
 * Destroyed under the context lock, a destructor that reaches back into the context - a listener
 * releasing its payload - blocked on a lock its own thread already held.
 */

#include "api/Context.h"
#include "core/MapTile.h"
#include "datasources/TileDataSource.h"
#include "datasources/components/TileData.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "TestCheck.h"

using namespace massif;
using namespace massif::api;

namespace {
    class ReachingBackSource : public TileDataSource {
    public:
        ReachingBackSource(Context& context, std::atomic<int>& destroyed) :
            TileDataSource(4, 15), _context(context), _destroyed(destroyed) { }

        ~ReachingBackSource() {
            _context.findObject("source", "reaching-back");
            _destroyed++;
        }

        virtual std::shared_ptr<TileData> loadTile(const MapTile& tile) override {
            return std::shared_ptr<TileData>();
        }

    private:
        Context& _context;
        std::atomic<int>& _destroyed;
    };

    template <typename Action>
    bool finishesWithin(Action action) {
        std::mutex mutex;
        std::condition_variable done;
        bool finished = false;
        std::thread worker([&] {
            action();
            std::lock_guard<std::mutex> lock(mutex);
            finished = true;
            done.notify_all();
        });
        std::unique_lock<std::mutex> lock(mutex);
        bool ok = done.wait_for(lock, std::chrono::seconds(10), [&] { return finished; });
        lock.unlock();
        if (ok) {
            worker.join();
        } else {
            worker.detach();
        }
        return ok;
    }
}

void testDestroyLock() {
    // Leaked on purpose when the test fails: the deadlocked thread still holds the context.
    Context* context = new Context();
    std::atomic<int>* destroyed = new std::atomic<int>(0);

    Handle handle = NULL_HANDLE;
    TEST_CHECK(context->registerObject("source", "reaching-back",
                                       std::make_shared<ReachingBackSource>(*context, *destroyed),
                                       "massif::TileDataSource", handle) == RESULT_OK,
               "a source whose destructor reads the context registers");
    bool destroyReturned = finishesWithin([&] { context->destroy(handle); });
    TEST_CHECK(destroyReturned, "destroy returns when the destructor reaches back into the context");
    if (!destroyReturned) {
        return;
    }
    TEST_CHECK(destroyed->load() == 1, "and the object is gone by the time destroy returns");
    TEST_CHECK(context->findObject("source", "reaching-back") == NULL_HANDLE, "its id is free again");

    handle = NULL_HANDLE;
    context->registerObject("source", "reaching-back",
                            std::make_shared<ReachingBackSource>(*context, *destroyed),
                            "massif::TileDataSource", handle);
    context->retain(handle);
    context->destroy(handle);
    TEST_CHECK(destroyed->load() == 1, "a retained object outlives its destroy");
    bool releaseReturned = finishesWithin([&] { context->release(handle); });
    TEST_CHECK(releaseReturned, "the last release returns when the destructor reaches back into the context");
    if (!releaseReturned) {
        return;
    }
    TEST_CHECK(destroyed->load() == 2, "and it is destroyed by that release");
    delete context;
    delete destroyed;
}
