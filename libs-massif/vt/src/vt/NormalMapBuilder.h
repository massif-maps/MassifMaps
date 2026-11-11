/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_VT_NORMALMAPBUILDER_H_
#define _MASSIF_VT_NORMALMAPBUILDER_H_

#include "Bitmap.h"
#include "TileId.h"

#include <memory>
#include <array>
#include <cstdint>

#include <cglib/vec.h>

namespace massif::vt {
    class NormalMapBuilder final {
    public:
        // rgbaHeightScale: coefficients for the slope/normal (tile-scaled, constant term usually zeroed). elevationCoeffs: raw absolute
        // elevation (m = c0*R + c1*G + c2*B + c3, DEM alpha assumed opaque), used only when encodeElevation is set.
        explicit NormalMapBuilder(const std::array<float, 4>& rgbaHeightScale, std::uint8_t alpha, bool encodeElevation = false, const std::array<float, 4>& elevationCoeffs = { { 0.0f, 0.0f, 0.0f, 0.0f } });
        virtual ~NormalMapBuilder() = default;

        std::shared_ptr<const Bitmap> buildNormalMapFromHeightMap(const massif::vt::TileId& tileId, const std::shared_ptr<const Bitmap>& bitmap) const;
        std::shared_ptr<const Bitmap> buildNormalMapFromHeightMap(const massif::vt::TileId& subTileId, const massif::vt::TileId& tileId, const std::shared_ptr<const Bitmap>& bitmap) const;
        std::shared_ptr<const Bitmap> buildNormalMapFromHeightMap2(const massif::vt::TileId& subTileId, const massif::vt::TileId& tileId, const std::shared_ptr<const Bitmap>& bitmap) const;

        // Fixed-point elevation packing for encodeElevation: B (high) and A (low) hold a 16-bit height, m = elev16 * ELEVATION_SCALE +
        // ELEVATION_OFFSET, -1100..15283 m at 0.25 m. The shader that samples the normal map decodes with the same constants.
        static constexpr float ELEVATION_OFFSET = -1100.0f;
        static constexpr float ELEVATION_SCALE = 0.25f;

    protected:
        float unpackHeight(std::uint32_t color) const;
        float unpackElevationMeters(std::uint32_t color) const;
        std::uint32_t packNormal(cglib::vec3<float> normal, float heightMeters) const;

        const std::array<float, 4> _rgbaHeightScale;
        const std::array<float, 4> _elevationCoeffs;
        const std::uint8_t _alpha;
        // When set, the normal map holds normal.xy in R,G and a 16-bit elevation in B,A (normal.z reconstructed in the shader, contrast a
        // uniform), so hillshade/custom shaders can sample absolute elevation. Off by default: RGB=normal, A=contrast.
        const bool _encodeElevation;
    };
}

#endif
