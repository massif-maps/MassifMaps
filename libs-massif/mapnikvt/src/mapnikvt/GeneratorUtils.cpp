#include "GeneratorUtils.h"
#include "ValueGenerator.h"
#include "ExpressionGenerator.h"
#include "TransformGenerator.h"
#include "ColorGenerator.h"

#include <iomanip>
#include <sstream>
#include <utility>

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

namespace carto { namespace mvt {
    std::string generateColorString(vt::Color color) {
        std::string str;
        std::back_insert_iterator<std::string> it(str);
        colorgenimpl::Delimiter delimiter;
        bool result = boost::spirit::karma::generate_delimited(it, ColorGeneratorGrammar<std::back_insert_iterator<std::string>>(), delimiter, color.value());
        if (!result) {
            throw GeneratorException("Could not generate color string");
        }
        return boost::trim_copy(str);
    }

    std::string generateValueString(const Value& val) {
        std::string str;
        std::back_insert_iterator<std::string> it(str);
        bool result = boost::spirit::karma::generate(it, ValueGeneratorGrammar<std::back_insert_iterator<std::string>>(), val);
        if (!result) {
            throw GeneratorException("Could not generate value string");
        }
        return boost::trim_copy(str);
    }

    std::string generateTransformListString(const std::vector<std::shared_ptr<const Transform>>& transforms) {
        std::string str;
        std::back_insert_iterator<std::string> it(str);
        transgenimpl::Delimiter delimiter;
        bool result = boost::spirit::karma::generate_delimited(it, TransformGeneratorGrammar<std::back_insert_iterator<std::string>>() % ',', delimiter, transforms);
        if (!result) {
            throw GeneratorException("Could not generate transform string");
        }
        return boost::trim_copy(str);
    }

    std::string generateExpressionString(const std::shared_ptr<const Expression>& expr, bool stringExpr) {
        std::string str;
        std::back_insert_iterator<std::string> it(str);
        bool result = false;
        if (stringExpr) {
            result = boost::spirit::karma::generate(it, StringExpressionGeneratorGrammar<std::back_insert_iterator<std::string>>(), expr);
        }
        else {
            result = boost::spirit::karma::generate(it, ExpressionGeneratorGrammar<std::back_insert_iterator<std::string>>(), expr);
        }
        if (!result) {
            throw GeneratorException("Could not generate expression string");
        }
        return boost::trim_copy(str);
    }
} }
