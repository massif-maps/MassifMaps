/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CELESTIALIMAGE_H_
#define _MASSIF_CELESTIALIMAGE_H_

#include "celestial/CelestialObject.h"

#include <memory>
#include <vector>

namespace massif {
    class Bitmap;

    /**
     * A bitmap laid on the sky, e.g. constellation artwork: three points of the bitmap are pinned
     * to three directions and the rest follows on the sky sphere, so it turns and stretches with the
     * sky rather than facing the camera. Its own direction is not used.
     */
    class CelestialImage : public CelestialObject {
    public:
        CelestialImage();
        virtual ~CelestialImage();

        /**
         * Returns the bitmap of the image.
         * @return The bitmap, or null if none is set.
         */
        std::shared_ptr<Bitmap> getBitmap() const;
        /**
         * Sets the bitmap of the image. An image without a bitmap is not drawn.
         * @param bitmap The new bitmap.
         */
        void setBitmap(const std::shared_ptr<Bitmap>& bitmap);

        /**
         * Pins the bitmap to the sky by three anchors, each a point of the bitmap and the direction
         * it sits in: [u, v, azimuth, altitude] per anchor, twelve values in all. u and v run 0..1
         * across the bitmap, v down from the top; azimuth and altitude are in degrees. The image is
         * not drawn until the anchors are set, nor while they are collinear.
         * @param anchors The anchors, as u, v, azimuth, altitude for each of the three.
         */
        void setAnchors(const std::vector<double>& anchors);
        /**
         * Returns the anchors.
         * @return The anchors, as u, v, azimuth, altitude for each of the three, or empty if unset.
         */
        std::vector<double> getAnchors() const;

        /**
         * Returns whether the bitmap's brightness is read as its opacity.
         * @return True if the brightness is the opacity.
         */
        bool isLuminanceAlpha() const;
        /**
         * Sets whether the bitmap's brightness is read as its opacity, the color then giving the
         * hue: for artwork drawn light on black, as Stellarium's is, which then shows on a pale sky
         * too. The default is false.
         * @param luminanceAlpha True to read the brightness as the opacity.
         */
        void setLuminanceAlpha(bool luminanceAlpha);

        /**
         * Builds the image's mesh as unit direction vectors, in the map's local frame (x east,
         * y north, z up), (subdivisions + 1)^2 of them row by row from the top of the bitmap.
         * Called by the renderer; an application does not need it.
         * @param subdivisions The number of cells along each side of the bitmap.
         * @return The direction vectors, or empty if the image cannot be placed.
         */
        std::vector<cglib::vec3<double> > buildDirections(int subdivisions) const;

    private:
        std::shared_ptr<Bitmap> _bitmap;
        std::vector<double> _anchors;
        bool _luminanceAlpha;
    };

}

#endif
