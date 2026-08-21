/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_STRUCTCODEC_H_
#define _MASSIF_API_STRUCTCODEC_H_

#include "core/MapBounds.h"
#include "core/MapPos.h"
#include "core/MapTile.h"
#include "core/MapRange.h"
#include "core/MapVec.h"
#include "core/ScreenPos.h"
#include "core/Variant.h"
#include "ui/ClickInfo.h"

#include <map>
#include <string>
#include <vector>

namespace massif { namespace api {

    /**
     * JSON for the small by-value structs the SDK passes around.
     *
     * These are the `%attributeval` properties - `MapPos`, `MapRange` and friends - which are too
     * structured for a scalar and too small to deserve a handle. A position is `[x, y]` or
     * `[x, y, z]` and a range is `[min, max]`, because that is what an app writes in a spec.
     *
     * Decoding is lenient in one direction only: a missing z is 0, but a wrong shape fails rather
     * than being guessed at.
     */
    namespace StructCodec {

        std::string encode(const MapPos& value);
        std::string encode(const MapVec& value);
        std::string encode(const ScreenPos& value);
        std::string encode(const MapRange& value);
        std::string encode(const MapBounds& value);
        /** A tile, as [x, y, zoom] - the same spelling a call argument uses. */
        std::string encode(const MapTile& value);
        /**
         * A click, as an OBJECT rather than an array: its two fields mean different things and
         * neither order is natural. A path walks into it, so clickInfo.clickType reads directly.
         */
        std::string encode(const ClickInfo& value);
        std::string encode(const Variant& value);
        /** A list of names - the shape a "which layers" filter has. */
        std::string encode(const std::vector<std::string>& value);
        /**
         * A list of positions, as [[x,y],…].
         *
         * Deliberately NOT in the generator's CODEC_TYPES, so no property accessor is emitted for
         * a vector<MapPos>: a route is thousands of positions and JSON is what the bulk channel
         * exists to avoid. This is for the handful a spec or an argument list carries.
         */
        std::string encode(const std::vector<MapPos>& value);
        /** A string map - HTTP headers, and a layer's metadata. */
        std::string encode(const std::map<std::string, std::string>& value);
        std::string encode(const std::map<std::string, Variant>& value);

        bool decode(const std::string& json, MapPos& value);
        bool decode(const std::string& json, MapVec& value);
        bool decode(const std::string& json, ScreenPos& value);
        bool decode(const std::string& json, MapRange& value);
        bool decode(const std::string& json, MapBounds& value);
        bool decode(const std::string& json, MapTile& value);
        bool decode(const std::string& json, ClickInfo& value);
        bool decode(const std::string& json, Variant& value);
        bool decode(const std::string& json, std::vector<std::string>& value);
        bool decode(const std::string& json, std::vector<MapPos>& value);
        bool decode(const std::string& json, std::map<std::string, std::string>& value);
        bool decode(const std::string& json, std::map<std::string, Variant>& value);

    }

} }

#endif
