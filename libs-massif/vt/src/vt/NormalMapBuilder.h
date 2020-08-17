/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_NORMALMAPBUILDER_H_
#define _CARTO_VT_NORMALMAPBUILDER_H_

#include "Bitmap.h"
#include "TileId.h"

#include <memory>
#include <array>
#include <cstdint>

namespace carto { namespace vt {
    class NormalMapBuilder final {
    public:
        explicit NormalMapBuilder(const std::array<float, 4>& rgbaHeightScale, std::uint8_t alpha);
        virtual ~NormalMapBuilder() = default;

        std::shared_ptr<const Bitmap> buildNormalMapFromHeightMap(const carto::vt::TileId& tileId, const std::shared_ptr<const Bitmap>& bitmap) const;

    protected:
        float unpackHeight(std::uint32_t color) const;
        std::uint32_t packNormal(float dx, float dy, float dz) const;

        const std::array<float, 4> _rgbaHeightScale;
        const std::uint8_t _alpha;
    };
} }

#endif
