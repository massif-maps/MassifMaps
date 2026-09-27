/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_STRUCTCODEC_H_
#define _MASSIF_API_STRUCTCODEC_H_

#include "api/PropertyTable.h"
#include "core/MapBounds.h"
#include "core/MapPos.h"
#include "core/MapTile.h"
#include "core/MapRange.h"
#include "core/MapVec.h"
#include "core/ScreenBounds.h"
#include "core/ScreenPos.h"
#include "components/LightStop.h"
#include "core/Variant.h"
#include "graphics/Color.h"
#include "ui/ClickInfo.h"

#include <map>
#include <string>
#include <vector>

namespace massif { namespace api {

    /**
     * JSON for the small by-value `%attributeval` structs: a position is `[x, y]` or `[x, y, z]`,
     * a range `[min, max]`. A missing z decodes as 0; a wrong shape fails.
     */
    namespace StructCodec {

        std::string encode(const MapPos& value);
        std::string encode(const MapVec& value);
        std::string encode(const ScreenPos& value);
        std::string encode(const MapRange& value);
        std::string encode(const MapBounds& value);
        std::string encode(const ScreenBounds& value);
        /** A tile, as [x, y, zoom] - the same spelling a call argument uses. */
        std::string encode(const MapTile& value);
        /** A click, as an object, so a path can read clickInfo.clickType directly. */
        std::string encode(const ClickInfo& value);
        std::string encode(const Variant& value);
        /** One day-cycle light stop, as an object: five fields with no natural order. */
        std::string encode(const LightStop& value);
        /** A light curve, as [stop, …]. */
        std::string encode(const std::vector<LightStop>& value);
        /** A list of names - the shape a "which layers" filter has. */
        std::string encode(const std::vector<std::string>& value);
        /**
         * A list of positions, as [[x,y],…], for specs and arguments. Deliberately not in the generator's
         * CODEC_TYPES: large position lists go through the bulk channel, not JSON.
         */
        std::string encode(const std::vector<MapPos>& value);
        /** Rings: a polygon's outline and its holes, as [[[x,y],…],…]. */
        std::string encode(const std::vector<std::vector<MapPos> >& value);
        /** A string map - HTTP headers, and a layer's metadata. */
        std::string encode(const std::map<std::string, std::string>& value);
        std::string encode(const std::map<std::string, Variant>& value);

        bool decode(const std::string& json, MapPos& value);
        bool decode(const std::string& json, MapVec& value);
        bool decode(const std::string& json, ScreenPos& value);
        bool decode(const std::string& json, MapRange& value);
        bool decode(const std::string& json, MapBounds& value);
        bool decode(const std::string& json, ScreenBounds& value);
        bool decode(const std::string& json, MapTile& value);
        bool decode(const std::string& json, ClickInfo& value);
        bool decode(const std::string& json, Variant& value);
        bool decode(const std::string& json, LightStop& value);
        bool decode(const std::string& json, std::vector<LightStop>& value);
        bool decode(const std::string& json, std::vector<std::string>& value);
        bool decode(const std::string& json, std::vector<MapPos>& value);
        bool decode(const std::string& json, std::vector<std::vector<MapPos> >& value);
        bool decode(const std::string& json, std::map<std::string, std::string>& value);
        bool decode(const std::string& json, std::map<std::string, Variant>& value);

        /**
         * A colour, from "#rgb", "#rgba", "#rrggbb", "#rrggbbaa" or a plain ARGB number. The one
         * decoder for every facade colour, matching mvt::parseCSSColor.
         * @return false for anything else, so the caller leaves the value alone.
         */
        bool decodeColor(const Variant& value, Color& color);
        bool decodeColor(const PropertyValue& value, Color& color);

        /**
         * One entry of a bag property - see IndexedAccess. A Variant entry keeps its type; a null
         * one reads false, so a JSON null and a missing key are the same here.
         */
        bool readEntry(const std::string& entry, PropertyValue& value);
        bool readEntry(const Variant& entry, PropertyValue& value);
        void writeEntry(const PropertyValue& value, std::string& entry);
        void writeEntry(const PropertyValue& value, Variant& entry);

    }

} }

#endif
