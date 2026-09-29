/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CELESTIALIMAGEGRID_H_
#define _MASSIF_CELESTIALIMAGEGRID_H_

#include <cmath>
#include <vector>

#include <cglib/vec.h>
#include <cglib/mat.h>

namespace massif {

    /**
     * The bitmap-to-sky mapping of a CelestialImage, header-only so it is testable without a renderer.
     * The three anchor directions fix a plane, and a point of the bitmap is its spot on that plane seen
     * from the centre: the transform Stellarium draws its constellation art with.
     */
    namespace CelestialImageGrid {

        /**
         * Builds (subdivisions + 1)^2 unit directions over the bitmap, row by row from the top.
         * @param uvs The anchors' bitmap coordinates, 0..1, v down from the top, (u, v) per anchor.
         * @param directions The anchors' unit directions.
         * @param subdivisions The number of cells along each side.
         * @param grid The resulting directions.
         * @return False if the anchors are collinear in the bitmap or their directions span no plane.
         */
        inline bool Build(const cglib::vec2<double> (&uvs)[3], const cglib::vec3<double> (&directions)[3], int subdivisions, std::vector<cglib::vec3<double> >& grid) {
            grid.clear();
            if (subdivisions < 1) {
                return false;
            }
            cglib::mat3x3<double> bitmapToPlane = cglib::mat3x3<double>::zero();
            cglib::mat3x3<double> anchorDirections = cglib::mat3x3<double>::zero();
            for (int i = 0; i < 3; i++) {
                bitmapToPlane(0, i) = uvs[i](0);
                bitmapToPlane(1, i) = uvs[i](1);
                bitmapToPlane(2, i) = 1.0;
                for (int c = 0; c < 3; c++) {
                    anchorDirections(c, i) = directions[i](c);
                }
            }
            // A plane through the centre would send part of the bitmap to the far side of the sky.
            if (std::abs(cglib::determinant(bitmapToPlane)) < 1.0e-9 || std::abs(cglib::determinant(anchorDirections)) < 1.0e-9) {
                return false;
            }
            cglib::mat3x3<double> transform = anchorDirections * cglib::inverse(bitmapToPlane);

            grid.reserve((subdivisions + 1) * (subdivisions + 1));
            for (int row = 0; row <= subdivisions; row++) {
                double v = static_cast<double>(row) / subdivisions;
                for (int column = 0; column <= subdivisions; column++) {
                    double u = static_cast<double>(column) / subdivisions;
                    cglib::vec3<double> point;
                    for (int c = 0; c < 3; c++) {
                        point(c) = transform(c, 0) * u + transform(c, 1) * v + transform(c, 2);
                    }
                    grid.push_back(cglib::unit(point));
                }
            }
            return true;
        }

    }

}

#endif
