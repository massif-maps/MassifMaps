/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPMOVEINFO_H_
#define _MASSIF_MAPMOVEINFO_H_

#include "ui/MapMoveReason.h"

namespace massif {

    /**
     * The payload of the facade's map.moved and map.stable events. The object API takes the same
     * value as a plain argument; this exists because a facade event carries an object or nothing.
     */
    class MapMoveInfo {
    public:
        explicit MapMoveInfo(MapMoveReason::MapMoveReason reason);
        virtual ~MapMoveInfo();

        /**
         * Returns what caused the movement.
         * @return The reason for the movement.
         */
        MapMoveReason::MapMoveReason getReason() const;

    private:
        MapMoveReason::MapMoveReason _reason;
    };

}

#endif
