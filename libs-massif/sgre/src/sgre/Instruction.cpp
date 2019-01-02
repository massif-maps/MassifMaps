#include "Instruction.h"

namespace carto { namespace sgre {
    picojson::value Instruction::serialize() const {
        picojson::object instructionObj;
        instructionObj["type"] = picojson::value(static_cast<std::int64_t>(_type));
        instructionObj["tag"] = _tag;
        instructionObj["distance"] = picojson::value(_distance);
        instructionObj["time"] = picojson::value(_time);
        instructionObj["geomindex"] = picojson::value(static_cast<std::int64_t>(_geometryIndex));
        return picojson::value(instructionObj);
    }

    Instruction Instruction::parse(const picojson::value& instructionDef) {
        Type type = static_cast<Type>(instructionDef.get("type").get<std::int64_t>());
        picojson::value tag = instructionDef.get("tag");
        double distance = instructionDef.get("distance").get<double>();
        double time = instructionDef.get("time").get<double>();
        std::size_t geometryIndex = static_cast<std::size_t>(instructionDef.get("geomindex").get<std::int64_t>());
        return Instruction(type, tag, distance, time, geometryIndex);
    }
} }
