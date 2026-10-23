#include "api/CustomLayer.h"
#include "api/CustomLayerHost.h"
#include "api/Context.h"
#include "api/MassifApiC.h"

using namespace massif;
using namespace massif::api;

extern "C" {

int mm_layer_create_custom(mm_ctx ctx, const char* id, const mm_custom_layer* layer, mm_handle* out) {
    Context* context = static_cast<Context*>(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    if (out) {
        *out = MM_NULL_HANDLE;
    }
    if (!CustomLayerHost::validate(layer)) {
        return MM_BAD_SPEC;
    }

    auto host = std::make_shared<CustomLayerHost>(*layer);
    // Registered as the base class, as a tile layer is: the layers list and the property chain look up "massif::Layer".
    std::shared_ptr<Layer> customLayer = std::make_shared<CustomLayer>(host);
    Handle handle = NULL_HANDLE;
    Result result = context->registerObject("layer", id ? id : "", customLayer, "massif::Layer", handle);
    if (result != RESULT_OK) {
        host->disown();
        return result;
    }
    if (out) {
        *out = handle;
    }
    return MM_OK;
}

int mm_layer_request_redraw(mm_ctx ctx, mm_handle handle) {
    Context* context = static_cast<Context*>(ctx);
    if (!context) {
        return MM_BAD_CONTEXT;
    }
    auto layer = std::dynamic_pointer_cast<CustomLayer>(
        std::static_pointer_cast<Layer>(context->getObject(handle, "massif::Layer")));
    if (!layer) {
        return MM_BAD_HANDLE;
    }
    layer->requestRedraw();
    return MM_OK;
}

}
