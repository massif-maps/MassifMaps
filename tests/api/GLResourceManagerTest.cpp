// What a GLResourceManager does with a resource released off the GL thread, and once its context
// is gone. A queue nobody drains again holds renderers that hold the manager back (the map's
// elevation cache is one), so a destroyed map kept its whole terrain alive.

#include "renderers/utils/GLResourceManager.h"

#include "TestCheck.h"

#include <memory>
#include <thread>

namespace massif {
    void* GLContext::GetCurrentContext() {
        return nullptr;
    }
}

using namespace massif;

namespace {

    struct Tally {
        int destroyed = 0;
        int released = 0;
    };

    class Probe : public GLResource {
    public:
        Probe(const std::weak_ptr<GLResourceManager>& manager, Tally* tally, std::shared_ptr<GLResourceManager> held = nullptr) :
            GLResource(manager), _tally(tally), _held(std::move(held)) { }
        ~Probe() override { _tally->released++; }

        void create() override { }
        void destroy() override { _tally->destroyed++; }

    private:
        Tally* _tally;
        std::shared_ptr<GLResourceManager> _held; // like ElevationTextureCache, which keeps the manager it was built with
    };

    std::shared_ptr<GLResourceManager> managerOfAnotherThread() {
        std::thread other([] { });
        std::thread::id id = other.get_id();
        other.join();
        auto manager = std::make_shared<GLResourceManager>();
        manager->setGLThreadId(id);
        return manager;
    }

}

void testGLResourceManager() {
    {
        Tally tally;
        auto manager = managerOfAnotherThread();
        manager->create<Probe>(&tally).reset();
        TEST_CHECK(tally.released == 0 && tally.destroyed == 0, "a resource released off the GL thread waits for it");
        manager->setGLThreadId(std::thread::id());
        TEST_CHECK(tally.released == 1, "invalidating the thread drops what was waiting");
        TEST_CHECK(tally.destroyed == 0, "and does not call into GL for it");

        manager->create<Probe>(&tally).reset();
        TEST_CHECK(tally.released == 2 && tally.destroyed == 0, "a resource released afterwards is dropped on the spot");
    }
    {
        Tally tally;
        auto manager = managerOfAnotherThread();
        std::weak_ptr<GLResourceManager> weak = manager;
        manager->create<Probe>(&tally, manager).reset();
        manager->setGLThreadId(std::thread::id());
        manager.reset();
        TEST_CHECK(weak.expired(), "a queued resource holding its manager no longer keeps both alive");
        TEST_CHECK(tally.released == 1, "and is freed with it");
    }
}
