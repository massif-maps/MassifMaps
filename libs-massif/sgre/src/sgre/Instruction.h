/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_SGRE_INSTRUCTION_H_
#define _CARTO_SGRE_INSTRUCTION_H_

#include "Base.h"

#include <cstddef>
#include <string>

#include <picojson/picojson.h>

namespace carto { namespace sgre {
    class Instruction final {
    public:
        enum class Type {
            HEAD_ON     = 1,
            GO_STRAIGHT = 2,
            GO_UP       = 3,
            GO_DOWN     = 4,
            TURN_RIGHT  = 5,
            TURN_LEFT   = 6,
            WAIT        = 7,
            REACHED_YOUR_DESTINATION = 8
        };

        Instruction() = default;
        explicit Instruction(Type type, picojson::value tag, double distance, double time, std::size_t geometryIndex) : _type(type), _tag(std::move(tag)), _distance(distance), _time(time), _geometryIndex(geometryIndex) { }

        Type getType() const { return _type; }
        const picojson::value& getTag() const { return _tag; }
        double getDistance() const { return _distance; }
        double getTime() const { return _time; }
        std::size_t getGeometryIndex() const { return _geometryIndex; }

        picojson::value serialize() const;

        static Instruction parse(const picojson::value& instructionDef);
    
    private:
        Type _type = Type::HEAD_ON;
        picojson::value _tag;
        double _distance = 0.0;
        double _time = 0.0;
        std::size_t _geometryIndex = 0;
    };
} }

#endif
