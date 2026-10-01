#ifndef _MASSIF_MAPNIKVT_LINEANCHORS_H_
#define _MASSIF_MAPNIKVT_LINEANCHORS_H_

#include <cglib/vec.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <utility>
#include <vector>

namespace massif::mvt {
    // maplibre's 3/5 em window (get_anchors.ts)
    constexpr float LINE_LABEL_ANGLE_WINDOW = 0.6f;

    // maplibre's checkMaxAngle (check_max_angle.ts): the turns summed over any window of the run
    // centred at anchorDistance; lengths[i] is the distance along the line to points[i]
    inline bool checkLineLabelMaxAngle(const std::vector<cglib::vec2<float>>& points, const std::vector<float>& lengths, float anchorDistance, float labelLength, float windowSize, float maxAngle) {
        std::deque<std::pair<float, float>> recentCorners;
        float recentAngle = 0;
        for (std::size_t i = 1; i + 1 < points.size(); i++) {
            if (lengths[i] <= anchorDistance - labelLength * 0.5f || lengths[i] >= anchorDistance + labelLength * 0.5f) {
                continue;
            }
            cglib::vec2<float> in = points[i] - points[i - 1];
            cglib::vec2<float> out = points[i + 1] - points[i];
            float angle = std::abs(std::atan2(in(0) * out(1) - in(1) * out(0), cglib::dot_product(in, out)));
            recentCorners.emplace_back(lengths[i], angle);
            recentAngle += angle;
            while (lengths[i] - recentCorners.front().first > windowSize) {
                recentAngle -= recentCorners.front().second;
                recentCorners.pop_front();
            }
            if (recentAngle > maxAngle) {
                return false;
            }
        }
        return true;
    }

    // maplibre's getAnchors (get_anchors.ts) as distances along the line, the middle tried only when
    // no anchor passes. glyphSize 0 skips the max-angle test: a billboard is not laid along the line.
    // fitScale shrinks the label for the fit and angle tests only; the offset stays maplibre's.
    template <typename InTile>
    std::vector<float> lineLabelAnchors(const std::vector<cglib::vec2<float>>& points, const std::vector<float>& lengths, bool continued, float step, float labelLength, float glyphSize, float maxAngle, InTile inTile, float fitScale = 1.0f) {
        std::vector<float> anchors;
        if (points.size() < 2 || !(step > 0)) {
            return anchors;
        }
        float totalLength = lengths.back();
        float fitLength = labelLength * fitScale;
        auto accepts = [&](float distance) {
            if (distance - fitLength * 0.5f < 0 || distance + fitLength * 0.5f > totalLength || !inTile(distance)) {
                return false;
            }
            return glyphSize <= 0 || checkLineLabelMaxAngle(points, lengths, distance, fitLength, glyphSize * fitScale * LINE_LABEL_ANGLE_WINDOW, maxAngle);
        };
        float offset = (continued ? step * 0.5f : std::fmod(labelLength * 0.5f + glyphSize * 2.0f, step));
        for (float distance = offset; distance < totalLength; distance += step) {
            if (accepts(distance)) {
                anchors.push_back(distance);
            }
        }
        if (anchors.empty() && !continued && accepts(totalLength * 0.5f)) {
            anchors.push_back(totalLength * 0.5f);
        }
        return anchors;
    }

    // Where the first spaced marker sits along a line of `lineLength`, or -1 for none: from the whole
    // line, not its first segment, and none on a line too short to hold two markers.
    inline float lineMarkerStart(float lineLength, float spacing, float markerSize) {
        return lineLength < 2 * markerSize ? -1.0f : std::min(lineLength, spacing) * 0.5f;
    }
}

#endif
