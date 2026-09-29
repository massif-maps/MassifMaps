#include "CelestialImage.h"
#include "celestial/CelestialImageGrid.h"
#include "components/Exceptions.h"
#include "graphics/Bitmap.h"
#include "utils/Const.h"

#include <cmath>

namespace massif {

    CelestialImage::CelestialImage() :
        CelestialObject(),
        _bitmap(),
        _anchors(),
        _luminanceAlpha(false)
    {
    }

    CelestialImage::~CelestialImage() {
    }

    std::shared_ptr<Bitmap> CelestialImage::getBitmap() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _bitmap;
    }

    void CelestialImage::setBitmap(const std::shared_ptr<Bitmap>& bitmap) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _bitmap = bitmap;
        }
        notifyChanged();
    }

    void CelestialImage::setAnchors(const std::vector<double>& anchors) {
        if (anchors.size() != 12) {
            throw InvalidArgumentException("CelestialImage anchors need 12 values: u, v, azimuth, altitude for each of three");
        }
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _anchors = anchors;
        }
        notifyChanged();
    }

    std::vector<double> CelestialImage::getAnchors() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _anchors;
    }

    bool CelestialImage::isLuminanceAlpha() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _luminanceAlpha;
    }

    void CelestialImage::setLuminanceAlpha(bool luminanceAlpha) {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _luminanceAlpha = luminanceAlpha;
        }
        notifyChanged();
    }

    std::vector<cglib::vec3<double> > CelestialImage::buildDirections(int subdivisions) const {
        std::lock_guard<std::mutex> lock(_mutex);

        std::vector<cglib::vec3<double> > grid;
        if (_anchors.size() != 12) {
            return grid;
        }
        cglib::vec2<double> uvs[3];
        cglib::vec3<double> directions[3];
        for (int i = 0; i < 3; i++) {
            const double* anchor = &_anchors[i * 4];
            double az = anchor[2] * Const::DEG_TO_RAD;
            double alt = anchor[3] * Const::DEG_TO_RAD;
            double cosAlt = std::cos(alt);
            uvs[i] = cglib::vec2<double>(anchor[0], anchor[1]);
            directions[i] = cglib::vec3<double>(cosAlt * std::sin(az), cosAlt * std::cos(az), std::sin(alt));
        }
        CelestialImageGrid::Build(uvs, directions, subdivisions, grid);
        return grid;
    }

}
