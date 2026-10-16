#include "ManeuverArrowBuilder.h"
#include "core/Variant.h"
#include "geometry/Feature.h"
#include "geometry/FeatureCollection.h"
#include "geometry/LineGeometry.h"
#include "projections/Projection.h"
#include "utils/Const.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace massif {

    ManeuverArrowBuilder::ManeuverArrowBuilder() :
        _lengthBefore(DEFAULT_LENGTH),
        _lengthAfter(DEFAULT_LENGTH),
        _mutex()
    {
    }

    ManeuverArrowBuilder::~ManeuverArrowBuilder() {
    }

    float ManeuverArrowBuilder::getLengthBefore() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _lengthBefore;
    }

    void ManeuverArrowBuilder::setLengthBefore(float length) {
        std::lock_guard<std::mutex> lock(_mutex);
        _lengthBefore = length;
    }

    float ManeuverArrowBuilder::getLengthAfter() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _lengthAfter;
    }

    void ManeuverArrowBuilder::setLengthAfter(float length) {
        std::lock_guard<std::mutex> lock(_mutex);
        _lengthAfter = length;
    }

    std::shared_ptr<FeatureCollection> ManeuverArrowBuilder::buildArrow(const std::shared_ptr<Projection>& projection, const std::vector<MapPos>& points, const MapPos& maneuverPos) const {
        std::vector<MapPos> wgs84Points = ConvertToWgs84(projection, points);
        if (wgs84Points.size() < 2) {
            return std::make_shared<FeatureCollection>(std::vector<std::shared_ptr<Feature> >());
        }
        MapPos wgs84ManeuverPos = projection ? projection->toWgs84(maneuverPos) : maneuverPos;

        // Nearest point on the route, in the same metric the arrow is cut with.
        double originLat = wgs84ManeuverPos.getY();
        LocalPos target = ToLocal(wgs84ManeuverPos, originLat);
        std::size_t bestIndex = 0;
        double bestT = 0;
        double bestDist = -1;
        for (std::size_t i = 0; i + 1 < wgs84Points.size(); i++) {
            LocalPos p0 = ToLocal(wgs84Points[i], originLat);
            LocalPos p1 = ToLocal(wgs84Points[i + 1], originLat);
            double dx = p1.x - p0.x, dy = p1.y - p0.y;
            double len2 = dx * dx + dy * dy;
            double t = len2 > 0 ? std::max(0.0, std::min(1.0, ((target.x - p0.x) * dx + (target.y - p0.y) * dy) / len2)) : 0;
            double dist = Distance(target, Interpolate(p0, p1, t));
            if (bestDist < 0 || dist < bestDist) {
                bestDist = dist;
                bestIndex = i;
                bestT = t;
            }
        }
        return buildArrowAtSegment(wgs84Points, bestIndex, bestT);
    }

    std::shared_ptr<FeatureCollection> ManeuverArrowBuilder::buildArrowAtIndex(const std::shared_ptr<Projection>& projection, const std::vector<MapPos>& points, int maneuverIndex) const {
        std::vector<MapPos> wgs84Points = ConvertToWgs84(projection, points);
        if (wgs84Points.size() < 2) {
            return std::make_shared<FeatureCollection>(std::vector<std::shared_ptr<Feature> >());
        }
        std::size_t index = static_cast<std::size_t>(std::max(0, std::min(static_cast<int>(wgs84Points.size()) - 1, maneuverIndex)));
        if (index + 1 < wgs84Points.size()) {
            return buildArrowAtSegment(wgs84Points, index, 0.0);
        }
        return buildArrowAtSegment(wgs84Points, wgs84Points.size() - 2, 1.0);
    }

    std::vector<MapPos> ManeuverArrowBuilder::ConvertToWgs84(const std::shared_ptr<Projection>& projection, const std::vector<MapPos>& points) {
        if (!projection) {
            return points;
        }
        std::vector<MapPos> wgs84Points;
        wgs84Points.reserve(points.size());
        for (const MapPos& point : points) {
            wgs84Points.push_back(projection->toWgs84(point));
        }
        return wgs84Points;
    }

    std::shared_ptr<FeatureCollection> ManeuverArrowBuilder::buildArrowAtSegment(const std::vector<MapPos>& wgs84Points, std::size_t segmentIndex, double segmentT) const {
        float lengthBefore = 0, lengthAfter = 0;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            lengthBefore = _lengthBefore;
            lengthAfter = _lengthAfter;
        }

        double originLat = wgs84Points[segmentIndex].getY() + (wgs84Points[segmentIndex + 1].getY() - wgs84Points[segmentIndex].getY()) * segmentT;
        LocalPos anchor = Interpolate(ToLocal(wgs84Points[segmentIndex], originLat), ToLocal(wgs84Points[segmentIndex + 1], originLat), segmentT);

        // Walk out of the maneuver point in both directions, stopping at the requested length or at
        // the end of the route, whichever comes first.
        std::vector<LocalPos> backPoses;
        {
            double remaining = lengthBefore;
            LocalPos cur = anchor;
            for (std::size_t i = segmentIndex + 1; i-- > 0; ) {
                LocalPos prev = ToLocal(wgs84Points[i], originLat);
                double dist = Distance(prev, cur);
                if (dist <= 0) {
                    continue; // duplicate route point, no step and no vertex to add
                }
                if (dist >= remaining) {
                    backPoses.push_back(Interpolate(cur, prev, remaining / dist));
                    remaining = 0;
                    break;
                }
                backPoses.push_back(prev);
                remaining -= dist;
                cur = prev;
            }
        }

        std::vector<LocalPos> forwardPoses;
        {
            double remaining = lengthAfter;
            LocalPos cur = anchor;
            for (std::size_t i = segmentIndex + 1; i < wgs84Points.size(); i++) {
                LocalPos next = ToLocal(wgs84Points[i], originLat);
                double dist = Distance(next, cur);
                if (dist <= 0) {
                    continue; // duplicate route point, no step and no vertex to add
                }
                if (dist >= remaining) {
                    forwardPoses.push_back(Interpolate(cur, next, remaining / dist));
                    remaining = 0;
                    break;
                }
                forwardPoses.push_back(next);
                remaining -= dist;
                cur = next;
            }
        }

        std::vector<LocalPos> shaftPoses;
        shaftPoses.reserve(backPoses.size() + forwardPoses.size() + 1);
        shaftPoses.insert(shaftPoses.end(), backPoses.rbegin(), backPoses.rend());
        shaftPoses.push_back(anchor);
        shaftPoses.insert(shaftPoses.end(), forwardPoses.begin(), forwardPoses.end());

        // The head takes its direction and place from the LAST segment. The cut lands anywhere in a route
        // segment, so that one can be a few pixels long, and then the head sits on the join before it.
        double walkedAfter = 0;
        for (std::size_t i = backPoses.size() + 1; i < shaftPoses.size(); i++) {
            walkedAfter += Distance(shaftPoses[i - 1], shaftPoses[i]);
        }
        StraightenEnd(shaftPoses, backPoses.size(), END_RUN_FRACTION * std::min(static_cast<double>(lengthAfter), walkedAfter));

        // The tail's round cap and the join after it do the same with a short first segment: the casing steps
        // out on the inner side of the bend. Same rule, from the other end.
        double walkedBefore = 0;
        for (std::size_t i = 1; i <= backPoses.size(); i++) {
            walkedBefore += Distance(shaftPoses[i - 1], shaftPoses[i]);
        }
        std::reverse(shaftPoses.begin(), shaftPoses.end());
        StraightenEnd(shaftPoses, shaftPoses.size() - 1 - backPoses.size(), END_RUN_FRACTION * std::min(static_cast<double>(lengthBefore), walkedBefore));
        std::reverse(shaftPoses.begin(), shaftPoses.end());

        double shaftLength = 0;
        for (std::size_t i = 1; i < shaftPoses.size(); i++) {
            shaftLength += Distance(shaftPoses[i - 1], shaftPoses[i]);
        }

        std::vector<std::shared_ptr<Feature> > features;
        if (shaftPoses.size() < 2 || shaftLength <= 0) {
            return std::make_shared<FeatureCollection>(std::move(features));
        }

        std::vector<MapPos> shaft;
        shaft.reserve(shaftPoses.size());
        for (const LocalPos& pos : shaftPoses) {
            shaft.push_back(FromLocal(pos, originLat));
        }

        // One line, pointing the way the driver goes. The arrow head is the style's business:
        // 'line-end-arrow' puts it on the last vertex, sized in line widths and extruded with the
        // line, so it stays one shape with the shaft and a casing rule outlines the whole arrow.
        features.push_back(std::make_shared<Feature>(std::make_shared<LineGeometry>(shaft), Variant()));

        return std::make_shared<FeatureCollection>(std::move(features));
    }

    ManeuverArrowBuilder::LocalPos ManeuverArrowBuilder::ToLocal(const MapPos& wgs84Pos, double originLat) {
        double scaleX = std::cos(originLat * Const::DEG_TO_RAD) * Const::DEG_TO_RAD * Const::EARTH_RADIUS;
        double scaleY = Const::DEG_TO_RAD * Const::EARTH_RADIUS;
        return LocalPos { wgs84Pos.getX() * scaleX, wgs84Pos.getY() * scaleY };
    }

    MapPos ManeuverArrowBuilder::FromLocal(const LocalPos& pos, double originLat) {
        double scaleX = std::cos(originLat * Const::DEG_TO_RAD) * Const::DEG_TO_RAD * Const::EARTH_RADIUS;
        double scaleY = Const::DEG_TO_RAD * Const::EARTH_RADIUS;
        return MapPos(scaleX != 0 ? pos.x / scaleX : 0, pos.y / scaleY);
    }

    ManeuverArrowBuilder::LocalPos ManeuverArrowBuilder::Interpolate(const LocalPos& p0, const LocalPos& p1, double t) {
        return LocalPos { p0.x + (p1.x - p0.x) * t, p0.y + (p1.y - p0.y) * t };
    }

    double ManeuverArrowBuilder::Distance(const LocalPos& p0, const LocalPos& p1) {
        double dx = p1.x - p0.x, dy = p1.y - p0.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    void ManeuverArrowBuilder::StraightenEnd(std::vector<LocalPos>& poses, std::size_t anchorIndex, double minRun) {
        std::size_t last = poses.size() - 1;
        if (minRun <= 0 || last < anchorIndex + 2 || Distance(poses[last - 1], poses[last]) >= minRun) {
            return;
        }
        // The nearest vertex to the end that is a full run away from it; the anchor is the turn and stays.
        std::size_t start = last - 1;
        while (start > anchorIndex && Distance(poses[start], poses[last]) < minRun) {
            start--;
        }
        if (Distance(poses[start], poses[last]) < minRun) {
            return;
        }
        // Only a stretch that is nearly straight already: a corner in it is a turn of the route, not noise.
        double dx = poses[last].x - poses[start].x, dy = poses[last].y - poses[start].y;
        double length = std::sqrt(dx * dx + dy * dy);
        for (std::size_t i = start + 1; i < last; i++) {
            double offset = std::abs((poses[i].x - poses[start].x) * dy - (poses[i].y - poses[start].y) * dx) / length;
            if (offset > END_RUN_DEVIATION * minRun) {
                return;
            }
        }
        poses.erase(poses.begin() + static_cast<std::ptrdiff_t>(start + 1), poses.begin() + static_cast<std::ptrdiff_t>(last));
    }

    const float ManeuverArrowBuilder::DEFAULT_LENGTH = 30.0f;
    // The last segment is at least this share of the walk after the turn: the casing head's setback plus the
    // casing's half width is about 1.25 casing widths, which is 60% of the after length the app asks for.
    const double ManeuverArrowBuilder::END_RUN_FRACTION = 0.6;
    // How far, as a share of that run, a vertex may stray from it and still be ironed out.
    const double ManeuverArrowBuilder::END_RUN_DEVIATION = 0.1;

}
