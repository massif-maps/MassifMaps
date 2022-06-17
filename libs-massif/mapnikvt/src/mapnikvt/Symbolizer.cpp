#include "Symbolizer.h"
#include "FeatureCollection.h"
#include "Expression.h"
#include "Transform.h"
#include "TransformUtils.h"
#include "ParserUtils.h"
#include "SymbolizerContext.h"

#include <atomic>

namespace carto::mvt {
    std::set<std::string> Symbolizer::getPropertyNames() const {
        std::set<std::string> names;
        for (std::pair<std::string, Property*> prop : _propertyMap) {
            names.insert(prop.first);
        }
        return names;
    }

    Property* Symbolizer::getProperty(const std::string& name) {
        auto it = _propertyMap.find(name);
        if (it == _propertyMap.end()) {
            throw std::invalid_argument("Illegal property");
        }
        return it->second;
    }

    const Property* Symbolizer::getProperty(const std::string& name) const {
        auto it = _propertyMap.find(name);
        if (it == _propertyMap.end()) {
            throw std::invalid_argument("Illegal property");
        }
        return it->second;
    }

    void Symbolizer::bindProperty(const std::string& name, Property* prop) {
        _propertyMap[name] = prop;
    }

    void Symbolizer::unbindProperty(const std::string& name) {
        auto it = _propertyMap.find(name);
        if (it != _propertyMap.end()) {
            _propertyMap.erase(it);
        }
    }

    long long Symbolizer::convertId(const Value& val) {
        struct IdHasher {
            long long operator() (std::monostate) const { return 0; }
            long long operator() (bool val) const { return (val ? 1 : 0); }
            long long operator() (long long val) const { return val; }
            long long operator() (double val) const { return (val != 0 ? std::hash<double>()(val) : 0); }
            long long operator() (const std::string& str) const { return (str.empty() ? 0 : std::hash<std::string>()(str)); }
        };

        long long id = std::visit(IdHasher(), val);
        return id & 0x3FFFFFFLL;
    }

    long long Symbolizer::generateId() {
        static std::atomic<int> counter = ATOMIC_VAR_INIT(0);
        return 0x4000000LL | counter++;
    }

    long long Symbolizer::combineId(long long id, std::size_t hash) {
        return std::abs(id ^ static_cast<long long>(hash));
    }
}
