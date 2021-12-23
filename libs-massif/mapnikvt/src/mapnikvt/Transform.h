/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_TRANSFORM_H_
#define _CARTO_MAPNIKVT_TRANSFORM_H_

#include "ExpressionPredicateBase.h"

#include <array>

namespace carto { namespace mvt {
    class MatrixTransform final {
    public:
        explicit MatrixTransform(const std::array<Expression, 6>& values) : _values(values) { }

        const std::array<Expression, 6>& getValues() const { return _values; }

    private:
        std::array<Expression, 6> _values;
    };

    class TranslateTransform final {
    public:
        explicit TranslateTransform(Expression dx, Expression dy) : _deltaX(std::move(dx)), _deltaY(std::move(dy)) { }

        const Expression& getDeltaX() const { return _deltaX; }
        const Expression& getDeltaY() const { return _deltaY; }

    private:
        Expression _deltaX;
        Expression _deltaY;
    };

    class RotateTransform final {
    public:
        explicit RotateTransform(Expression x, Expression y, Expression angle) : _originX(std::move(x)), _originY(std::move(y)), _angle(std::move(angle)) { }

        const Expression& getOriginX() const { return _originX; }
        const Expression& getOriginY() const { return _originY; }
        const Expression& getAngle() const { return _angle; }

    private:
        Expression _originX;
        Expression _originY;
        Expression _angle;
    };

    class ScaleTransform final {
    public:
        explicit ScaleTransform(Expression sx, Expression sy) : _scaleX(std::move(sx)), _scaleY(std::move(sy)) { }

        const Expression& getScaleX() const { return _scaleX; }
        const Expression& getScaleY() const { return _scaleY; }

    private:
        Expression _scaleX;
        Expression _scaleY;
    };

    class SkewXTransform final {
    public:
        explicit SkewXTransform(Expression angle) : _angle(std::move(angle)) { }

        const Expression& getAngle() const { return _angle; }

    private:
        Expression _angle;
    };

    class SkewYTransform final {
    public:
        explicit SkewYTransform(Expression angle) : _angle(std::move(angle)) { }

        const Expression& getAngle() const { return _angle; }

    private:
        Expression _angle;
    };
} }

#endif
