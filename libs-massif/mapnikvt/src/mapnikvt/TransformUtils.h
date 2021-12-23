/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_TRANSFORMUTILS_H_
#define _CARTO_MAPNIKVT_TRANSFORMUTILS_H_

#include "Transform.h"
#include "ExpressionContext.h"

namespace carto { namespace mvt {
    struct TransformEvaluator {
        explicit TransformEvaluator(const ExpressionContext& context) : _context(context) { }
        
        cglib::mat3x3<float> operator() (const MatrixTransform& matTransform) const;
        cglib::mat3x3<float> operator() (const TranslateTransform& transTransform) const;
        cglib::mat3x3<float> operator() (const RotateTransform& rotTransform) const;
        cglib::mat3x3<float> operator() (const ScaleTransform& scaleTransform) const;
        cglib::mat3x3<float> operator() (const SkewXTransform& skewTransform) const;
        cglib::mat3x3<float> operator() (const SkewYTransform& skewTransform) const;

    private:
        const ExpressionContext& _context;
    };

    struct TransformSubExpressionBuilder {
        std::vector<Expression> operator() (const MatrixTransform& matTransform) const;
        std::vector<Expression> operator() (const TranslateTransform& transTransform) const;
        std::vector<Expression> operator() (const RotateTransform& rotTransform) const;
        std::vector<Expression> operator() (const ScaleTransform& scaleTransform) const;
        std::vector<Expression> operator() (const SkewXTransform& skewTransform) const;
        std::vector<Expression> operator() (const SkewYTransform& skewTransform) const;
    };
} }

#endif
