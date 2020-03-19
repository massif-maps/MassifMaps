/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_TRANSFORMPARSER_H_
#define _CARTO_MAPNIKVT_TRANSFORMPARSER_H_

#include "Transform.h"

#include <memory>
#include <functional>
#include <typeinfo>

#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/phoenix.hpp>
#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_bind.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/repository/include/qi_distinct.hpp>

namespace carto { namespace mvt {
    namespace transparserimpl {
        using Skipper = boost::spirit::iso8859_1::space_type;

        template <typename Iterator>
        struct Grammar : boost::spirit::qi::grammar<Iterator, std::shared_ptr<Transform>(), Skipper> {
            Grammar() : Grammar::base_type(transform) {
                using namespace boost;
                using namespace boost::spirit;
                using qi::_val;
                using qi::_1;
                using qi::_2;
                using qi::_3;
                using qi::_4;
                using qi::_5;
                using qi::_6;

                matrix_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["matrix"]];
                translate_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["translate"]];
                rotate_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["rotate"]];
                scale_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["scale"]];
                skewx_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["skewx"]];
                skewy_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["skewy"]];

                number %= qi::float_;

                transform %=
                      matrix
                    | translate
                    | rotate
                    | scale
                    | skewx
                    | skewy
                    ;

                matrix =
                    (matrix_kw >> ('(' > number >> ',' > number > ',' > number > ',' > number > ',' > number > ',' > number > ')')) [_val = phoenix::bind(&makeMatrixTransform, _1, _2, _3, _4, _5, _6)]
                    ;

                translate =
                    (translate_kw >> ('(' > number > ',' > number > ')')) [_val = phoenix::bind(&makeTranslateTransform, _1, _2)]
                    ;

                rotate =
                      (rotate_kw >> '(' >> number >> ')') [_val = phoenix::bind(&makeRotateTransform, 0, 0, _1)]
                    | (rotate_kw >> ('(' > number > ',' > number > ',' > number > ')')) [_val = phoenix::bind(&makeRotateTransform, _2, _3, _1)]
                    ;

                scale =
                      (scale_kw >> '(' >> number >> ')') [_val = phoenix::bind(&makeScaleTransform, _1, _1)]
                    | (scale_kw >> ('(' > number > ',' > number > ')')) [_val = phoenix::bind(&makeScaleTransform, _1, _2)]
                    ;

                skewx =
                    (skewx_kw >> ('(' > number > ')')) [_val = phoenix::bind(&makeSkewTransform<SkewXTransform>, _1)]
                    ;

                skewy =
                    (skewy_kw >> ('(' > number > ')')) [_val = phoenix::bind(&makeSkewTransform<SkewYTransform>, _1)]
                    ;
            }

            boost::spirit::qi::rule<Iterator, boost::spirit::qi::unused_type()> matrix_kw, translate_kw, rotate_kw, scale_kw, skewx_kw, skewy_kw;
            boost::spirit::qi::rule<Iterator, float()> number;
            boost::spirit::qi::rule<Iterator, std::shared_ptr<Transform>(), Skipper> transform, matrix, translate, rotate, scale, skewx, skewy;

        private:
            static std::shared_ptr<Transform> makeMatrixTransform(float a, float b, float c, float d, float e, float f) {
                cglib::mat3x3<float> m = cglib::mat3x3<float>::identity();
                m(0, 0) = a;
                m(1, 0) = b;
                m(0, 1) = c;
                m(1, 1) = d;
                m(0, 2) = e;
                m(1, 2) = f;
                return std::make_shared<MatrixTransform>(m);
            }
            
            static std::shared_ptr<Transform> makeTranslateTransform(float x, float y) {
                return std::make_shared<TranslateTransform>(cglib::vec2<float>(x, y));
            }

            static std::shared_ptr<Transform> makeRotateTransform(float x, float y, float angle) {
                return std::make_shared<RotateTransform>(cglib::vec2<float>(x, y), angle);
            }

            static std::shared_ptr<Transform> makeScaleTransform(float sx, float sy) {
                return std::make_shared<ScaleTransform>(cglib::vec2<float>(sx, sy));
            }

            template <typename SkewTransform>
            static std::shared_ptr<Transform> makeSkewTransform(float angle) {
                return std::make_shared<SkewTransform>(angle);
            }
        };
    }

    template <typename Iterator> using TransformParserGrammar = transparserimpl::Grammar<Iterator>;
} }

#endif
