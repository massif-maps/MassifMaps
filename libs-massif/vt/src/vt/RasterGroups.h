#ifndef _MASSIF_VT_RASTERGROUPS_H_
#define _MASSIF_VT_RASTERGROUPS_H_

#include "TileBitmap.h"

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

namespace massif::vt {
    // GLES2 cannot index a sampler by a computed value, so the shader unrolls the slots.
    constexpr int MAX_RASTER_SLOTS = 4;

    // Slot 0 is the bitmap unit. The normal-map draw uses 1 and 5 for terrain; 4, 6 and 7 belong to
    // other programs, and 8 units is all GLES2 guarantees.
    constexpr std::array<int, MAX_RASTER_SLOTS - 1> RASTER_SLOT_UNITS = { 4, 6, 7 };

    struct RasterGroup {
        std::size_t first;
        std::size_t count;
    };

    // Consecutive normal maps share a draw; uUVScale is the first one's, which only the pixel-addressed reads use.
    inline std::vector<RasterGroup> groupRasters(const std::vector<std::shared_ptr<TileBitmap>>& bitmaps) {
        std::vector<RasterGroup> groups;
        for (std::size_t i = 0; i < bitmaps.size(); i++) {
            if (!groups.empty()) {
                RasterGroup& group = groups.back();
                if (bitmaps[group.first]->getType() == TileBitmap::Type::NORMALMAP && bitmaps[i]->getType() == TileBitmap::Type::NORMALMAP &&
                    group.count < static_cast<std::size_t>(MAX_RASTER_SLOTS))
                {
                    group.count++;
                    continue;
                }
            }
            groups.push_back({ i, 1 });
        }
        return groups;
    }
}

#endif
