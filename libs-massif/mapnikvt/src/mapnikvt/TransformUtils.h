/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_TRANSFORMUTILS_H_
#define _CARTO_MAPNIKVT_TRANSFORMUTILS_H_

#include "Transform.h"

#include <cmath>

#include <boost/math/constants/constants.hpp>

namespace carto { namespace mvt {
    struct TransformEvaluator {
        cglib::mat3x3<float> operator() (const MatrixTransform& matTransform) const { return matTransform.getMatrix(); }
        cglib::mat3x3<float> operator() (const TranslateTransform& transTransform) const { return cglib::translate3_matrix(cglib::expand(transTransform.getPos(), 1.0f)); }
        cglib::mat3x3<float> operator() (const RotateTransform& rotTransform) const { return cglib::translate3_matrix(cglib::expand(rotTransform.getPos(), 1.0f)) * cglib::rotate3_matrix(cglib::vec3<float>(0, 0, 1), rotTransform.getAngle() * boost::math::constants::pi<float>() / 180.0f) * cglib::translate3_matrix(cglib::expand(-rotTransform.getPos(), 1.0f)); }
        cglib::mat3x3<float> operator() (const ScaleTransform& scaleTransform) const { return cglib::scale3_matrix(cglib::expand(scaleTransform.getScale(), 1.0f)); }

        cglib::mat3x3<float> operator() (const SkewXTransform& skewTransform) const {
            cglib::mat3x3<float> m = cglib::mat3x3<float>::identity();
            m(0, 1) = std::tan(skewTransform.getAngle() * boost::math::constants::pi<float>() / 180.0f);
            return m;
        }

        cglib::mat3x3<float> operator() (const SkewYTransform& skewTransform) const {
            cglib::mat3x3<float> m = cglib::mat3x3<float>::identity();
            m(1, 0) = std::tan(skewTransform.getAngle() * boost::math::constants::pi<float>() / 180.0f);
            return m;
        }
    };
} }

#endif
