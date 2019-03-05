#include "Result.h"

namespace carto { namespace sgre {
    picojson::value Result::serialize() const {
        picojson::object resultObj;
        resultObj["status"] = picojson::value(static_cast<std::int64_t>(_status));
        if (_status == Status::SUCCESS) {
            picojson::array instructionDef;
            for (const Instruction& instruction : _instructions) {
                instructionDef.push_back(instruction.serialize());
            }
            picojson::array geometryDef;
            for (const Point& point : _geometry) {
                picojson::array pointDef;
                for (std::size_t i = 0; i < 3; i++) {
                    pointDef.emplace_back(point(i));
                }
                geometryDef.emplace_back(std::move(pointDef));
            }
            resultObj["instructions"] = picojson::value(instructionDef);
            resultObj["geometry"] = picojson::value(geometryDef);
        }
        return picojson::value(resultObj);
    }

    Result Result::parse(const picojson::value& resultDef) {
        Status status = static_cast<Status>(resultDef.get("status").get<std::int64_t>());
        if (status == Status::FAILED) {
            return Result();
        }
        const picojson::array& instructionsDef = resultDef.get("instructions").get<picojson::array>();
        std::vector<Instruction> instructions;
        for (const picojson::value& instructionDef : instructionsDef) {
            instructions.push_back(Instruction::parse(instructionDef));
        }
        const picojson::array& geometryDef = resultDef.get("geometry").get<picojson::array>();
        std::vector<Point> geometry;
        for (const picojson::value& pointDef : geometryDef) {
            Point point;
            for (std::size_t i = 0; i < 3; i++) {
                point(i) = pointDef.get<picojson::array>().at(i).get<double>();
            }
            geometry.push_back(point);
        }
        return Result(std::move(instructions), std::move(geometry));
    }
} }
