#include "api/CustomLayer.h"
#include "api/CustomLayerHost.h"
#include "api/GLStateSnapshot.h"
#include "components/ClassRegistry.h"
#include "graphics/ViewState.h"
#include "renderers/MapRenderer.h"
#include "renderers/utils/GLContext.h"
#include "renderers/utils/GLResource.h"
#include "renderers/utils/GLResourceManager.h"
#include "utils/Log.h"

#include <algorithm>
#include <vector>

namespace massif { namespace api {

    // The Java side only knows Layer: without this swigGetClassName logs an error on every wrap.
    static ClassRegistry::Entry customLayerRegistryEntry(typeid(const CustomLayer&), "Layer");

    namespace {
        // The manager runs destroy on the GL thread when the owner lets go, which is where a caller's GL objects can be deleted.
        class HostGLResource : public GLResource {
        public:
            HostGLResource(const std::weak_ptr<GLResourceManager>& manager, const std::shared_ptr<CustomLayerHost>& host) :
                GLResource(manager),
                _host(host)
            {
            }

        protected:
            virtual void create() {
            }

            virtual void destroy() {
                _host->release();
            }

        private:
            const std::shared_ptr<CustomLayerHost> _host;
        };

#ifndef NDEBUG
        int integer(GLenum name) {
            GLint value = 0;
            glGetIntegerv(name, &value);
            return value;
        }

        void integers(GLenum name, int* values, int count) {
            GLint buffer[4] = { 0, 0, 0, 0 };
            glGetIntegerv(name, buffer);
            for (int i = 0; i < count; i++) {
                values[i] = buffer[i];
            }
        }

        GLStateSnapshot captureGLState() {
            GLStateSnapshot state;
            state.program = integer(GL_CURRENT_PROGRAM);
            state.vertexArray = integer(GL_VERTEX_ARRAY_BINDING);
            state.arrayBuffer = integer(GL_ARRAY_BUFFER_BINDING);
            state.elementArrayBuffer = integer(GL_ELEMENT_ARRAY_BUFFER_BINDING);
            state.activeTexture = integer(GL_ACTIVE_TEXTURE);
            state.texture2D = integer(GL_TEXTURE_BINDING_2D);
            state.framebuffer = integer(GL_FRAMEBUFFER_BINDING);
            state.blend = glIsEnabled(GL_BLEND);
            state.blendFunc[0] = integer(GL_BLEND_SRC_RGB);
            state.blendFunc[1] = integer(GL_BLEND_DST_RGB);
            state.blendFunc[2] = integer(GL_BLEND_SRC_ALPHA);
            state.blendFunc[3] = integer(GL_BLEND_DST_ALPHA);
            state.depthTest = glIsEnabled(GL_DEPTH_TEST);
            state.depthMask = integer(GL_DEPTH_WRITEMASK);
            state.depthFunc = integer(GL_DEPTH_FUNC);
            state.cullFace = glIsEnabled(GL_CULL_FACE);
            state.cullFaceMode = integer(GL_CULL_FACE_MODE);
            state.scissorTest = glIsEnabled(GL_SCISSOR_TEST);
            integers(GL_SCISSOR_BOX, state.scissorBox, 4);
            integers(GL_VIEWPORT, state.viewport, 4);
            return state;
        }
#endif

        bool sameManager(const std::weak_ptr<GLResourceManager>& a, const std::weak_ptr<GLResourceManager>& b) {
            return !a.owner_before(b) && !b.owner_before(a);
        }
    }

    CustomLayer::CustomLayer(const std::shared_ptr<CustomLayerHost>& host) :
        Layer(),
        _host(host),
        _glResource(),
        _glManager()
    {
    }

    CustomLayer::~CustomLayer() {
    }

    bool CustomLayer::isUpdateInProgress() const {
        return false;
    }

    void CustomLayer::requestRedraw() const {
        redraw();
    }

    void CustomLayer::setComponents(const std::shared_ptr<CancelableThreadPool>& envelopeThreadPool,
                                    const std::shared_ptr<CancelableThreadPool>& tileThreadPool,
                                    const std::weak_ptr<Options>& options,
                                    const std::weak_ptr<MapRenderer>& mapRenderer,
                                    const std::weak_ptr<TouchHandler>& touchHandler)
    {
        Layer::setComponents(envelopeThreadPool, tileThreadPool, options, mapRenderer, touchHandler);
        if (!mapRenderer.lock()) {
            std::lock_guard<std::recursive_mutex> lock(_mutex);
            _glResource.reset();
            _glManager.reset();
        } else {
            // Nothing loads for this layer, so nothing else would ask for the frame that first draws it.
            redraw();
        }
    }

    void CustomLayer::loadData(const std::shared_ptr<CullState>& cullState) {
    }

    void CustomLayer::offsetLayerHorizontally(double offset) {
    }

    bool CustomLayer::onDrawFrame(float deltaSeconds, BillboardSorter& billboardSorter, const ViewState& viewState) {
        return false;
    }

    bool CustomLayer::onDrawOverlay(float deltaSeconds, const ViewState& viewState) {
        if (!isVisible() || !getVisibleZoomRange().inRange(viewState.getZoom()) || getOpacity() <= 0) {
            return false;
        }

        std::lock_guard<std::recursive_mutex> lock(_mutex);
        std::shared_ptr<MapRenderer> mapRenderer = getMapRenderer();
        std::shared_ptr<GLResourceManager> manager = mapRenderer ? mapRenderer->getGLResourceManager() : nullptr;
        if (!manager) {
            return false;
        }
        // A manager per GL context, and Android may never say the old one died.
        std::weak_ptr<GLResourceManager> managerWeak(manager);
        if (!_glResource || !sameManager(_glManager, managerWeak)) {
            if (_glResource) {
                _host->contextLost();
            }
            _glResource = manager->create<HostGLResource>(_host);
            _glManager = managerWeak;
        }

        mm_render_params params;
        std::fill_n(reinterpret_cast<char*>(&params), sizeof(params), 0);
        params.size = sizeof(params);
        GLint framebuffer = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        params.framebuffer = static_cast<uint32_t>(framebuffer);

        const cglib::mat4x4<double>& mvp = viewState.getModelviewProjectionMat();
        std::copy_n(mvp.data(), 16, params.mvp);
        const cglib::mat4x4<float>& mvpLocal = viewState.getRTEModelviewProjectionMat();
        std::copy_n(mvpLocal.data(), 16, params.mvp_local);
        const cglib::vec3<double>& cameraPos = viewState.getCameraPos();
        params.origin[0] = cameraPos(0);
        params.origin[1] = cameraPos(1);
        params.origin[2] = cameraPos(2);
        params.delta_seconds = deltaSeconds;
        params.viewport[2] = viewState.getWidth();
        params.viewport[3] = viewState.getHeight();
        params.opacity = getOpacity();
        params.zoom = viewState.getZoom();
        params.tilt = viewState.getTilt();
        params.rotation = viewState.getRotation();
        params.fov_y = viewState.getFOVY();
        params.near_plane = viewState.getNear();
        params.far_plane = viewState.getFar();

        glDisable(GL_DEPTH_TEST);
#ifndef NDEBUG
        GLStateSnapshot before = captureGLState();
#endif
        _host->render(params);
#ifndef NDEBUG
        std::string leaked = GLStateSnapshot::describeChanges(before, captureGLState());
        if (leaked != _reportedLeak) {
            _reportedLeak = leaked;
            if (!leaked.empty()) {
                Log::Errorf("CustomLayer: render left the GL state changed (%s); the next layer will draw wrong", leaked.c_str());
            }
        }
#endif
        glEnable(GL_DEPTH_TEST);
        return false;
    }

    void CustomLayer::calculateRayIntersectedElements(const cglib::ray3<double>& ray, const ViewState& viewState, std::vector<RayIntersectedElement>& results) const {
    }

    bool CustomLayer::processClick(const ClickInfo& clickInfo, const RayIntersectedElement& intersectedElement, const ViewState& viewState) const {
        return false;
    }

    void CustomLayer::registerDataSourceListener() {
    }

    void CustomLayer::unregisterDataSourceListener() {
    }

} }
