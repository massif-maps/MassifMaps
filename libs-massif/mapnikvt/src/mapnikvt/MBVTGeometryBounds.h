/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_MBVTGEOMETRYBOUNDS_H_
#define _MASSIF_MAPNIKVT_MBVTGEOMETRYBOUNDS_H_

#include <cglib/bbox.h>
#include <cglib/mat.h>

#include <vector>

namespace massif::mvt {

    /**
     * The bounds of the vertices MBVTFeatureDecoder's decodeGeometry yields for an MVT command stream
     * of `size` integers read through at(i), without building them. Empty when it has no vertex.
     */
    template <typename At>
    cglib::bbox2<float> mbvtGeometryBounds(int size, const At& at, float scale) {
        cglib::bbox2<float> bounds = cglib::bbox2<float>::smallest();
        int cx = 0, cy = 0;
        int cmd = 0, length = 0;
        for (int i = 0; i < size; ) {
            if (length == 0) {
                int cmdLength = at(i++);
                length = cmdLength >> 3;
                cmd = cmdLength & 7;
                if (length == 0) {
                    continue;
                }
            }

            length--;
            if ((cmd == 1 || cmd == 2) && i + 2 <= size) {
                int dx = at(i++);
                int dy = at(i++);
                cx += ((dx >> 1) ^ (-(dx & 1)));
                cy += ((dy >> 1) ^ (-(dy & 1)));
                bounds.add(cglib::vec2<float>(static_cast<float>(cx) * scale, static_cast<float>(cy) * scale));
            }
        }
        return bounds;
    }

    /** Whether one part of a geometry, its vertices already transformed, meets the clip box. */
    inline bool mbvtPartMeetsClip(const std::vector<cglib::vec2<float>>& vertices, const cglib::bbox2<float>& clipBox) {
        if (vertices.empty()) {
            return false;
        }
        cglib::bbox2<float> bounds = cglib::bbox2<float>::smallest();
        for (const cglib::vec2<float>& p : vertices) {
            bounds.add(p);
        }
        return bounds.inside(clipBox);
    }

    /** Whether bounds, carried by a scale-and-translate transform, meet the clip box. */
    inline bool mbvtBoundsMeetClip(const cglib::bbox2<float>& bounds, const cglib::mat3x3<float>& transform, const cglib::bbox2<float>& clipBox) {
        if (!(bounds.min(0) <= bounds.max(0))) { // no vertex; bbox::empty() would also drop a point
            return false;
        }
        return cglib::bbox2<float>(cglib::transform_point(bounds.min, transform), cglib::transform_point(bounds.max, transform)).inside(clipBox);
    }

}

#endif
