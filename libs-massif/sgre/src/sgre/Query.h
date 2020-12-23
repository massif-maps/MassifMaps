/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_SGRE_QUERY_H_
#define _CARTO_SGRE_QUERY_H_

#include "Base.h"

#include <array>

#include <picojson/picojson.h>

namespace carto { namespace sgre {
    class Query final {
    public:
        Query() = delete;
        explicit Query(const Point& pos0, const Point& pos1, const FeatureFilter& filter0 = FeatureFilter(), const FeatureFilter& filter1 = FeatureFilter()) : _points {{ pos0, pos1 }}, _filters {{ filter0, filter1 }} { }

        Point getPos(int index) const { return _points.at(index); }
        void setPos(int index, const Point& pos) { _points.at(index) = pos; }
        FeatureFilter getFilter(int index) const { return _filters.at(index); }
        void setFilter(int index, const FeatureFilter& filter) { _filters.at(index) = filter; }

        picojson::value serialize() const;

        static Query parse(const picojson::value& queryDef);

    private:
        std::array<Point, 2> _points;
        std::array<FeatureFilter, 2> _filters;
    };
} }

#endif
