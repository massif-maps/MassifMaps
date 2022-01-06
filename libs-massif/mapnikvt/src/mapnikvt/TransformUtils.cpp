#include "TransformUtils.h"
#include "Transform.h"
#include "ExpressionUtils.h"

#include <cmath>

#include <boost/math/constants/constants.hpp>

namespace carto::mvt {
    cglib::mat3x3<float> TransformEvaluator::operator() (const MatrixTransform& matTransform) const {
        std::array<float, 6> values;
        for (std::size_t i = 0; i < 6; i++) {
            values[i] = ValueConverter<float>::convert(std::visit(ExpressionEvaluator(_context, nullptr), matTransform.getValues()[i]));
        }

        cglib::mat3x3<float> mat;
        mat(0, 0) = values[0];
        mat(1, 0) = values[1];
        mat(2, 0) = 0.0f;
        mat(0, 1) = values[2];
        mat(1, 1) = values[3];
        mat(2, 1) = 0.0f;
        mat(0, 2) = values[4];
        mat(1, 2) = values[5];
        mat(2, 2) = 1.0f;
        return mat;
    }

    cglib::mat3x3<float> TransformEvaluator::operator() (const TranslateTransform& transTransform) const {
        cglib::vec2<float> translate;
        translate(0) = ValueConverter<float>::convert(std::visit(ExpressionEvaluator(_context, nullptr), transTransform.getDeltaX()));
        translate(1) = ValueConverter<float>::convert(std::visit(ExpressionEvaluator(_context, nullptr), transTransform.getDeltaY()));
        return cglib::translate3_matrix(cglib::expand(translate, 1.0f));
    }

    cglib::mat3x3<float> TransformEvaluator::operator() (const RotateTransform& rotTransform) const {
        cglib::vec2<float> origin;
        origin(0) = ValueConverter<float>::convert(std::visit(ExpressionEvaluator(_context, nullptr), rotTransform.getOriginX()));
        origin(1) = ValueConverter<float>::convert(std::visit(ExpressionEvaluator(_context, nullptr), rotTransform.getOriginY()));
        float angle = ValueConverter<float>::convert(std::visit(ExpressionEvaluator(_context, nullptr), rotTransform.getAngle()));
        return cglib::translate3_matrix(cglib::expand(origin, 1.0f)) * cglib::rotate3_matrix(cglib::vec3<float>(0, 0, 1), angle * boost::math::constants::pi<float>() / 180.0f) * cglib::translate3_matrix(cglib::expand(-origin, 1.0f));
    }

    cglib::mat3x3<float> TransformEvaluator::operator() (const ScaleTransform& scaleTransform) const {
        cglib::vec2<float> scale;
        scale(0) = ValueConverter<float>::convert(std::visit(ExpressionEvaluator(_context, nullptr), scaleTransform.getScaleX()));
        scale(1) = ValueConverter<float>::convert(std::visit(ExpressionEvaluator(_context, nullptr), scaleTransform.getScaleY()));
        return cglib::scale3_matrix(cglib::expand(scale, 1.0f));
    }

    cglib::mat3x3<float> TransformEvaluator::operator() (const SkewXTransform& skewTransform) const {
        cglib::mat3x3<float> mat = cglib::mat3x3<float>::identity();
        float angle = ValueConverter<float>::convert(std::visit(ExpressionEvaluator(_context, nullptr), skewTransform.getAngle()));
        mat(0, 1) = std::tan(angle * boost::math::constants::pi<float>() / 180.0f);
        return mat;
    }

    cglib::mat3x3<float> TransformEvaluator::operator() (const SkewYTransform& skewTransform) const {
        cglib::mat3x3<float> mat = cglib::mat3x3<float>::identity();
        float angle = ValueConverter<float>::convert(std::visit(ExpressionEvaluator(_context, nullptr), skewTransform.getAngle()));
        mat(1, 0) = std::tan(angle * boost::math::constants::pi<float>() / 180.0f);
        return mat;
    }

    std::vector<Expression> TransformSubExpressionBuilder::operator() (const MatrixTransform& matTransform) const {
        std::vector<Expression> subExprs;
        subExprs.reserve(6);
        for (std::size_t i = 0; i < 6; i++) {
            subExprs.push_back(matTransform.getValues()[i]);
        }
        return subExprs;
    }

    std::vector<Expression> TransformSubExpressionBuilder::operator() (const TranslateTransform& transTransform) const {
        return std::vector<Expression>{ transTransform.getDeltaX(), transTransform.getDeltaY() };
    }

    std::vector<Expression> TransformSubExpressionBuilder::operator() (const RotateTransform& rotTransform) const {
        return std::vector<Expression>{ rotTransform.getOriginX(), rotTransform.getOriginY(), rotTransform.getAngle() };
    }

    std::vector<Expression> TransformSubExpressionBuilder::operator() (const ScaleTransform& scaleTransform) const {
        return std::vector<Expression>{ scaleTransform.getScaleX(), scaleTransform.getScaleY() };
    }

    std::vector<Expression> TransformSubExpressionBuilder::operator() (const SkewXTransform& skewTransform) const {
        return std::vector<Expression>{ skewTransform.getAngle() };
    }

    std::vector<Expression> TransformSubExpressionBuilder::operator() (const SkewYTransform& skewTransform) const {
        return std::vector<Expression>{ skewTransform.getAngle() };
    }
}
