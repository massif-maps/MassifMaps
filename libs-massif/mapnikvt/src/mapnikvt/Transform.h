/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_TRANSFORM_H_
#define _CARTO_MAPNIKVT_TRANSFORM_H_

#include <variant>

#include <cglib/vec.h>
#include <cglib/mat.h>

namespace carto { namespace mvt {
    class MatrixTransform;
    class TranslateTransform;
    class RotateTransform;
    class ScaleTransform;
    class SkewXTransform;
    class SkewYTransform;

    using Transform = std::variant<MatrixTransform, TranslateTransform, RotateTransform, ScaleTransform, SkewXTransform, SkewYTransform>;

    class MatrixTransform final {
    public:
        MatrixTransform() : _matrix(cglib::mat3x3<float>::identity()) { }
        explicit MatrixTransform(const cglib::mat3x3<float>& matrix) : _matrix(matrix) { }

        const cglib::mat3x3<float>& getMatrix() const { return _matrix; }

    private:
        cglib::mat3x3<float> _matrix;
    };

    class TranslateTransform final {
    public:
        explicit TranslateTransform(const cglib::vec2<float>& pos) : _pos(pos) { }

        const cglib::vec2<float>& getPos() const { return _pos; }

    private:
        cglib::vec2<float> _pos;
    };

    class RotateTransform final {
    public:
        explicit RotateTransform(const cglib::vec2<float>& pos, float angle) : _pos(pos), _angle(angle) { }

        const cglib::vec2<float>& getPos() const { return _pos; }
        float getAngle() const { return _angle; }

    private:
        cglib::vec2<float> _pos;
        float _angle;
    };

    class ScaleTransform final {
    public:
        explicit ScaleTransform(const cglib::vec2<float>& scale) : _scale(scale) { }

        const cglib::vec2<float>& getScale() const { return _scale; }

    private:
        cglib::vec2<float> _scale;
    };

    class SkewXTransform final {
    public:
        explicit SkewXTransform(float angle) : _angle(angle) { }

        float getAngle() const { return _angle; }

    private:
        float _angle;
    };

    class SkewYTransform final {
    public:
        explicit SkewYTransform(float angle) : _angle(angle) { }

        float getAngle() const { return _angle; }

    private:
        float _angle;
    };
} }

#endif
