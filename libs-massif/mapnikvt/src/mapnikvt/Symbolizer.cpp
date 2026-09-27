#include "Symbolizer.h"
#include "FeatureCollection.h"
#include "Expression.h"
#include "Transform.h"
#include "TransformUtils.h"
#include "ParserUtils.h"
#include "SymbolizerContext.h"

#include <atomic>

namespace massif::mvt {
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
    const bool Symbolizer::hasProperties() const {
        return _propertyMap.size() > 0;
    }

    bool Symbolizer::isPropertyDefined(const std::string& name) const {
        auto it = _propertyMap.find(name);
        return it != _propertyMap.end() && it->second->isDefined();
    }

    const Property* Symbolizer::getProperty(const std::string& name) const {
        auto it = _propertyMap.find(name);
        if (it == _propertyMap.end()) {
            throw std::invalid_argument("Illegal property");
        }
        return it->second;
    }

    void Symbolizer::bindProperty(const std::string& name, Property* prop, bool bakedAtDecode) {
        if (prop) { // a null property registers a name the symbolizer accepts and ignores
            prop->setBakedAtDecode(bakedAtDecode);
        }
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
            long long operator() (const std::shared_ptr<const ValueArray>& val) const { return (val ? std::hash<const void*>()(val.get()) : 0); }
            long long operator() (const std::shared_ptr<const ValueObject>& val) const { return (val ? std::hash<const void*>()(val.get()) : 0); }
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

    long long Symbolizer::combineAnchorId(long long id, const vt::TileId& tileId, const cglib::vec2<float>& vertex) {
        // 2^-28 of the world, ~0.15 m at the equator: below any two POIs worth telling apart, and
        // far above the tile grid's own rounding, so overzoom levels of one point agree on a cell.
        constexpr double ANCHOR_RESOLUTION = 268435456.0;

        double scale = ANCHOR_RESOLUTION / (1 << tileId.zoom);
        long long x = std::llround((tileId.x + static_cast<double>(vertex(0))) * scale);
        long long y = std::llround((tileId.y + static_cast<double>(vertex(1))) * scale);
        return combineId(id, std::hash<long long>()(x) * 63 + std::hash<long long>()(y));
    }
}
