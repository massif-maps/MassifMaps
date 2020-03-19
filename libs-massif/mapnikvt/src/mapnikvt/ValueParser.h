/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_VALUEPARSER_H_
#define _CARTO_MAPNIKVT_VALUEPARSER_H_

#include "Value.h"

#include <memory>
#include <functional>

#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/phoenix.hpp>
#include <boost/spirit/include/phoenix_core.hpp>
#include <boost/spirit/include/phoenix_bind.hpp>
#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/repository/include/qi_distinct.hpp>

namespace carto { namespace mvt {
    namespace valparserimpl {
        template <typename Iterator>
        struct Grammar : boost::spirit::qi::grammar<Iterator, Value()> {
            Grammar() : Grammar::base_type(value) {
                using namespace boost;
                using namespace boost::spirit;
                using qi::_val;
                using qi::_1;

                unesc_char.add("\\a", '\a')("\\b", '\b')("\\f", '\f')("\\n", '\n')
                              ("\\r", '\r')("\\t", '\t')("\\v", '\v')("\\\\", '\\')
                              ("\\\'", '\'')("\\\"", '\"');

                null_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["null"]];
                point_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["point"]];
                linestring_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["linestring"]];
                polygon_kw = repository::qi::distinct(qi::char_("a-zA-Z0-9_"))[qi::no_case["polygon"]];

                string =
                      '\'' >> *(unesc_char | "\\x" >> octet_ | (qi::char_ - '\'')) >> '\''
                    | '\"' >> *(unesc_char | "\\x" >> octet_ | (qi::char_ - '\"')) >> '\"';

                value =
                      null_kw                [_val = phoenix::construct<Value>()]
                    | point_kw               [_val = phoenix::construct<Value>(1LL)]
                    | linestring_kw          [_val = phoenix::construct<Value>(2LL)]
                    | polygon_kw             [_val = phoenix::construct<Value>(3LL)]
                    | qi::bool_              [_val = phoenix::construct<Value>(_1)]
                    | qi::real_parser<double, qi::strict_real_policies<double>>() [_val = phoenix::construct<Value>(_1)]
                    | qi::long_long          [_val = phoenix::construct<Value>(_1)]
                    | string                 [_val = phoenix::construct<Value>(_1)]
                    ;
            }

            boost::spirit::qi::int_parser<char, 16, 2, 2> octet_;
            boost::spirit::qi::symbols<char const, char const> unesc_char;
            boost::spirit::qi::rule<Iterator, boost::spirit::qi::unused_type()> null_kw, point_kw, linestring_kw, polygon_kw;
            boost::spirit::qi::rule<Iterator, std::string()> string;
            boost::spirit::qi::rule<Iterator, Value()> value;
        };
    }

    template <typename Iterator> using ValueParserGrammar = valparserimpl::Grammar<Iterator>;
} }

#endif
