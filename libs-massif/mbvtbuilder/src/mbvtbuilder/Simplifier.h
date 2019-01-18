/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_MBVTBUILDER_SIMPLIFIER_H_
#define _CARTO_MBVTBUILDER_SIMPLIFIER_H_

#include <vector>
#include <queue>

#include <cglib/vec.h>

namespace carto { namespace mbvtbuilder {
    template <typename T>
    class Simplifier final {
    public:
        using Point = cglib::vec2<T>;

        explicit Simplifier(T tolerance) : _tolerance(tolerance) { }

        std::vector<Point> simplifyLineString(const std::vector<Point>& coordsList) const;
        std::vector<Point> simplifyPolygonRing(const std::vector<Point>& coordsList) const;

    private:
        static T calculateSegmentDistance(const Point& p0, const Point& p1, const Point& p);

        T _tolerance;
    };

    template <typename T>
    std::vector<typename Simplifier<T>::Point> Simplifier<T>::simplifyLineString(const std::vector<Point>& coordsList) const {
        if (coordsList.size() < 3) {
            return coordsList;
        }

        std::vector<bool> keyPoints(coordsList.size(), false);
        keyPoints.front() = keyPoints.back() = true;
        std::size_t keyPointCount = 2;

        std::queue<std::pair<std::size_t, std::size_t>> segmentQueue;
        segmentQueue.emplace(0, coordsList.size() - 1);
        while (!segmentQueue.empty()) {
            std::size_t firstIndex = segmentQueue.front().first;
            std::size_t secondIndex = segmentQueue.front().second;
            segmentQueue.pop();

            T maxDist = -1;
            std::size_t maxDistIndex = 0;
            for (std::size_t i = firstIndex + 1; i < secondIndex; i++) {
                T dist = calculateSegmentDistance(coordsList[firstIndex], coordsList[secondIndex], coordsList[i]);
                if (dist > maxDist) {
                    maxDist = dist;
                    maxDistIndex = i;
                }
            }

            if (maxDist >= _tolerance) {
                keyPoints[maxDistIndex] = true;
                keyPointCount++;

                if (maxDistIndex - firstIndex >= 2) {
                    segmentQueue.emplace(firstIndex, maxDistIndex);
                }
                if (secondIndex - maxDistIndex >= 2) {
                    segmentQueue.emplace(maxDistIndex, secondIndex);
                }
            }
        }

        if (keyPointCount == coordsList.size()) {
            return coordsList;
        }

        std::vector<Point> simplifiedCoordsList;
        simplifiedCoordsList.reserve(keyPointCount);
        for (std::size_t i = 0; i < keyPoints.size(); i++) {
            if (keyPoints[i]) {
                simplifiedCoordsList.push_back(coordsList[i]);
            }
        }

        return simplifiedCoordsList;
    }

    template <typename T>
    std::vector<typename Simplifier<T>::Point> Simplifier<T>::simplifyPolygonRing(const std::vector<Point>& coordsList) const {
        return simplifyLineString(coordsList);
    }

    template <typename T>
    T Simplifier<T>::calculateSegmentDistance(const Point& p0, const Point& p1, const Point& p) {
        cglib::vec2<T> v0 = p1 - p0;
        cglib::vec2<T> v1 = p - p0;
        T dot1 = cglib::dot_product(v0, v1);
        T dot2 = cglib::dot_product(v0, v0);
        if (dot1 <= 0) {
            return cglib::length(p - p0);
        }
        if (dot1 >= dot2) {
            return cglib::length(p - p1);
        }

        T s = dot1 / dot2;
        Point pp = p0 + v0 * s;
        return cglib::length(p - pp);
    }
} }

#endif
