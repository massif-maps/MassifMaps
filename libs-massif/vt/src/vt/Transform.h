/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_TRANSFORM_H_
#define _CARTO_VT_TRANSFORM_H_

#include <functional>

#include <cglib/vec.h>
#include <cglib/mat.h>

namespace carto::vt {
    class Transform final {
    public:
        Transform() : _matrix { 1, 0, 0, 1 }, _translate { 0, 0 } { }
        
        explicit Transform(float m00, float m10, float m01, float m11, float t0, float t1) : _matrix { m00, m10, m01, m11 }, _translate { t0, t1 } { }

        cglib::vec2<float> translate() const {
            return cglib::vec2<float>(_translate[0], _translate[1]);
        }

        cglib::mat2x2<float> matrix2() const {
            cglib::mat2x2<float> mat;
            mat(0, 0) = _matrix[0]; mat(0, 1) = _matrix[2];
            mat(1, 0) = _matrix[1]; mat(1, 1) = _matrix[3];
            return mat;
        }

        cglib::mat3x3<float> matrix3() const {
            cglib::mat3x3<float> mat;
            mat(0, 0) = _matrix[0]; mat(0, 1) = _matrix[2]; mat(0, 2) = _translate[0];
            mat(1, 0) = _matrix[1]; mat(1, 1) = _matrix[3]; mat(1, 2) = _translate[1];
            mat(2, 0) = 0;          mat(2, 1) = 0;          mat(2, 2) = 1;
            return mat;
        }

        static Transform fromTranslate(const cglib::vec2<float>& vec) {
            return Transform(1, 0, 0, 1, vec(0), vec(1));
        }

        static Transform fromMatrix2(const cglib::mat2x2<float>& mat) {
            return Transform(mat(0, 0), mat(1, 0), mat(0, 1), mat(1, 1), 0, 0);
        }

        static Transform fromMatrix2Translate(const cglib::mat2x2<float>& mat, const cglib::vec2<float>& vec) {
            return Transform(mat(0, 0), mat(1, 0), mat(0, 1), mat(1, 1), vec(0), vec(1));
        }

        static Transform fromMatrix3(const cglib::mat3x3<float>& mat) {
            return Transform(mat(0, 0), mat(1, 0), mat(0, 1), mat(1, 1), mat(0, 2), mat(1, 2));
        }

    private:
        float _matrix[4]; // [m00, m10, m01, m11]
        float _translate[2]; // [t0, t1]
    };

    inline Transform operator * (const Transform& transform1, const Transform& transform2) {
        return Transform::fromMatrix3(transform1.matrix3() * transform2.matrix3());
    }

    inline bool operator == (const Transform& transform1, const Transform& transform2) {
        return transform1.translate() == transform2.translate() && transform1.matrix2() == transform2.matrix2();
    }
    
    inline bool operator != (const Transform& transform1, const Transform& transform2) {
        return !(transform1 == transform2);
    }
}

namespace std {
    template <>
    struct hash<carto::vt::Transform> {
        std::size_t operator() (const carto::vt::Transform& transform) const {
            std::size_t hash = 0;
            cglib::mat3x3<float> mat = transform.matrix3();
            for (std::size_t i = 0; i < 3; i++) {
                for (std::size_t j = 0; j < 3; j++) {
                    hash = hash * 3 + std::hash<float>()(mat(i, j));
                }
            }
            return hash;
        }
    };
}

#endif
