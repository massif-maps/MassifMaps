#include "Symbolizer.h"
#include "FeatureCollection.h"
#include "Expression.h"
#include "Transform.h"
#include "TransformUtils.h"
#include "ParserUtils.h"
#include "SymbolizerContext.h"

#include <atomic>

namespace carto::mvt {
    std::set<std::string> Symbolizer::getParameterNames() const {
        std::set<std::string> paramNames;
        for (std::pair<std::string, SymbolizerParameter*> param : _parameterMap) {
            paramNames.insert(param.first);
        }
        return paramNames;
    }

    SymbolizerParameter* Symbolizer::getParameter(const std::string& paramName) {
        auto it = _parameterMap.find(paramName);
        if (it == _parameterMap.end()) {
            throw std::invalid_argument("Illegal parameter");
        }
        return it->second;
    }

    const SymbolizerParameter* Symbolizer::getParameter(const std::string& paramName) const {
        auto it = _parameterMap.find(paramName);
        if (it == _parameterMap.end()) {
            throw std::invalid_argument("Illegal parameter");
        }
        return it->second;
    }

    void Symbolizer::bindParameter(const std::string& paramName, SymbolizerParameter* param) {
        _parameterMap[paramName] = param;
    }

    void Symbolizer::unbindParameter(const std::string& paramName) {
        auto it = _parameterMap.find(paramName);
        if (it != _parameterMap.end()) {
            _parameterMap.erase(it);
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
