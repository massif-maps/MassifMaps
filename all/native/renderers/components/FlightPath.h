/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_FLIGHTPATH_H_
#define _MASSIF_FLIGHTPATH_H_

#include <algorithm>
#include <cmath>

namespace massif {

    /**
     * Van Wijk & Nuij's optimal zoom-and-pan path, as mapbox-gl flies it. w0, w1 and u1 share ONE
     * unit and it must be a SCREENFUL - see docs/internals/rendering/01-frame.md.
     */
    class FlightPath {
    public:
        FlightPath() : _rho(1.42), _r0(0), _s(0), _zoomDelta(0), _w0OverU1(0), _zeroPath(true) { }

        void setup(double w0, double w1, double u1, double rho) {
            _rho = (rho > 0.1 ? rho : 1.42);
            _zoomDelta = (w0 > 0 && w1 > 0 ? std::log(w0 / w1) / std::log(2.0) : 0.0);
            _zeroPath = !(std::abs(u1) > w0 * 1.0e-6) || !(w0 > 0) || !(w1 > 0);
            _w0OverU1 = (_zeroPath ? 0.0 : w0 / u1);

            double rho2 = _rho * _rho;
            if (_zeroPath) {
                // Their formula divides by the distance; a pure zoom degenerates to an exponential.
                _r0 = 0;
                _s = (w0 > 0 && w1 > 0 ? std::abs(std::log(w1 / w0)) / _rho : 0.0);
            } else {
                _r0 = zoomOutFactor(w0, w1, u1, rho2, false);
                _s = (zoomOutFactor(w0, w1, u1, rho2, true) - _r0) / _rho;
            }
            if (!(_s > 0)) {
                _s = 0;
            }
        }

        /** S, in rho-screenfuls. 0 means nothing to say: the caller interpolates on the clock. */
        double getLength() const { return _s; }

        /** v is in screenfuls per second. */
        double suggestedDuration(double v, double minSeconds) const {
            return std::max(minSeconds, _s / (v > 0 ? v : 1.2));
        }

        /** k is the EASED clock; zoomDelta is added to the start zoom. */
        void sample(double k, double& ratio, double& zoomDelta) const {
            k = std::max(0.0, std::min(1.0, k));
            if (!(_s > 0)) {
                ratio = k;
                zoomDelta = k * _zoomDelta;
                return;
            }

            double s = k * _s;
            if (_zeroPath) {
                double w = std::exp((_zoomDelta > 0 ? -1.0 : 1.0) * _rho * s);
                ratio = k;
                zoomDelta = -std::log(w) / std::log(2.0);
                return;
            }

            double coshR0 = std::cosh(_r0);
            double u = (coshR0 * std::tanh(_rho * s + _r0) - std::sinh(_r0)) / (_rho * _rho);
            double w = coshR0 / std::cosh(_rho * s + _r0);
            ratio = std::max(0.0, std::min(1.0, u * _w0OverU1));
            zoomDelta = -std::log(w) / std::log(2.0);
        }

    private:
        static double zoomOutFactor(double w0, double w1, double u1, double rho2, bool descent) {
            double b = (w1 * w1 - w0 * w0 + (descent ? -1.0 : 1.0) * rho2 * rho2 * u1 * u1) /
                       (2 * (descent ? w1 : w0) * rho2 * u1);
            return std::log(std::sqrt(b * b + 1) - b);
        }

        double _rho;
        double _r0;
        double _s;
        double _zoomDelta;
        double _w0OverU1;
        bool _zeroPath;
    };

}

#endif
