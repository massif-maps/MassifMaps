#include "MapMoveInfo.h"

namespace massif {

    MapMoveInfo::MapMoveInfo(MapMoveReason::MapMoveReason reason) :
        _reason(reason)
    {
    }

    MapMoveInfo::~MapMoveInfo() {
    }

    MapMoveReason::MapMoveReason MapMoveInfo::getReason() const {
        return _reason;
    }

}
