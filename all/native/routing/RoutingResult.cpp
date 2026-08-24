#ifdef _MASSIF_ROUTING_SUPPORT

#include "RoutingResult.h"
#include "components/Exceptions.h"
#include "core/Variant.h"

#include <numeric>
#include <functional>
#include <utility>
#include <iomanip>
#include <sstream>

namespace massif {

    RoutingResult::RoutingResult(const std::shared_ptr<Projection>& projection, std::vector<MapPos> points, std::vector<RoutingInstruction> instructions, const std::string rawResult) :
        _projection(projection),
        _rawResult(rawResult),
        _points(std::move(points)),
        _instructions(std::move(instructions))
    {
        if (!projection) {
            throw NullArgumentException("Null projection");
        }
    }

    RoutingResult::~RoutingResult() {
    }

    const std::shared_ptr<Projection>& RoutingResult::getProjection() const {
        return _projection;
    }

    const std::vector<MapPos>& RoutingResult::getPoints() const {
        return _points;
    }

    const std::vector<RoutingInstruction>& RoutingResult::getInstructions() const {
        return _instructions;
    }

    int RoutingResult::getInstructionCount() const {
        return static_cast<int>(_instructions.size());
    }

    std::string RoutingResult::getInstructionsJSON() const {
        std::stringstream ss;
        ss << std::setiosflags(std::ios::fixed) << std::setprecision(6) << "[";
        for (std::size_t index = 0; index < _instructions.size(); index++) {
            const RoutingInstruction& instruction = _instructions[index];
            ss << (index ? ",{" : "{")
               << "\"action\":" << static_cast<int>(instruction.getAction())
               << ",\"pointIndex\":" << instruction.getPointIndex()
               << ",\"streetName\":" << Variant(instruction.getStreetName()).toString()
               << ",\"instruction\":" << Variant(instruction.getInstruction()).toString()
               << ",\"turnAngle\":" << instruction.getTurnAngle()
               << ",\"azimuth\":" << instruction.getAzimuth()
               << ",\"distance\":" << instruction.getDistance()
               << ",\"time\":" << instruction.getTime()
               << "}";
        }
        ss << "]";
        return ss.str();
    }

    int RoutingResult::getPointCount() const {
        return static_cast<int>(_points.size());
    }

    double RoutingResult::getTotalDistance() const {
        return std::accumulate(_instructions.begin(), _instructions.end(), 0.0, [](double dist, const RoutingInstruction& instruction) {
            return dist + instruction.getDistance();
        });
    }
    
    double RoutingResult::getTotalTime() const {
        return std::accumulate(_instructions.begin(), _instructions.end(), 0.0, [](double time, const RoutingInstruction& instruction) {
            return time + instruction.getTime();
        });
    }

    const std::string& RoutingResult::getRawResult() const {
        return _rawResult;
    }

    std::string RoutingResult::toString() const {
        std::stringstream ss;
        ss << std::setiosflags(std::ios::fixed);
        ss << "RoutingResult [";
        ss << "instructions=" << _instructions.size() << ", ";
        ss << "totalDistance=" << getTotalDistance() << ", ";
        ss << "totalTime=" << getTotalTime();
        ss << "]";
        return ss.str();
    }

}

#endif
