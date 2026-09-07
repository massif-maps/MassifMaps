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
     * A style parameter that picks ONE feature out by comparing itself with a feature field, which
     * is how every route or POI selection is written:
     *
     *     @is_selected: [param::selected_id] = [osmid] + '';
     *     #routes { line-color: @is_selected ? red : blue; }
     *
     * Setting it used to mean decoding every visible tile again, because the comparison can only be
     * answered per feature. The decoder instead folds it BOTH ways at decode: the tile carries the
     * selected and the unselected appearance as two style slots, and each feature keeps the hash of
     * what the parameter is compared with (fieldExpression, evaluated on that feature). A change is
     * then a hash comparison and a rewrite of one byte per vertex - see vt::TileGeometry.
     */
    struct SelectionParameter {
        std::string name;            // the parameter, without the "param::" prefix
        Expression fieldExpression;  // the side of the comparison the feature answers
    };
}

#endif
