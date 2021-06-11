#include "ParserUtils.h"
#include "ValueParser.h"
#include "ExpressionParser.h"
#include "TransformParser.h"
#include "ParseTables.h"
#include "ColorParser.h"
#include "StringUtils.h"

#include <sstream>
#include <utility>
#include <unordered_map>
#include <mutex>

#include <boost/algorithm/string.hpp>

namespace carto { namespace mvt {
    vt::LineCapMode parseLineCapMode(const std::string& str) {
        auto it = getLineCapModeTable().find(toLower(str));
        if (it == getLineCapModeTable().end()) {
            throw ParserException("LineCapMode parsing failed", str);
        }
        return it->second;
    }

    vt::LineJoinMode parseLineJoinMode(const std::string& str) {
        auto it = getLineJoinModeTable().find(toLower(str));
        if (it == getLineJoinModeTable().end()) {
            throw ParserException("LineJoinMode parsing failed", str);
        }
        return it->second;
    }

    vt::CompOp parseCompOp(const std::string& str) {
        auto it = getCompOpTable().find(toLower(str));
        if (it == getCompOpTable().end()) {
            throw ParserException("CompOp parsing failed", str);
        }
        return it->second;
    }

    vt::LabelOrientation parseLabelOrientation(const std::string& str) {
        auto it = getLabelOrientationTable().find(toLower(str));
        if (it == getLabelOrientationTable().end()) {
            throw ParserException("LabelOrientation parsing failed", str);
        }
        return it->second;
    }

    vt::Color parseColor(const std::string& str) {
        std::string::const_iterator it = str.begin();
        std::string::const_iterator end = str.end();
        unsigned int color = 0;
        bool result = false;
        try {
            static const ColorParserGrammar<std::string::const_iterator> parser;
            static const colorparserimpl::Skipper skipper;
            result = boost::spirit::qi::phrase_parse(it, end, parser, skipper, color);
        }
        catch (const boost::spirit::qi::expectation_failure<std::string::const_iterator>& ex) {
            throw ParserException("Expectation error, error at position " + std::to_string(ex.first - str.begin()), str);
        }
        if (!result) {
            throw ParserException("Color parsing failed", str);
        }
        if (it != str.end()) {
            throw ParserException("Could not parse to the end of color, error at position " + std::to_string(it - str.begin()), str);
        }
        return vt::Color(color);
    }

    Value parseValue(const std::string& str) {
        std::string::const_iterator it = str.begin();
        std::string::const_iterator end = str.end();
        Value val;
        bool result = false;
        try {
            static const ValueParserGrammar<std::string::const_iterator> parser;
            result = boost::spirit::qi::parse(it, end, parser, val);
        }
        catch (const boost::spirit::qi::expectation_failure<std::string::const_iterator>& ex) {
            throw ParserException("Expectation error, error at position " + std::to_string(ex.first - str.begin()), str);
        }
        if (!result) {
            throw ParserException("Value parsing failed", str);
        }
        if (it != str.end()) {
            throw ParserException("Could not parse to the end of value, error at position " + std::to_string(it - str.begin()), str);
        }
        return val;
    }

    std::vector<Transform> parseTransformList(const std::string& str) {
        std::string::const_iterator it = str.begin();
        std::string::const_iterator end = str.end();
        std::vector<Transform> transforms;
        bool result = false;
        try {
            static const TransformParserGrammar<std::string::const_iterator> parser;
            static const transparserimpl::Skipper skipper;
            result = boost::spirit::qi::phrase_parse(it, end, parser % ',', skipper, transforms);
        }
        catch (const boost::spirit::qi::expectation_failure<std::string::const_iterator>& ex) {
            throw ParserException("Expectation error, error at position " + std::to_string(ex.first - str.begin()), str);
        }
        if (!result) {
            throw ParserException("Transform parsing failed", str);
        }
        if (it != str.end()) {
            throw ParserException("Could not parse to the end of transform, error at position " + std::to_string(it - str.begin()), str);
        }
        return transforms;
    }

    Expression parseExpression(const std::string& str, bool stringExpr) {
        std::string::const_iterator it = str.begin();
        std::string::const_iterator end = str.end();
        Expression expr;
        bool result = false;
        try {
            if (stringExpr) {
                if (str.empty()) {
                    return Value(std::string());
                }
                static const StringExpressionParserGrammar<std::string::const_iterator> parser;
                result = boost::spirit::qi::parse(it, end, parser, expr);
            }
            else {
                static const ExpressionParserGrammar<std::string::const_iterator> parser;
                result = boost::spirit::qi::parse(it, end, parser, expr);
            }
        }
        catch (const boost::spirit::qi::expectation_failure<std::string::const_iterator>& ex) {
            throw ParserException("Expectation error, error at position " + std::to_string(ex.first - str.begin()), str);
        }
        if (!result) {
            throw ParserException("Expression parsing failed", str);
        }
        if (it != str.end()) {
            throw ParserException("Could not parse to the end of expression, error at position " + std::to_string(it - str.begin()), str);
        }
        return expr;
    }
} }
