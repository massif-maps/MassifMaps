#include "GeneratorUtils.h"
#include "ValueGenerator.h"
#include "ExpressionGenerator.h"
#include "ParseTables.h"
#include "ColorGenerator.h"

#include <iomanip>
#include <sstream>
#include <utility>

#include <boost/algorithm/string.hpp>

namespace carto::mvt {
    std::string generateLineCapModeString(vt::LineCapMode lineCapMode) {
        auto it = std::find_if(getLineCapModeTable().begin(), getLineCapModeTable().end(), [lineCapMode](const auto& keyVal) {
            return keyVal.second == lineCapMode;
        });
        if (it == getLineCapModeTable().end()) {
            throw GeneratorException("Could not generate LineCapMode string");
        }
        return it->first;
    }

    std::string generateLineJoinModeString(vt::LineJoinMode lineJoinMode) {
        auto it = std::find_if(getLineJoinModeTable().begin(), getLineJoinModeTable().end(), [lineJoinMode](const auto& keyVal) {
            return keyVal.second == lineJoinMode;
        });
        if (it == getLineJoinModeTable().end()) {
            throw GeneratorException("Could not generate LineJoinMode string");
        }
        return it->first;
    }

    std::string generateCompOpString(vt::CompOp compOp) {
        auto it = std::find_if(getCompOpTable().begin(), getCompOpTable().end(), [compOp](const auto& keyVal) {
            return keyVal.second == compOp;
        });
        if (it == getCompOpTable().end()) {
            throw GeneratorException("Could not generate CompOp string");
        }
        return it->first;
    }

    std::string generateLabelOrientationString(vt::LabelOrientation labelOrientation) {
        auto it = std::find_if(getLabelOrientationTable().begin(), getLabelOrientationTable().end(), [labelOrientation](const auto& keyVal) {
            return keyVal.second == labelOrientation;
        });
        if (it == getLabelOrientationTable().end()) {
            throw GeneratorException("Could not generate LabelOrientation string");
        }
        return it->first;
    }

    std::string generateColorString(vt::Color color) {
        std::string str;
        std::back_insert_iterator<std::string> it(str);
        bool result = boost::spirit::karma::generate(it, ColorGeneratorGrammar<std::back_insert_iterator<std::string>>(), color.value());
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

    std::string generateExpressionString(const Expression& expr, bool stringExpr) {
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
}
