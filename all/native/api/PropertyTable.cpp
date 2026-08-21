#include "api/PropertyTable.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>

// The class headers plus one thunk per accessor. At file scope, because it is #includes.
#include "api/PropertyAccessors.inc"

namespace massif { namespace api {

    namespace {
        // Both tables are emitted sorted by name, so every lookup is a binary search over static
        // data - no map, no allocation, nothing to build at load time.
        #include "api/PropertyTable.inc"

        template <typename Entry, typename NameOf>
        const Entry* findSorted(const Entry* entries, std::size_t count, const char* name, NameOf nameOf) {
            const Entry* end = entries + count;
            const Entry* it = std::lower_bound(entries, end, name, [&nameOf](const Entry& entry, const char* key) {
                return std::strcmp(nameOf(entry), key) < 0;
            });
            return it != end && std::strcmp(nameOf(*it), name) == 0 ? it : nullptr;
        }
    }

    PropertyValue PropertyValue::ofBool(bool v) {
        PropertyValue value; value.type = PT_BOOL; value.boolValue = v; return value;
    }

    PropertyValue PropertyValue::ofLong(long long v) {
        PropertyValue value; value.type = PT_INT; value.intValue = v; return value;
    }

    PropertyValue PropertyValue::ofDouble(double v) {
        PropertyValue value; value.type = PT_FLOAT; value.floatValue = v; return value;
    }

    PropertyValue PropertyValue::ofString(const std::string& v) {
        PropertyValue value; value.type = PT_STRING; value.stringValue = v; return value;
    }

    // Text coerces to a number too, because a binding with one string type - a C caller, a URL
    // query, a scripting language - would otherwise write 0 over a real value. Garbage reads as 0,
    // the same as every other unrepresentable conversion here.
    double PropertyValue::asDouble() const {
        switch (type) {
        case PT_BOOL:   return boolValue ? 1 : 0;
        case PT_FLOAT:  return floatValue;
        case PT_STRING: return std::strtod(stringValue.c_str(), nullptr);
        default:        return static_cast<double>(intValue);
        }
    }

    long long PropertyValue::asLong() const {
        switch (type) {
        case PT_BOOL:   return boolValue ? 1 : 0;
        case PT_FLOAT:  return static_cast<long long>(floatValue);
        case PT_STRING: return std::strtoll(stringValue.c_str(), nullptr, 0);
        default:        return intValue;
        }
    }

    bool PropertyValue::asBool() const {
        switch (type) {
        case PT_BOOL:   return boolValue;
        case PT_FLOAT:  return floatValue != 0;
        // "false" is not a number, so strtod would make it true. Spelled booleans are what an
        // intent extra and a JSON-ish caller actually send.
        case PT_STRING: return !(stringValue.empty() || stringValue == "0" ||
                                 stringValue == "false" || stringValue == "no");
        default:        return intValue != 0;
        }
    }

    std::string PropertyValue::asString() const {
        switch (type) {
        case PT_BOOL:   return boolValue ? "true" : "false";
        case PT_FLOAT: {
            std::ostringstream stream;
            stream.precision(17);
            stream << floatValue;
            return stream.str();
        }
        case PT_STRING:
        case PT_STRUCT:
        case PT_VARIANT: return stringValue;
        default:         return std::to_string(intValue);
        }
    }

    const ClassEntry* findClass(const char* cppClass) {
        if (!cppClass) {
            return nullptr;
        }
        return findSorted(kClasses, sizeof(kClasses) / sizeof(kClasses[0]), cppClass,
                          [](const ClassEntry& entry) { return entry.cppClass; });
    }

    const PropertyEntry* findProperty(const ClassEntry* classEntry, const char* path) {
        if (!path) {
            return nullptr;
        }
        for (; classEntry; classEntry = classEntry->base ? findClass(classEntry->base) : nullptr) {
            if (!classEntry->props) {
                continue;
            }
            const PropertyEntry* entry = findSorted(classEntry->props, classEntry->count, path,
                                                    [](const PropertyEntry& e) { return e.path; });
            if (entry) {
                return entry;
            }
        }
        return nullptr;
    }

    const PropertyEntry* findProjectionProperty(const ClassEntry* classEntry) {
        for (; classEntry; classEntry = classEntry->base ? findClass(classEntry->base) : nullptr) {
            for (std::size_t index = 0; index < classEntry->count; index++) {
                const PropertyEntry& entry = classEntry->props[index];
                if ((entry.flags & PF_PROJECTION) && entry.objectGetter) {
                    return &entry;
                }
            }
        }
        return nullptr;
    }

    bool isSubclassOf(const char* cppClass, const char* base) {
        if (!cppClass || !base) {
            return false;
        }
        for (const ClassEntry* entry = findClass(cppClass); entry;
             entry = entry->base ? findClass(entry->base) : nullptr) {
            if (std::strcmp(entry->cppClass, base) == 0) {
                return true;
            }
        }
        return false;
    }

    std::size_t getClassCount() {
        return sizeof(kClasses) / sizeof(kClasses[0]);
    }

    const ClassEntry* getClass(std::size_t index) {
        return index < getClassCount() ? &kClasses[index] : nullptr;
    }

} }
