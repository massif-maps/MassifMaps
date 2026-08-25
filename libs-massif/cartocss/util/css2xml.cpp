#include "css2xml.h"
#include "common.h"

#include <mapnikvt/MapParser.h>
#include <mapnikvt/MapGenerator.h>
#include <mapnikvt/SymbolizerParser.h>
#include <mapnikvt/SymbolizerGenerator.h>
#include <mapnikvt/SymbolizerContext.h>

#include <cartocss/CartoCSSMapLoader.h>

#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>

#include <pugixml.hpp>

namespace massif::cssutils {
    namespace {
        std::shared_ptr<pugi::xml_document> generateXML(const massif::mvt::Map& map, const std::shared_ptr<Logger>& logger) {
            auto symbolizerGenerator = std::make_shared<massif::mvt::SymbolizerGenerator>(logger);
            massif::mvt::MapGenerator mapGen(symbolizerGenerator, logger);
            return mapGen.generateMap(map);
        }

        std::string serializeXML(const pugi::xml_document& doc) {
            std::ostringstream os;
            doc.save(os);
            return os.str();
        }

        std::shared_ptr<massif::mvt::Map> compileCartoCSS(const std::string& sourceProjectFile, const std::shared_ptr<Logger>& logger) {
            auto [folder, file] = splitProjectPath(sourceProjectFile);
            auto loader = std::make_shared<AssetLoader>(folder);
            massif::css::CartoCSSMapLoader cartoCSSLoader(loader, logger);
            return cartoCSSLoader.loadMapProject(file);
        }

        /**
         * Reads the compiled XML back and compiles it again. The two must be identical: anything the XML
         * parser cannot read - a symbolizer type it has no case for, an operator missing from the
         * expression grammar, a Map setting only one side knows - shows up here as a diff, where writing
         * the file alone reports nothing. Returns false on a mismatch.
         */
        bool checkRoundTrip(const pugi::xml_document& doc, const std::shared_ptr<Logger>& logger) {
            auto symbolizerParser = std::make_shared<massif::mvt::SymbolizerParser>(logger);
            massif::mvt::MapParser mapParser(symbolizerParser, logger);
            std::shared_ptr<massif::mvt::Map> map = mapParser.parseMap(doc);

            std::string before = serializeXML(doc);
            std::string after = serializeXML(*generateXML(*map, logger));
            if (before == after) {
                return true;
            }

            std::istringstream beforeStream(before), afterStream(after);
            std::string beforeLine, afterLine;
            for (int line = 1; std::getline(beforeStream, beforeLine); line++) {
                if (!std::getline(afterStream, afterLine) || beforeLine != afterLine) {
                    std::cerr << "Round-trip mismatch at line " << line << ":" << std::endl;
                    std::cerr << "  compiled: " << beforeLine << std::endl;
                    std::cerr << "  reparsed: " << afterLine << std::endl;
                    break;
                }
            }
            return false;
        }
    }

    int css2xmlMain(const std::vector<std::string>& args) {
        std::vector<std::string> files;
        bool roundTrip = false;
        for (const std::string& arg : args) {
            if (arg == "--roundtrip") {
                roundTrip = true;
            }
            else {
                files.push_back(arg);
            }
        }
        if (files.size() < 2) {
            std::cerr << "Usage: css2xml [--roundtrip] input-file output-file" << std::endl;
            return -1;
        }

        try {
            auto logger = std::make_shared<Logger>();
            std::shared_ptr<massif::mvt::Map> map = compileCartoCSS(files[0], logger);
            std::shared_ptr<pugi::xml_document> docPtr = generateXML(*map, logger);
            docPtr->save_file(files[1].c_str());
            if (roundTrip && !checkRoundTrip(*docPtr, logger)) {
                return -1;
            }
        } catch (const std::exception& ex) {
            std::cerr << "Exception while compiling: " << ex.what() << std::endl;
            return -1;
        }
        return 0;
    }
}
