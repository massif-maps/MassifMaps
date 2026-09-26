/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_SELECTIONPARAMETER_H_
#define _MASSIF_MAPNIKVT_SELECTIONPARAMETER_H_

#include "ExpressionPredicateBase.h"

#include <string>

namespace massif::mvt {
    /**
     * A style parameter compared with a feature field to pick one feature out ([param::selected_id] = [osmid] + '').
     * Folded both ways at decode, with each feature keeping the hash of fieldExpression, so a change is a
     * byte rewrite per vertex rather than a decode - see docs/features/style-parameters.md.
     */
    struct SelectionParameter {
        std::string name;            // the parameter, without the "param::" prefix
        Expression fieldExpression;  // the side of the comparison the feature answers
    };
}

#endif
