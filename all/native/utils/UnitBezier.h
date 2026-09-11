/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_UNITBEZIER_H_
#define _MASSIF_UNITBEZIER_H_

#include <cmath>

namespace massif {

    /**
     * A CSS cubic-bezier(p1x, p1y, p2x, p2y) timing curve, the solver mapbox-gl eases with.
     */
    class UnitBezier {
    public:
        UnitBezier(double p1x, double p1y, double p2x, double p2y) :
            _cx(3.0 * p1x),
            _bx(3.0 * (p2x - p1x) - _cx),
            _ax(1.0 - _cx - _bx),
            _cy(3.0 * p1y),
            _by(3.0 * (p2y - p1y) - _cy),
            _ay(1.0 - _cy - _by)
        {
        }

        double solve(double t) const {
            if (!(t > 0)) {
                return 0;
            }
            if (t >= 1) {
                return 1;
            }
            return sampleY(solveX(t));
        }

    private:
        double sampleX(double u) const { return ((_ax * u + _bx) * u + _cx) * u; }
        double sampleY(double u) const { return ((_ay * u + _by) * u + _cy) * u; }
        double sampleDerivativeX(double u) const { return (3.0 * _ax * u + 2.0 * _bx) * u + _cx; }

        double solveX(double t) const {
            double u = t;
            for (int i = 0; i < 8; i++) {
                double x = sampleX(u) - t;
                if (std::abs(x) < 1.0e-6) {
                    return u;
                }
                double d = sampleDerivativeX(u);
                if (std::abs(d) < 1.0e-6) {
                    break;
                }
                u -= x / d;
            }

            // Newton left the curve flat; bisect, which cannot.
            double lo = 0, hi = 1;
            u = t;
            for (int i = 0; i < 20; i++) {
                double x = sampleX(u);
                if (std::abs(x - t) < 1.0e-6) {
                    return u;
                }
                if (x < t) {
                    lo = u;
                } else {
                    hi = u;
                }
                u = (lo + hi) * 0.5;
            }
            return u;
        }

        double _cx, _bx, _ax;
        double _cy, _by, _ay;
    };

}

#endif
