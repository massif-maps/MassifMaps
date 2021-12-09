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

#include <boost/phoenix.hpp>
#include <boost/spirit/include/karma.hpp>

namespace carto { namespace mvt {
    namespace valgenimpl {
        template <typename OutputIterator>
        struct Grammar : boost::spirit::karma::grammar<OutputIterator, Value()> {
            Grammar() : Grammar::base_type(value) {
                using namespace boost;
                using namespace boost::spirit;
                using karma::_pass;
                using karma::_val;
                using karma::_1;

                esc_char.add('\a', "\\a")('\b', "\\b")('\f', "\\f")('\n', "\\n")
                            ('\r', "\\r")('\t', "\\t")('\v', "\\v")('\\', "\\\\")
                            ('\'', "\\\'")('"', "\\\"");

                string %= '\'' << *(esc_char | karma::print | ("\\x0" << octet) [_pass = _1 >= 0x00 && _1 <= 0x0f] | "\\x" << octet) << '\'';

                value =
                      karma::lit("null")        [_pass = phoenix::bind(&isNullValue, _val)]
                    | karma::bool_              [_pass = phoenix::bind(&getBoolValue, _val, _1)]
                    | karma::long_long          [_pass = phoenix::bind(&getLongValue, _val, _1)]
                    | karma::double_            [_pass = phoenix::bind(&getDoubleValue, _val, _1)]
                    | string                    [_pass = phoenix::bind(&getStringValue, _val, _1)]
                    ;
            }

            boost::spirit::karma::uint_generator<unsigned char, 16> octet;
            boost::spirit::karma::symbols<char, const char*> esc_char;
            boost::spirit::karma::rule<OutputIterator, std::string()> string;
            boost::spirit::karma::rule<OutputIterator, Value()> value;

        private:
            static bool isNullValue(const Value& val) {
                return std::holds_alternative<std::monostate>(val);
            }

            static bool getBoolValue(const Value& val, bool& result) {
                if (auto boolVal = std::get_if<bool>(&val)) {
                    result = *boolVal;
                    return true;
                }
                return false;
            }

            static bool getLongValue(const Value& val, long long& result) {
                if (auto longVal = std::get_if<long long>(&val)) {
                    result = *longVal;
                    return true;
                }
                return false;
            }

            static bool getDoubleValue(const Value& val, double& result) {
                if (auto doubleVal = std::get_if<double>(&val)) {
                    result = *doubleVal;
                    return true;
                }
                return false;
            }

            static bool getStringValue(const Value& val, std::string& result) {
                if (auto strVal = std::get_if<std::string>(&val)) {
                    result = *strVal;
                    return true;
                }
                return false;
            }
        };
    }

    template <typename Iterator> using ValueGeneratorGrammar = valgenimpl::Grammar<Iterator>;
} }

#endif
