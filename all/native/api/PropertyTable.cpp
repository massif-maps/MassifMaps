#include "api/PropertyTable.h"

#include <algorithm>
#include <cstring>

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

    const ClassEntry* findClass(const char* cppClass) {
        if (!cppClass) {
            return nullptr;
        }
        return findSorted(kClasses, sizeof(kClasses) / sizeof(kClasses[0]), cppClass,
                          [](const ClassEntry& entry) { return entry.cppClass; });
    }

    const PropertyEntry* findProperty(const ClassEntry* classEntry, const char* path) {
        if (!classEntry || !path) {
            return nullptr;
        }
        return findSorted(classEntry->props, classEntry->count, path,
                          [](const PropertyEntry& entry) { return entry.path; });
    }

    std::size_t getClassCount() {
        return sizeof(kClasses) / sizeof(kClasses[0]);
    }

    const ClassEntry* getClass(std::size_t index) {
        return index < getClassCount() ? &kClasses[index] : nullptr;
    }

} }
