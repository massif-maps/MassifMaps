/*
 * The tile-load and frame event bridges: each callback emits its event on the target handle, after
 * the listener the app had installed. What is out of scope: attaching them to a layer or a
 * renderer (that needs both) and MethodImpls' setColorParameter, which is only reachable on a
 * device. The colour crossing is checked on the class the thunk calls.
 */

#include "api/Context.h"
#include "api/MapEventBridge.h"
#include "components/Options.h"
#include "layers/TileLoadListener.h"
#include "renderers/MapRendererListener.h"
#include "renderers/PostProcessEffect.h"

#include <memory>
#include <string>
#include <vector>

using namespace massif;
using namespace massif::api;

#include "TestCheck.h"

namespace {

    std::vector<std::string> order;

    int record(void*, std::uint32_t, const char* event, std::uint32_t) {
        order.push_back(event);
        return false;
    }

    class ChainedTiles : public TileLoadListener {
    public:
        void onVisibleTilesLoaded() { order.push_back("chained.visible"); }
        void onPreloadingTilesLoaded() { order.push_back("chained.preloading"); }
    };

    class ChainedFrames : public MapRendererListener {
    public:
        void onSurfaceChanged(int, int) { order.push_back("chained.surface"); }
        void onBeforeDrawFrame() { order.push_back("chained.before"); }
        void onAfterDrawFrame() { order.push_back("chained.after"); }
    };

}

void testTileFrameEvents() {
    auto context = std::make_shared<Context>();
    Handle target = NULL_HANDLE;
    context->registerObject("options", "tiles", std::make_shared<PostProcessEffect>("fx", "void main() {}"),
                            "massif::PostProcessEffect", target);
    for (const char* event : { "tiles.loaded", "tiles.preloaded", "frame.before", "frame.after" }) {
        context->subscribe(target, event, &record, nullptr, false);
    }

    auto chainedTiles = std::make_shared<ChainedTiles>();
    TileLoadEventBridge tiles(context, target, chainedTiles);
    tiles.onVisibleTilesLoaded();
    TEST_CHECK(order == (std::vector<std::string>{ "chained.visible", "tiles.loaded" }),
               "tiles.loaded is emitted after the app's own listener ran");
    order.clear();
    tiles.onPreloadingTilesLoaded();
    TEST_CHECK(order == (std::vector<std::string>{ "chained.preloading", "tiles.preloaded" }),
               "tiles.preloaded is its own event, not tiles.loaded");
    order.clear();

    TileLoadEventBridge bareTiles(context, target, nullptr);
    bareTiles.onVisibleTilesLoaded();
    TEST_CHECK(order == (std::vector<std::string>{ "tiles.loaded" }),
               "a bridge with no chained listener still emits");
    order.clear();

    auto chainedFrames = std::make_shared<ChainedFrames>();
    FrameEventBridge frames(context, target, chainedFrames);
    frames.onBeforeDrawFrame();
    frames.onAfterDrawFrame();
    TEST_CHECK(order == (std::vector<std::string>{ "chained.before", "frame.before", "chained.after", "frame.after" }),
               "frame.before and frame.after bracket the frame, each after the chained listener");
    order.clear();
    frames.onSurfaceChanged(10, 20);
    TEST_CHECK(order == (std::vector<std::string>{ "chained.surface" }),
               "a surface change is forwarded and emits nothing");
    order.clear();

    PostProcessEffect effect("fx", "void main() {}");
    int argb = static_cast<int>(0x80FF4020u);
    effect.setColorParameter("tint", Color(argb));
    TEST_CHECK(effect.getColorParameter("tint").getARGB() == argb,
               "a colour parameter keeps its alpha through the signed ARGB int the facade passes");
    TEST_CHECK(effect.getColorParameters().size() == 1, "and is stored once under its name");
}
