/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_SGRE_RESULT_H_
#define _CARTO_SGRE_RESULT_H_

#include "Base.h"
#include "Instruction.h"

#include <vector>
#include <numeric>

#include <picojson/picojson.h>

namespace carto { namespace sgre {
    class Result final {
    public:
        enum class Status {
            FAILED = 0,
            SUCCESS = 1
        };

        Result() = default;
        explicit Result(std::vector<Instruction> instructions, std::vector<Point> geometry) : _status(Status::SUCCESS), _instructions(std::move(instructions)), _geometry(std::move(geometry)) { }

        Status getStatus() const { return _status; }
        const std::vector<Instruction>& getInstructions() const { return _instructions; }
        const std::vector<Point>& getGeometry() const { return _geometry; }

        double getTotalDistance() const {
            return std::accumulate(_instructions.begin(), _instructions.end(), 0.0, [](double dist, const Instruction& instruction) {
                return dist + instruction.getDistance();
            });
        }

        double getTotalTime() const {
            return std::accumulate(_instructions.begin(), _instructions.end(), 0.0, [](double time, const Instruction& instruction) {
                return time + instruction.getTime();
            });
        }

        picojson::value serialize() const;

        static Result parse(const picojson::value& resultDef);

    private:
        Status _status = Status::FAILED;
        std::vector<Instruction> _instructions;
        std::vector<Point> _geometry;
    };
} }

#endif
