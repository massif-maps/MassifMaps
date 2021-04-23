/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_SHIELDSYMBOLIZER_H_
#define _CARTO_MAPNIKVT_SHIELDSYMBOLIZER_H_

#include "TextSymbolizer.h"

namespace carto { namespace mvt {
    class ShieldSymbolizer : public TextSymbolizer {
    public:
        explicit ShieldSymbolizer(const Expression& text, std::vector<std::shared_ptr<FontSet>> fontSets, std::shared_ptr<Logger> logger) : TextSymbolizer(text, std::move(fontSets), std::move(logger)) {
            bindParameter("file", &_file);
            bindParameter("shield-dx", &_shieldDx);
            bindParameter("shield-dy", &_shieldDy);
            bindParameter("unlock-image", &_unlockImage);
        }

        virtual void build(const FeatureCollection& featureCollection, const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext, vt::TileLayerBuilder& layerBuilder) const override;

    protected:
        inline static constexpr float IMAGE_UPSAMPLING_SCALE = 2.5f;

        StringParameter _file;
        BoolParameter _unlockImage = BoolParameter(false);
        FloatParameter _shieldDx = FloatParameter(0.0f);
        FloatParameter _shieldDy = FloatParameter(0.0f);
    };
} }

#endif
