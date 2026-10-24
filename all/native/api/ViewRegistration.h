#ifndef _MASSIF_API_VIEWREGISTRATION_H_
#define _MASSIF_API_VIEWREGISTRATION_H_

#include "api/Context.h"

#include <memory>
#include <string>

namespace massif { namespace api {

    /**
     * What a native-owned map view puts in the registry: the view, its Options (kind "map", the
     * target of the map's events) and its Layers. All three share the view's id.
     */
    struct ViewHandles {
        Handle view = NULL_HANDLE;
        Handle map = NULL_HANDLE;
        Handle layers = NULL_HANDLE;
    };

    namespace ViewRegistration {
        extern const char* const KIND_VIEW;
        extern const char* const KIND_MAP;
        extern const char* const KIND_LAYERS;

        /**
         * All or nothing: when one id is taken nothing stays registered.
         * @return RESULT_OK, or RESULT_DUPLICATE_ID.
         */
        Result registerView(Context& context, const std::string& id,
                            const std::shared_ptr<void>& view, const std::shared_ptr<void>& options,
                            const std::shared_ptr<void>& layers, ViewHandles& out);

        /** Drops the three ids. Safe to call for an id never registered. */
        void unregisterView(Context& context, const std::string& id);
    }

} }

#endif
