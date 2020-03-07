#include "Symbolizer.h"
#include "FeatureCollection.h"
#include "Expression.h"
#include "ExpressionBinder.h"
#include "Transform.h"
#include "ParserUtils.h"
#include "SymbolizerContext.h"

#include <atomic>
#include <unordered_map>

namespace carto { namespace mvt {
    void Symbolizer::setParameter(const std::string& name, const std::string& value) {
        _parameterMap[name] = value;
        bindParameter(name, value);
    }
    
    const std::map<std::string, std::string>& Symbolizer::getParameterMap() const {
        return _parameterMap;
    }

    const std::vector<std::shared_ptr<const Expression>>& Symbolizer::getParameterExpressions() const {
        return _parameterExprs;
    }

    std::shared_ptr<Expression> Symbolizer::parseExpression(const std::string& str) const {
        static std::mutex exprCacheMutex;
        static std::unordered_map<std::string, std::shared_ptr<Expression>> exprCache;

        constexpr static int MAX_CACHE_SIZE = 1024;

        std::lock_guard<std::mutex> lock(exprCacheMutex);
        auto exprIt = exprCache.find(str);
        if (exprIt != exprCache.end()) {
            return exprIt->second;
        }
        std::shared_ptr<Expression> expr = mvt::parseExpression(str, false);
        if (exprCache.size() >= MAX_CACHE_SIZE) {
            exprCache.erase(exprCache.begin());
        }
        exprCache.emplace(str, expr);
        return expr;
    }

    std::shared_ptr<Expression> Symbolizer::parseStringExpression(const std::string& str) const {
        static std::mutex exprCacheMutex;
        static std::unordered_map<std::string, std::shared_ptr<Expression>> exprCache;

        constexpr static int MAX_CACHE_SIZE = 1024;

        std::lock_guard<std::mutex> lock(exprCacheMutex);
        auto exprIt = exprCache.find(str);
        if (exprIt != exprCache.end()) {
            return exprIt->second;
        }
        std::shared_ptr<Expression> expr = mvt::parseExpression(str, true);
        if (exprCache.size() >= MAX_CACHE_SIZE) {
            exprCache.erase(exprCache.begin());
        }
        exprCache.emplace(str, expr);
        return expr;
    }

    vt::CompOp Symbolizer::convertCompOp(const std::string& compOp) const {
        try {
            return parseCompOp(compOp);
        }
        catch (const ParserException& ex) {
            _logger->write(Logger::Severity::ERROR, ex.what());
            return vt::CompOp::SRC_OVER;
        }
    }

    vt::LabelOrientation Symbolizer::convertLabelPlacement(const std::string& orientation) const {
        try {
            return parseLabelOrientation(orientation);
        }
        catch (const ParserException& ex) {
            _logger->write(Logger::Severity::ERROR, ex.what());
            return vt::LabelOrientation::LINE;
        }
    }

    long long Symbolizer::convertId(const Value& val) const {
        if (auto integer = boost::get<long long>(&val)) {
            return *integer;
        }
        
        std::string str = boost::lexical_cast<std::string>(val);
        long long hash = 1125899906842597LL;
        for (char c : str) {
            hash = 31 * hash + static_cast<unsigned char>(c);
        }
        return std::abs(hash);
    }

    vt::Color Symbolizer::convertColor(const Value& val) const {
        try {
            return parseColor(boost::lexical_cast<std::string>(val));
        }
        catch (const ParserException& ex) {
            _logger->write(Logger::Severity::ERROR, ex.what());
            return vt::Color();
        }
    }

    cglib::mat3x3<float> Symbolizer::convertTransform(const Value& val) const {
        try {
            std::vector<std::shared_ptr<Transform>> transforms = parseTransformList(boost::lexical_cast<std::string>(val));
            cglib::mat3x3<float> matrix = cglib::mat3x3<float>::identity();
            for (const std::shared_ptr<Transform>& transform : transforms) {
                matrix = matrix * transform->getMatrix();
            }
            return matrix;
        }
        catch (const ParserException& ex) {
            _logger->write(Logger::Severity::ERROR, ex.what());
            return cglib::mat3x3<float>::identity();
        }
    }

    boost::optional<cglib::mat3x3<float>> Symbolizer::convertOptionalTransform(const Value& val) const {
        return convertTransform(val);
    }

    void Symbolizer::bindParameter(const std::string& name, const std::string& value) {
        _logger->write(Logger::Severity::WARNING, "Unsupported symbolizer parameter: " + name);
    }

    long long Symbolizer::generateId() {
        static std::atomic<int> counter = ATOMIC_VAR_INIT(0);
        return 0x4000000LL | counter++;
    }

    long long Symbolizer::combineId(long long id, std::size_t hash) {
        return std::abs(id ^ static_cast<long long>(hash));
    }
} }
