/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_CUSTOMLAYERHOST_H_
#define _MASSIF_API_CUSTOMLAYERHOST_H_

#include "api/MassifApiC.h"

namespace massif { namespace api {

    /**
     * The caller's mm_custom_layer vtable and its lifetime, apart from the Layer that draws with it,
     * so the contract can be tested without the renderer.
     */
    class CustomLayerHost {
    public:
        /** Whether a vtable can be taken: a render function, a known mode, and the fields this SDK reads. */
        static bool validate(const mm_custom_layer* layer);

        /** Copies up to layer.size bytes; a field the caller's struct predates reads as null. */
        explicit CustomLayerHost(const mm_custom_layer& layer);
        /** Calls destroy, once. */
        ~CustomLayerHost();

        /** Drops destroy, for a host that was built and then not handed over. */
        void disown();

        mm_custom_layer_mode getMode() const;

        void render(const mm_render_params& params) const;
        void release() const;
        void contextLost() const;

        /** False when the caller gave no hit_test, so the layer need not be asked. */
        bool canHitTest() const;
        bool hitTest(const double origin[3], const double direction[3], double& t, uint64_t& feature) const;
        /** True when the caller consumed the click. */
        bool click(uint64_t feature, int clickType, const double hitPos[3]) const;

    private:
        mm_custom_layer _layer;
    };

} }

#endif
