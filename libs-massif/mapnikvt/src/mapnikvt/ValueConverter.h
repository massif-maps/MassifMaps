/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_VALUECONVERTER_H_
#define _CARTO_MAPNIKVT_VALUECONVERTER_H_

#include "Value.h"

#include <boost/lexical_cast.hpp>

namespace carto { namespace mvt {
    template <typename V>
    struct ValueConverter {
        static V convert(const Value& val) {
            return std::visit(Converter(), val);
        }

    private:
        struct Converter {
            V operator() (std::monostate) const { return V(); }
            V operator() (const std::string& val) const {
                if (val.empty()) {
                    return V();
                }
                try {
                    return boost::lexical_cast<V>(val);
                }
                catch (const boost::bad_lexical_cast&) {
                    return V();
                }
            }
            template <typename T> V operator() (T val) const { return static_cast<V>(val); }
        };
    };

    template <>
    struct ValueConverter<std::monostate> {
        static std::monostate convert(const Value& val) {
            return std::monostate();
        }
    };

    template <>
    struct ValueConverter<bool> {
        static bool convert(const Value& val) {
            return std::visit(Converter(), val);
        }

    private:
        struct Converter {
            bool operator() (std::monostate) const { return false; }
            bool operator() (bool val) const { return val; }
            bool operator() (const std::string& val) const {
                if (val.empty()) {
                    return false;
                } else if (val == "true") {
                    return true;
                } else if (val == "false") {
                    return false;
                }
                try {
                    return boost::lexical_cast<bool>(val);
                }
                catch (const boost::bad_lexical_cast&) {
                    return false;
                }
            }
            template <typename T> bool operator() (T val) const { return val != 0; }
        };
    };

    template <>
    struct ValueConverter<std::string> {
        static std::string convert(const Value& val) {
            return std::visit(Converter(), val);
        }

    private:
        struct Converter {
            std::string operator() (std::monostate) const { return std::string(); }
            std::string operator() (const std::string& val) const { return val; }
            template <typename T> std::string operator() (T val) const {
                try {
                    return boost::lexical_cast<std::string>(val);
                }
                catch (const boost::bad_lexical_cast&) {
                    return std::string();
                }
            }
        };
    };
} }

#endif
