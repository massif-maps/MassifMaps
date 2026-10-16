/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_MAPNIKVT_MAPGENERATOR_H_
#define _MASSIF_MAPNIKVT_MAPGENERATOR_H_

#include "Value.h"

#include <memory>
#include <string>

#include <pugixml.hpp>

namespace massif::mvt {
    class Map;
    class Logger;
    class SymbolizerGenerator;

    class MapGenerator {
    public:
        explicit MapGenerator(std::shared_ptr<SymbolizerGenerator> generator, std::shared_ptr<Logger> logger) : _symbolizerGenerator(std::move(generator)), _logger(std::move(logger)) { }
        virtual ~MapGenerator() = default;

        virtual std::shared_ptr<pugi::xml_document> generateMap(const Map& map) const;

    protected:
        // Shorter values are not worth a <Define> of their own
        static constexpr std::size_t INTERN_MIN_LENGTH = 40;

        static void internAttributes(pugi::xml_node mapNode);

        std::string generateTypeString(const Value& value) const;
        std::string generateValueString(const Value& value) const;

        const std::shared_ptr<SymbolizerGenerator> _symbolizerGenerator;
        const std::shared_ptr<Logger> _logger;
    };
}

#endif
