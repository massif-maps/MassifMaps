/*
 * Tests for the vtable behind mm_layer_create_custom - a layer the CALLER draws, reached over the C ABI.
 *
 * What is checked is the contract the C struct makes: which vtables are taken, that the caller's
 * struct is copied, that each callback
 * reaches the caller with its user_data, and that destroy runs exactly once and never for a
 * vtable the SDK refused or was handed back. The GL state guard's comparison is checked too: what
 * it names when a render leaks, and that it stays silent when none does.
 *
 * The Layer that calls render needs the renderer, and so does a real draw: depth, framebuffer and
 * GL state are device checks. See tests/README.md.
 */

#include "api/CustomLayerHost.h"
#include "api/GLStateSnapshot.h"
#include "api/MassifApiC.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>

using namespace massif::api;

#include "TestCheck.h"

namespace {

    struct Recorder {
        int renders = 0;
        int removed = 0;
        int contextLost = 0;
        int destroyed = 0;
        uint32_t framebuffer = 0;
        float zoom = 0;
    };

    void render(void* userData, const mm_render_params* params) {
        Recorder* recorder = static_cast<Recorder*>(userData);
        recorder->renders++;
        recorder->framebuffer = params->framebuffer;
        recorder->zoom = params->zoom;
    }

    void removed(void* userData) {
        static_cast<Recorder*>(userData)->removed++;
    }

    void contextLost(void* userData) {
        static_cast<Recorder*>(userData)->contextLost++;
    }

    void destroy(void* userData) {
        static_cast<Recorder*>(userData)->destroyed++;
    }

    mm_custom_layer vtable(Recorder* recorder) {
        mm_custom_layer layer;
        std::memset(&layer, 0, sizeof(layer));
        layer.size = sizeof(layer);
        layer.mode = MM_CUSTOM_LAYER_OVERLAY;
        layer.on_removed = &removed;
        layer.on_context_lost = &contextLost;
        layer.render = &render;
        layer.destroy = &destroy;
        layer.user_data = recorder;
        return layer;
    }

}

void testCustomLayer() {
    Recorder recorder;
    mm_custom_layer layer = vtable(&recorder);

    TEST_CHECK(CustomLayerHost::validate(&layer), "a vtable with a render function is taken");
    TEST_CHECK(!CustomLayerHost::validate(nullptr), "a null vtable is refused");

    mm_custom_layer noRender = layer;
    noRender.render = nullptr;
    TEST_CHECK(!CustomLayerHost::validate(&noRender),
               "a layer with nothing to draw is refused rather than crashing on the first frame");

    mm_custom_layer scene = layer;
    scene.mode = MM_CUSTOM_LAYER_SCENE;
    scene.destroy = nullptr;
    TEST_CHECK(CustomLayerHost::validate(&scene) && CustomLayerHost(scene).getMode() == MM_CUSTOM_LAYER_SCENE,
               "the scene mode is taken and kept");

    mm_custom_layer badMode = layer;
    badMode.mode = static_cast<mm_custom_layer_mode>(99);
    TEST_CHECK(!CustomLayerHost::validate(&badMode), "a mode this SDK does not know is refused");

    mm_custom_layer tooShort = layer;
    tooShort.size = offsetof(mm_custom_layer, render);
    TEST_CHECK(!CustomLayerHost::validate(&tooShort),
               "a struct that ends before the fields the SDK reads is refused, not read past its end");

    {
        CustomLayerHost host(layer);
        // The caller's struct is a local in real use.
        std::memset(&layer, 0, sizeof(layer));

        mm_render_params params;
        std::memset(&params, 0, sizeof(params));
        params.framebuffer = 7;
        params.zoom = 11.5f;
        host.render(params);
        TEST_CHECK(recorder.renders == 1, "render reaches the caller");
        TEST_CHECK(recorder.framebuffer == 7 && recorder.zoom == 11.5f,
                   "with the parameters, and the caller's user_data");

        host.contextLost();
        host.release();
        TEST_CHECK(recorder.contextLost == 1 && recorder.removed == 1,
                   "on_context_lost and on_removed reach the caller");
        TEST_CHECK(recorder.destroyed == 0, "destroy has not run while the layer is alive");
    }
    TEST_CHECK(recorder.destroyed == 1, "destroy runs exactly once when the layer is dropped");

    // An optional callback the caller left out is skipped; a vtable that is a bare render is legal.
    Recorder bare;
    mm_custom_layer bareLayer = vtable(&bare);
    bareLayer.on_removed = nullptr;
    bareLayer.on_context_lost = nullptr;
    bareLayer.destroy = nullptr;
    {
        CustomLayerHost host(bareLayer);
        host.contextLost();
        host.release();
        TEST_CHECK(bare.contextLost == 0 && bare.removed == 0,
                   "an optional callback left null is skipped");
    }
    TEST_CHECK(bare.destroyed == 0, "and so is destroy");

    // mm_layer_create_custom promises that a failed create takes nothing: user_data stays the caller's.
    Recorder refused;
    mm_custom_layer refusedLayer = vtable(&refused);
    {
        CustomLayerHost host(refusedLayer);
        host.disown();
    }
    TEST_CHECK(refused.destroyed == 0, "a disowned host never calls destroy");

    GLStateSnapshot before;
    GLStateSnapshot after = before;
    TEST_CHECK(GLStateSnapshot::describeChanges(before, after).empty(),
               "an unchanged GL state reports nothing");

    after.program = 5;
    after.vertexArray = 3;
    after.viewport[2] = 800;
    std::string leaked = GLStateSnapshot::describeChanges(before, after);
    TEST_CHECK(leaked.find("program 0->5") != std::string::npos, "a leaked program is named with both values");
    TEST_CHECK(leaked.find("vertex array 0->3") != std::string::npos,
               "so is a bound VAO, the leak that corrupts the next layer");
    TEST_CHECK(leaked.find("viewport [0,0,0,0]->[0,0,800,0]") != std::string::npos,
               "and a changed viewport, with the whole rectangle");
    TEST_CHECK(leaked.find("blend") == std::string::npos, "and nothing the render left alone");
}
