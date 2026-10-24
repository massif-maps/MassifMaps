#include "api/ViewRegistration.h"

namespace massif { namespace api {

    const char* const ViewRegistration::KIND_VIEW = "view";
    const char* const ViewRegistration::KIND_MAP = "map";
    const char* const ViewRegistration::KIND_LAYERS = "layers";

    Result ViewRegistration::registerView(Context& context, const std::string& id,
                                          const std::shared_ptr<void>& view,
                                          const std::shared_ptr<void>& options,
                                          const std::shared_ptr<void>& layers, ViewHandles& out) {
        ViewHandles handles;
        Result result = context.registerObject(KIND_VIEW, id, view, "massif::BaseMapView", handles.view);
        if (result == RESULT_OK) {
            result = context.registerObject(KIND_MAP, id, options, "massif::Options", handles.map);
        }
        if (result == RESULT_OK) {
            result = context.registerObject(KIND_LAYERS, id, layers, "massif::Layers", handles.layers);
        }
        if (result != RESULT_OK) {
            // Only what this call registered is given back: a taken id is not ours to drop.
            if (handles.layers) {
                context.unregisterObject(KIND_LAYERS, id);
            }
            if (handles.map) {
                context.unregisterObject(KIND_MAP, id);
            }
            if (handles.view) {
                context.unregisterObject(KIND_VIEW, id);
            }
            return result;
        }
        out = handles;
        return RESULT_OK;
    }

    void ViewRegistration::unregisterView(Context& context, const std::string& id) {
        context.unregisterObject(KIND_LAYERS, id);
        context.unregisterObject(KIND_MAP, id);
        context.unregisterObject(KIND_VIEW, id);
    }

} }
