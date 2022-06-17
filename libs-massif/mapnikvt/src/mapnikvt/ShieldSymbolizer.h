/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_SHIELDSYMBOLIZER_H_
#define _CARTO_MAPNIKVT_SHIELDSYMBOLIZER_H_

#include "TextSymbolizer.h"

namespace carto::mvt {
    class ShieldSymbolizer : public TextSymbolizer {
    public:
        explicit ShieldSymbolizer(const Expression& text, std::vector<std::shared_ptr<FontSet>> fontSets, std::shared_ptr<Logger> logger) : TextSymbolizer(text, std::move(fontSets), std::move(logger)) {
            bindProperty("file", &_file);
            bindProperty("shield-dx", &_shieldDx);
            bindProperty("shield-dy", &_shieldDy);
            bindProperty("unlock-image", &_unlockImage);
        }

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const override;

    protected:
        static constexpr float IMAGE_UPSAMPLING_SCALE = 2.5f;

        StringProperty _file;
        BoolProperty _unlockImage = BoolProperty(false);
        FloatProperty _shieldDx = FloatProperty(0.0f);
        FloatProperty _shieldDy = FloatProperty(0.0f);
    };
}

#endif
