/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_LINEPATTERNSYMBOLIZER_H_
#define _CARTO_MAPNIKVT_LINEPATTERNSYMBOLIZER_H_

#include "GeometrySymbolizer.h"
#include "FunctionBuilder.h"

namespace carto::mvt {
    class LinePatternSymbolizer : public GeometrySymbolizer {
    public:
        explicit LinePatternSymbolizer(std::shared_ptr<Logger> logger) : GeometrySymbolizer(std::move(logger)) {
            bindParameter("file", &_file);
            bindParameter("fill", &_fill);
            bindParameter("opacity", &_opacity);
            bindParameter("offset", &_offset);
        }

        virtual FeatureProcessor createFeatureProcessor(const ExpressionContext& exprContext, const SymbolizerContext& symbolizerContext) const override;
        
    protected:
        static constexpr float PATTERN_SCALE = 0.75f;
        static constexpr float PATTERN_DOT_LIMIT = 0.2f;

        StringParameter _file;
        ColorFunctionParameter _fill = ColorFunctionParameter("#ffffff");
        FloatFunctionParameter _opacity = FloatFunctionParameter(1.0f);
        FloatFunctionParameter _offset = FloatFunctionParameter(0.0f);

        FloatFunctionBuilder _widthFuncBuilder;
        ColorFunctionBuilder _fillFuncBuilder;
    };
}

#endif
