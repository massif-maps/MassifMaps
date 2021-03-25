#include "Symbolizer.h"
#include "FeatureCollection.h"
#include "Expression.h"
#include "ExpressionBinder.h"
#include "Transform.h"
#include "TransformUtils.h"
#include "ParserUtils.h"
#include "SymbolizerContext.h"

#include <atomic>
#include <functional>
#include <unordered_map>

namespace carto { namespace mvt {
    void Symbolizer::setParameter(const std::string& name, const std::string& value) {
        _parameterMap[name] = value;
        bindParameter(name, value);
    }
    
    const std::map<std::string, std::string>& Symbolizer::getParameterMap() const {
        return _parameterMap;
    }

    const std::vector<Expression>& Symbolizer::getParameterExpressions() const {
        return _parameterExprs;
    }

    Expression Symbolizer::parseExpression(const std::string& str) const {
        static std::mutex exprCacheMutex;
        static std::unordered_map<std::string, Expression> exprCache;
        static constexpr int MAX_CACHE_SIZE = 1024;

        std::lock_guard<std::mutex> lock(exprCacheMutex);
        auto exprIt = exprCache.find(str);
        if (exprIt != exprCache.end()) {
            return exprIt->second;
        }
        Expression expr;
        try {
            expr = mvt::parseExpression(str, false);
        }
        catch (const ParserException& ex) {
            try {
                expr = mvt::parseExpression(str, true);
            }
            catch (const ParserException&) {
                throw ex;
            }
            _logger->write(Logger::Severity::WARNING, "Internal issue: property marked as non-string property, but parsed as string property: " + str);
        }
        if (exprCache.size() >= MAX_CACHE_SIZE) {
            exprCache.erase(exprCache.begin());
        }
        exprCache.emplace(str, expr);
        return expr;
    }

    Expression Symbolizer::parseStringExpression(const std::string& str) const {
        static std::mutex exprCacheMutex;
        static std::unordered_map<std::string, Expression> exprCache;
        constexpr static int MAX_CACHE_SIZE = 1024;

        std::lock_guard<std::mutex> lock(exprCacheMutex);
        auto exprIt = exprCache.find(str);
        if (exprIt != exprCache.end()) {
            return exprIt->second;
        }
        Expression expr = mvt::parseExpression(str, true);
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

    vt::Color Symbolizer::convertColor(const Value& val) const {
        try {
            return parseColor(ValueConverter<std::string>::convert(val));
        }
        catch (const ParserException& ex) {
            _logger->write(Logger::Severity::ERROR, ex.what());
            return vt::Color();
        }
    }

    vt::Transform Symbolizer::convertTransform(const Value& val) const {
        try {
            std::vector<Transform> transforms = parseTransformList(ValueConverter<std::string>::convert(val));
            cglib::mat3x3<float> matrix = cglib::mat3x3<float>::identity();
            for (const Transform& transform : transforms) {
                matrix = matrix * std::visit(TransformEvaluator(), transform);
            }
            return vt::Transform::fromMatrix3(matrix);
        }
        catch (const ParserException& ex) {
            _logger->write(Logger::Severity::ERROR, ex.what());
            return vt::Transform();
        }
    }

    vt::CompOp Symbolizer::getCompOp() const {
        return convertCompOp(_compOp);
    }

    void Symbolizer::bindParameter(const std::string& name, const std::string& value) {
        if (name == "comp-op") {
            bind(&_compOp, parseStringExpression(value));
        }
        else {
            _logger->write(Logger::Severity::WARNING, "Unsupported symbolizer parameter: " + name);
        }
    }

    long long Symbolizer::convertId(const Value& val) {
        struct NumericIdHasher {
            long long operator() (std::monostate) const { return 0; }
            long long operator() (bool val) const { return (val ? 1 : 0); }
            long long operator() (long long val) const { return val; }
            long long operator() (double val) const { return (val != 0 ? std::hash<double>()(val) : 0); }
            long long operator() (const std::string& str) const { return (str.empty() ? 0 : std::hash<std::string>()(str)); }
        };

        long long id = std::visit(NumericIdHasher(), val);
        return id & 0x3FFFFFFLL;
    }

    long long Symbolizer::generateId() {
        static std::atomic<int> counter = ATOMIC_VAR_INIT(0);
        return 0x4000000LL | counter++;
    }

    long long Symbolizer::combineId(long long id, std::size_t hash) {
        return std::abs(id ^ static_cast<long long>(hash));
    }
} }
