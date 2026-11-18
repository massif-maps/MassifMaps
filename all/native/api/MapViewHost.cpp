#include "api/MapViewHost.h"
#include "api/Builtins.h"
#include "api/Context.h"
#include "api/MapEventBridge.h"
#include "api/ViewRegistration.h"
#include "components/Layers.h"
#include "components/Options.h"
#include "renderers/RedrawRequestListener.h"
#include "ui/BaseMapView.h"

namespace massif { namespace api {

    namespace {

        std::shared_ptr<BaseMapView> viewOf(int handle) {
            return std::static_pointer_cast<BaseMapView>(
                Context::GetDefault()->getObject(static_cast<Handle>(handle), "massif::BaseMapView"));
        }

        // Keeps the facade projection equal to Options.baseProjection: read once, as adopt does, it
        // goes stale when an app sets one after the view exists.
        class ProjectionSync : public Options::OnChangeListener {
        public:
            ProjectionSync(const std::shared_ptr<Context>& context, Handle handle,
                           const std::shared_ptr<BaseMapView>& view) :
                _context(context), _handle(handle), _view(view) { }

            virtual void onOptionChanged(const std::string& optionName) {
                if (optionName == "BaseProjection") {
                    apply();
                }
            }

            void apply() {
                auto view = _view.lock();
                if (view && _context->getObject(_handle) == std::static_pointer_cast<void>(view)) {
                    _context->setObjectProjection(_handle, view->getOptions()->getBaseProjection());
                }
            }

        private:
            std::shared_ptr<Context> _context;
            Handle _handle;
            std::weak_ptr<BaseMapView> _view;
        };

    }

    int MapViewHost::create(const std::string& id, float dpi) {
        registerBuiltins();
        const std::shared_ptr<Context>& context = Context::GetDefault();
        auto view = std::make_shared<BaseMapView>();
        if (dpi > 0) {
            view->getOptions()->setDPI(dpi);
        }
        ViewHandles handles;
        if (ViewRegistration::registerView(*context, id, view, view->getOptions(), view->getLayers(),
                                           handles) != RESULT_OK) {
            return NULL_HANDLE;
        }
        auto projection = std::make_shared<ProjectionSync>(context, handles.view, view);
        view->getOptions()->registerOnChangeListener(projection);
        projection->apply();
        view->setMapEventListener(std::make_shared<MapEventBridge>(context, handles.map, nullptr));
        return static_cast<int>(handles.view);
    }

    void MapViewHost::destroy(const std::string& id) {
        const std::shared_ptr<Context>& context = Context::GetDefault();
        if (auto view = viewOf(static_cast<int>(context->findObject(ViewRegistration::KIND_VIEW, id)))) {
            view->setRedrawRequestListener(std::shared_ptr<RedrawRequestListener>());
            view->setMapEventListener(std::shared_ptr<MapEventListener>());
        }
        ViewRegistration::unregisterView(*context, id);
    }

    void MapViewHost::setRedrawRequestListener(int handle, const std::shared_ptr<RedrawRequestListener>& listener) {
        if (auto view = viewOf(handle)) {
            view->setRedrawRequestListener(listener);
        }
    }

    void MapViewHost::onSurfaceCreated(int handle) {
        if (auto view = viewOf(handle)) {
            view->onSurfaceCreated();
        }
    }

    void MapViewHost::onSurfaceChanged(int handle, int width, int height) {
        if (auto view = viewOf(handle)) {
            view->onSurfaceChanged(width, height);
        }
    }

    void MapViewHost::onSurfaceDestroyed(int handle) {
        if (auto view = viewOf(handle)) {
            view->onSurfaceDestroyed();
        }
    }

    void MapViewHost::onDrawFrame(int handle) {
        if (auto view = viewOf(handle)) {
            view->onDrawFrame();
        }
    }

    void MapViewHost::onInputEvent(int handle, int event, float x1, float y1, float x2, float y2) {
        if (auto view = viewOf(handle)) {
            view->onInputEvent(event, x1, y1, x2, y2);
        }
    }

} }
