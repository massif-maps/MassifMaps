/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_CARTOCSS_CARTOCSSPARSER_H_
#define _CARTO_CARTOCSS_CARTOCSSPARSER_H_

#include "Expression.h"
#include "Predicate.h"
#include "StyleSheet.h"

#include <memory>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <utility>
#include <stdexcept>

#include <boost/lexical_cast.hpp>

namespace carto { namespace css {
    class CartoCSSParser final {
    public:
        class ParserError : public std::runtime_error {
        public:
            explicit ParserError(const std::string& msg) : runtime_error(msg), _message(msg), _position(0, 0) { }
            explicit ParserError(const std::string& msg, std::pair<int, int> pos) : runtime_error(msg + ", error at line " + boost::lexical_cast<std::string>(pos.second) + ", column " + boost::lexical_cast<std::string>(pos.first)), _message(msg), _position(pos) { }

            const std::string& message() const { return _message; }
            std::pair<int, int> position() const { return _position; }
        
        private:
            std::string _message;
            std::pair<int, int> _position;
        };
        
        static StyleSheet parse(const std::string& cartoCSS);
        
    private:
        static std::pair<int, int> resolvePosition(const std::string& str, std::string::size_type pos);
    };
} }

#endif
