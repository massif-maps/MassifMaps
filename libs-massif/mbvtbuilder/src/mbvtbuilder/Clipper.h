/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MBVTBUILDER_CLIPPER_H_
#define _CARTO_MBVTBUILDER_CLIPPER_H_

#include <vector>
#include <algorithm>
#include <limits>

#include <cglib/vec.h>
#include <cglib/bbox.h>

namespace carto { namespace mbvtbuilder {
    template <typename T>
    class Clipper final {
    public:
        using Point = cglib::vec2<T>;
        using Bounds = cglib::bbox2<T>;

        explicit Clipper(const Bounds& bounds) : _bounds(bounds) { }

        bool testPoint(const Point& coords) const;
        std::vector<std::vector<Point>> clipLineString(const std::vector<Point>& coordsList) const;
        std::vector<Point> clipPolygonRing(const std::vector<Point>& coordsList) const;

    private:
        using PointMask = unsigned int;
        
        enum {
            LEFT_OUT   = 1,
            RIGHT_OUT  = 2,
            TOP_OUT    = 4,
            BOTTOM_OUT = 8
        };

        PointMask classifyPoint(const Point& p) const;
        Point clipPoint(Point p, PointMask mask) const;
        Point clipSegment(const Point& p0, const Point& p1, PointMask mask, T dir) const;

        Bounds _bounds;
    };

    template <typename T>
    bool Clipper<T>::testPoint(const Point& coords) const {
        if (classifyPoint(coords)) {
            return false;
        }

        return true;
    }

    template <typename T>
    std::vector<std::vector<typename Clipper<T>::Point>> Clipper<T>::clipLineString(const std::vector<Point>& coordsList) const {
        if (coordsList.empty()) {
            return std::vector<std::vector<Point>>();
        }

        std::vector<std::vector<Point>> clippedSegments;
        std::vector<Point> clippedCoordsList;
        clippedCoordsList.reserve(coordsList.size());

        auto pushClippedPoint = [&clippedCoordsList](const Point& p) {
            if (clippedCoordsList.empty() || clippedCoordsList.back() != p) {
                clippedCoordsList.push_back(p);
            }
        };

        Point p0 = coordsList.front();
        PointMask mask0 = classifyPoint(p0);
        if (!mask0) {
            pushClippedPoint(p0);
        }
        for (std::size_t i = 1; i < coordsList.size(); i++) {
            Point p1 = coordsList[i];
            if (p1 == p0) {
                continue;
            }
            PointMask mask1 = classifyPoint(p1);

            if (!(mask0 | mask1)) {
                pushClippedPoint(p1);
            }
            else if (!(mask0 & mask1)) {
                Point q0 = mask0 ? clipSegment(p0, p1, mask0, 0) : p0;
                Point q1 = mask1 ? clipSegment(p0, p1, mask1, 1) : p1;
                if (cglib::dot_product(q0 - p0, p1 - p0) < cglib::dot_product(q1 - p0, p1 - p0)) {
                    if (mask0) {
                        pushClippedPoint(q0);
                    }
                    pushClippedPoint(q1);
                }
                if (mask1) {
                    if (clippedCoordsList.size() >= 2) {
                        clippedSegments.push_back(std::move(clippedCoordsList));
                    }
                    clippedCoordsList.clear();
                }
            }

            p0 = p1;
            mask0 = mask1;
        }
        
        if (clippedCoordsList.size() >= 2) {
            clippedSegments.push_back(std::move(clippedCoordsList));
        }

        return clippedSegments;
    }

    template <typename T>
    std::vector<typename Clipper<T>::Point> Clipper<T>::clipPolygonRing(const std::vector<Point>& coordsList) const {
        PointMask allmask = LEFT_OUT | RIGHT_OUT | TOP_OUT | BOTTOM_OUT;
        PointMask anymask = 0;
        for (std::size_t i = 0; i < coordsList.size(); i++) {
            PointMask mask = classifyPoint(coordsList[i]);
            allmask = allmask & mask;
            anymask = anymask | mask;
        }
        if (allmask) {
            return std::vector<Point>();
        }
        if (!anymask) {
            return coordsList;
        }

        std::vector<Point> clippedCoordsList;
        clippedCoordsList.reserve(coordsList.size() + 1);
        std::vector<Point> baseCoordsList = coordsList;

        auto pushClippedPoint = [&clippedCoordsList](const Point& p) {
            if (clippedCoordsList.empty() || clippedCoordsList.back() != p) {
                clippedCoordsList.push_back(p);
            }
        };

        for (PointMask edgemask : { LEFT_OUT, RIGHT_OUT, TOP_OUT, BOTTOM_OUT }) {
            if (!(anymask & edgemask)) {
                continue;
            }

            Point p0 = baseCoordsList.front();
            PointMask mask0 = classifyPoint(p0) & edgemask;
            if (!mask0) {
                pushClippedPoint(p0);
            }
            for (std::size_t i = 1; i <= baseCoordsList.size(); i++) {
                Point p1 = baseCoordsList[i < baseCoordsList.size() ? i : 0];
                if (p1 == p0) {
                    continue;
                }
                PointMask mask1 = classifyPoint(p1) & edgemask;

                if (!(mask0 | mask1)) {
                    pushClippedPoint(p1);
                }
                else if (!(mask0 & mask1)) {
                    if (mask0) {
                        pushClippedPoint(clipSegment(p0, p1, mask0, 0));
                    }
                    pushClippedPoint(mask1 ? clipSegment(p0, p1, mask1, 1) : p1);
                }

                p0 = p1;
                mask0 = mask1;
            }

            if (!clippedCoordsList.empty() && clippedCoordsList.back() == clippedCoordsList.front()) {
                clippedCoordsList.pop_back();
            }
            baseCoordsList = std::move(clippedCoordsList);
            clippedCoordsList.clear();
            if (baseCoordsList.empty()) {
                break;
            }
        }

        return baseCoordsList;
    }

    template <typename T>
    typename Clipper<T>::PointMask Clipper<T>::classifyPoint(const Point& p) const {
        PointMask mask = 0;
        if (p(0) < _bounds.min(0)) {
            mask |= LEFT_OUT;
        } else if (p(0) > _bounds.max(0)) {
            mask |= RIGHT_OUT;
        }
        if (p(1) < _bounds.min(1)) {
            mask |= TOP_OUT;
        } else if (p(1) > _bounds.max(1)) {
            mask |= BOTTOM_OUT;
        }
        return mask;
    }

    template <typename T>
    typename Clipper<T>::Point Clipper<T>::clipPoint(Point p, PointMask mask) const {
        if (mask & LEFT_OUT) {
            p(0) = _bounds.min(0);
        } else if (mask & RIGHT_OUT) {
            p(0) = _bounds.max(0);
        }
        if (mask & TOP_OUT) {
            p(1) = _bounds.min(1);
        } else if (mask & BOTTOM_OUT) {
            p(1) = _bounds.max(1);
        }
        return p;
    }

    template <typename T>
    typename Clipper<T>::Point Clipper<T>::clipSegment(const Point& p0, const Point& p1, PointMask mask, T dir) const {
        T s = dir * 2 - 1;
        T t = dir;
        PointMask clipMask = 0;
        if (mask & LEFT_OUT) {
            T tt = (_bounds.min(0) - p0(0)) / (p1(0) - p0(0));
            if (tt * s <= t * s) {
                t = tt;
                clipMask = LEFT_OUT;
            }
        } else if (mask & RIGHT_OUT) {
            T tt = (_bounds.max(0) - p0(0)) / (p1(0) - p0(0));
            if (tt * s <= t * s) {
                t = tt;
                clipMask = RIGHT_OUT;
            }
        }
        if (mask & TOP_OUT) {
            T tt = (_bounds.min(1) - p0(1)) / (p1(1) - p0(1));
            if (tt * s <= t * s) {
                t = tt;
                clipMask = TOP_OUT;
            }
        } else if (mask & BOTTOM_OUT) {
            T tt = (_bounds.max(1) - p0(1)) / (p1(1) - p0(1));
            if (tt * s <= t * s) {
                t = tt;
                clipMask = BOTTOM_OUT;
            }
        }
        return clipPoint(p0 * (1 - t) + p1 * t, clipMask);
    }
} }

#endif
