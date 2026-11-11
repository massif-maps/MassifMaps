/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_DEBUGSWITCH_H_
#define _MASSIF_DEBUGSWITCH_H_

#include <cstdlib>
#include <string>

#if defined(__ANDROID__) && MASSIF_DEBUG_PROPERTIES
#include <sys/system_properties.h>
#endif

namespace massif {
    /**
     * Measurement switches: Android property `debug.massif.<name>` (`adb shell setprop`), read in demo
     * builds only (MASSIF_DEBUG_PROPERTIES). Everywhere else every call returns its default, so a call
     * site needs no #if. Callers cache the result in a function-local static.
     */
    namespace DebugSwitch {
        /** The property's value, empty when unset or when properties are compiled out. */
        inline std::string text(const char* name) {
#if defined(__ANDROID__) && MASSIF_DEBUG_PROPERTIES
            char value[PROP_VALUE_MAX] = { 0 };
            std::string key = std::string("debug.massif.") + name;
            if (__system_property_get(key.c_str(), value) > 0) {
                return value;
            }
#endif
            return std::string();
        }

        /** '0' is off, '1' is on, anything else (including unset) the default. */
        inline bool flag(const char* name, bool defaultValue) {
            std::string value = text(name);
            if (value.empty() || (value[0] != '0' && value[0] != '1')) {
                return defaultValue;
            }
            return value[0] == '1';
        }

        /** The value when it parses and is >= min (> min when !inclusive), else the default. */
        inline double number(const char* name, double defaultValue, double min = 0.0, bool inclusive = true) {
            std::string value = text(name);
            if (value.empty()) {
                return defaultValue;
            }
            double parsed = std::atof(value.c_str());
            return (inclusive ? parsed >= min : parsed > min) ? parsed : defaultValue;
        }
    }
}

#endif
