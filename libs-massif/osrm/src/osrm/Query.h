/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_OSRM_QUERY_H_
#define _CARTO_OSRM_QUERY_H_

#include "Base.h"

#include <array>

namespace carto::osrm {
    class Query final {
    public:
        Query() = delete;
        explicit Query(const WGSPos& pos0, const WGSPos& pos1) : _points {{ pos0, pos1 }} { }

        WGSPos getPos(int index) const { return _points.at(index); }

    private:
        std::array<WGSPos, 2> _points;
    };
}

#endif
