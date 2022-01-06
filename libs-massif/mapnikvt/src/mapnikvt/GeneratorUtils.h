/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_GENERATORUTILS_H_
#define _CARTO_MAPNIKVT_GENERATORUTILS_H_

#include "Value.h"
#include "Expression.h"
#include "vt/Color.h"
#include "vt/Styles.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace carto::mvt {
    class GeneratorException : public std::runtime_error {
    public:
        explicit GeneratorException(const std::string& msg) : runtime_error(msg) { }
    };

    std::string generateLineCapModeString(vt::LineCapMode lineCapMode);
    std::string generateLineJoinModeString(vt::LineJoinMode lineJoinMode);
    std::string generateCompOpString(vt::CompOp compOp);
    std::string generateLabelOrientationString(vt::LabelOrientation labelOrientation);
    std::string generateColorString(vt::Color color);
    std::string generateValueString(const Value& val);
    std::string generateExpressionString(const Expression& expr, bool stringExpr);
}

#endif
