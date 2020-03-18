#include "ParserUtils.h"
#include "ValueParser.h"
#include "ExpressionParser.h"
#include "TransformParser.h"
#include "ColorParser.h"

#include <sstream>
#include <utility>
#include <unordered_map>
#include <mutex>

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

namespace carto { namespace mvt {
    vt::CompOp parseCompOp(const std::string& str) {
        static const std::unordered_map<std::string, vt::CompOp> compOpTable = {
            { "src",      vt::CompOp::SRC },
            { "src-over", vt::CompOp::SRC_OVER },
            { "src-in",   vt::CompOp::SRC_IN },
            { "src-atop", vt::CompOp::SRC_ATOP },
            { "dst",      vt::CompOp::DST },
            { "dst-over", vt::CompOp::DST_OVER },
            { "dst-in",   vt::CompOp::DST_IN },
            { "dst-atop", vt::CompOp::DST_ATOP },
            { "clear",    vt::CompOp::ZERO },
            { "zero",     vt::CompOp::ZERO },
            { "plus",     vt::CompOp::PLUS },
            { "minus",    vt::CompOp::MINUS },
            { "multiply", vt::CompOp::MULTIPLY },
            { "screen",   vt::CompOp::SCREEN },
            { "darken",   vt::CompOp::DARKEN },
            { "lighten",  vt::CompOp::LIGHTEN }
        };

        auto it = compOpTable.find(str);
        if (it == compOpTable.end()) {
            throw ParserException("CompOp parsing failed", str);
        }
        return it->second;
    }

    vt::LabelOrientation parseLabelOrientation(const std::string& str) {
        static const std::unordered_map<std::string, vt::LabelOrientation> labelOrientationTable = {
            { "point",         vt::LabelOrientation::BILLBOARD_2D },
            { "nutibillboard", vt::LabelOrientation::BILLBOARD_3D },
            { "nutipoint",     vt::LabelOrientation::POINT },
            { "line",          vt::LabelOrientation::LINE }
        };

        auto it = labelOrientationTable.find(str);
        if (it == labelOrientationTable.end()) {
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
            colorparserimpl::Skipper skipper;
            result = boost::spirit::qi::phrase_parse(it, end, ColorParserGrammar<std::string::const_iterator>(), skipper, color);
        }
        catch (const boost::spirit::qi::expectation_failure<std::string::const_iterator>& ex) {
            throw ParserException("Expectation error, error at position " + boost::lexical_cast<std::string>(ex.first - str.begin()), str);
        }
        if (!result) {
            throw ParserException("Color parsing failed", str);
        }
        if (it != str.end()) {
            throw ParserException("Could not parse to the end of color, error at position " + boost::lexical_cast<std::string>(it - str.begin()), str);
        }
        return vt::Color(color);
    }

    Value parseValue(const std::string& str) {
        std::string::const_iterator it = str.begin();
        std::string::const_iterator end = str.end();
        Value val;
        bool result = false;
        try {
            result = boost::spirit::qi::parse(it, end, ValueParserGrammar<std::string::const_iterator>(), val);
        }
        catch (const boost::spirit::qi::expectation_failure<std::string::const_iterator>& ex) {
            throw ParserException("Expectation error, error at position " + boost::lexical_cast<std::string>(ex.first - str.begin()), str);
        }
        if (!result) {
            throw ParserException("Value parsing failed", str);
        }
        if (it != str.end()) {
            throw ParserException("Could not parse to the end of value, error at position " + boost::lexical_cast<std::string>(it - str.begin()), str);
        }
        return val;
    }

    std::vector<std::shared_ptr<Transform> > parseTransformList(const std::string& str) {
        std::string::const_iterator it = str.begin();
        std::string::const_iterator end = str.end();
        std::vector<std::shared_ptr<Transform>> transforms;
        bool result = false;
        try {
            transparserimpl::Skipper skipper;
            result = boost::spirit::qi::phrase_parse(it, end, TransformParserGrammar<std::string::const_iterator>() % ',', skipper, transforms);
        }
        catch (const boost::spirit::qi::expectation_failure<std::string::const_iterator>& ex) {
            throw ParserException("Expectation error, error at position " + boost::lexical_cast<std::string>(ex.first - str.begin()), str);
        }
        if (!result) {
            throw ParserException("Transform parsing failed", str);
        }
        if (it != str.end()) {
            throw ParserException("Could not parse to the end of transform, error at position " + boost::lexical_cast<std::string>(it - str.begin()), str);
        }
        return transforms;
    }

    std::shared_ptr<Expression> parseExpression(const std::string& str, bool stringExpr) {
        std::string::const_iterator it = str.begin();
        std::string::const_iterator end = str.end();
        std::shared_ptr<Expression> expr;
        bool result = false;
        try {
            if (stringExpr) {
                result = boost::spirit::qi::parse(it, end, StringExpressionParserGrammar<std::string::const_iterator>(), expr);
            }
            else {
                result = boost::spirit::qi::parse(it, end, ExpressionParserGrammar<std::string::const_iterator>(), expr);
            }
        }
        catch (const boost::spirit::qi::expectation_failure<std::string::const_iterator>& ex) {
            throw ParserException("Expectation error, error at position " + boost::lexical_cast<std::string>(ex.first - str.begin()), str);
        }
        if (!result) {
            throw ParserException("Expression parsing failed", str);
        }
        if (it != str.end()) {
            throw ParserException("Could not parse to the end of expression, error at position " + boost::lexical_cast<std::string>(it - str.begin()), str);
        }
        return expr;
    }
} }
