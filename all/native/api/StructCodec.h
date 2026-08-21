/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_API_STRUCTCODEC_H_
#define _MASSIF_API_STRUCTCODEC_H_

#include "core/MapBounds.h"
#include "core/MapPos.h"
#include "core/MapRange.h"
#include "core/MapVec.h"
#include "core/ScreenPos.h"
#include "core/Variant.h"

#include <string>

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
        std::string encode(const Variant& value);

        bool decode(const std::string& json, MapPos& value);
        bool decode(const std::string& json, MapVec& value);
        bool decode(const std::string& json, ScreenPos& value);
        bool decode(const std::string& json, MapRange& value);
        bool decode(const std::string& json, MapBounds& value);
        bool decode(const std::string& json, Variant& value);

    }

} }

#endif
