/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_TORQUELAYER_H_
#define _MASSIF_MAPNIKVT_TORQUELAYER_H_

#include "Layer.h"

namespace massif::mvt {
    class TorqueLayer : public Layer {
    public:
        explicit TorqueLayer(std::string name, int frameOffset, std::vector<std::string> styleNames) : Layer(std::move(name), std::move(styleNames)), _frameOffset(frameOffset) { }

        int getFrameOffset() const { return _frameOffset; }

    private:
        const int _frameOffset;
    };
}

#endif
