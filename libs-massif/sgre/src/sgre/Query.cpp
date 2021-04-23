#include "Query.h"

namespace carto { namespace sgre {
    picojson::value Query::serialize() const {
        picojson::array pos0Def, pos1Def;
        for (std::size_t i = 0; i < 3; i++) {
            pos0Def.emplace_back(_points[0](i));
            pos1Def.emplace_back(_points[1](i));
        }
        picojson::object queryObj;
        queryObj["pos0"] = picojson::value(pos0Def);
        queryObj["pos1"] = picojson::value(pos1Def);
        queryObj["filter0"] = picojson::value(_filters[0]);
        queryObj["filter1"] = picojson::value(_filters[1]);
        return picojson::value(queryObj);
    }

    Query Query::parse(const picojson::value& queryDef) {
        const picojson::array& pos0Def = queryDef.get("pos0").get<picojson::array>();
        const picojson::array& pos1Def = queryDef.get("pos1").get<picojson::array>();
        Point pos0, pos1;
        for (std::size_t i = 0; i < 3; i++) {
            pos0(i) = pos0Def.at(i).get<double>();
            pos1(i) = pos1Def.at(i).get<double>();
        }
        FeatureFilter filter0 = queryDef.get("filter0").get<picojson::object>();
        FeatureFilter filter1 = queryDef.get("filter1").get<picojson::object>();
        return Query(pos0, pos1, filter0, filter1);
    }
} }
