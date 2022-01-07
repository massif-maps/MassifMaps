/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MAPNIKVT_TORQUEMAP_H_
#define _CARTO_MAPNIKVT_TORQUEMAP_H_

#include "Map.h"

#include <memory>
#include <string>

namespace carto::mvt {
    class TorqueMap : public Map {
    public:
        struct TorqueSettings {
            int frameCount = 128;
            float resolution = 2.0f;
            float animationDuration = 30.0f;
            vt::Color clearColor;
            std::string timeAttribute = "time";
            std::string aggregationFunction = "count(cartodb_id)";
            std::string dataAggregation = "linear";
        };

        explicit TorqueMap(const Settings& settings, const TorqueSettings& torqueSettings) : Map(settings), _torqueSettings(torqueSettings) { }

        const TorqueSettings& getTorqueSettings() const { return _torqueSettings; }

    private:
        TorqueSettings _torqueSettings;
    };
}

#endif
