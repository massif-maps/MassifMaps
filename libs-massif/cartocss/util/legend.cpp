#include "legend.h"
#include "common.h"

#include <mapnikvt/Map.h>
#include <mapnikvt/LegendResolver.h>
#include <mapnikvt/StyleParameterStore.h>

#include <cartocss/CartoCSSMapLoader.h>

#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace massif::cssutils {
    namespace {
        // The text an app hands setStyleParameter, read by the declared type as the decoder reads it.
        massif::mvt::Value convertParameter(const massif::mvt::StyleParameter& param, const std::string& text) {
            if (!param.getEnumMap().empty()) {
                auto it = param.getEnumMap().find(text);
                if (it == param.getEnumMap().end()) {
                    throw std::invalid_argument("Illegal enum value for parameter " + param.getName() + ": " + text);
                }
                return it->second;
            }
            const massif::mvt::Value& defaultValue = param.getDefaultValue();
            if (massif::mvt::isContainerValue(defaultValue)) {
                return massif::mvt::valueFromJSON(text);
            }
            if (std::holds_alternative<bool>(defaultValue)) {
                return massif::mvt::Value(text == "true" || text == "1");
            }
            if (std::holds_alternative<long long>(defaultValue)) {
                return massif::mvt::Value(std::stoll(text));
            }
            if (std::holds_alternative<double>(defaultValue)) {
                return massif::mvt::Value(std::stod(text));
            }
            return massif::mvt::Value(text);
        }
    }

    int legendMain(const std::vector<std::string>& args) {
        std::string projectFile, specFile, outFile;
        std::vector<std::string> params;
        for (std::size_t i = 0; i < args.size(); i++) {
            bool hasValue = i + 1 < args.size();
            if (args[i] == "--spec" && hasValue) {
                specFile = args[++i];
            }
            else if (args[i] == "--params" && hasValue) {
                params.push_back(args[++i]);
            }
            else if (args[i] == "--out" && hasValue) {
                outFile = args[++i];
            }
            else {
                projectFile = args[i];
            }
        }
        if (projectFile.empty()) {
            std::cerr << "Usage: legend [--spec legend.json] [--params name=value]... [--out file] input-project-file" << std::endl;
            return -1;
        }

        try {
            auto logger = std::make_shared<Logger>();
            auto [folder, file] = splitProjectPath(projectFile);
            massif::css::CartoCSSMapLoader mapLoader(std::make_shared<AssetLoader>(folder), logger);
            std::shared_ptr<massif::mvt::Map> map = mapLoader.loadMapProject(file);

            std::map<std::string, massif::mvt::Value> values;
            for (const auto& [name, param] : map->getStyleParameterMap()) {
                values[name] = param.getDefaultValue();
            }
            for (const std::string& assignment : params) {
                std::string::size_type pos = assignment.find('=');
                auto it = map->getStyleParameterMap().find(assignment.substr(0, pos));
                if (pos == std::string::npos || it == map->getStyleParameterMap().end()) {
                    std::cerr << "Unknown style parameter: " << assignment << std::endl;
                    return -1;
                }
                values[it->first] = convertParameter(it->second, assignment.substr(pos + 1));
            }

            std::vector<unsigned char> spec = loadFile(specFile.empty() ? folder + "/legend.json" : specFile);
            std::string legend = massif::mvt::resolveLegend(*map, std::string(spec.begin(), spec.end()), std::make_shared<massif::mvt::StyleParameterStore>(std::move(values)));
            if (outFile.empty()) {
                std::cout << legend << std::endl;
            }
            else {
                std::ofstream(outFile) << legend;
            }
        } catch (const std::exception& ex) {
            std::cerr << "Exception while resolving the legend: " << ex.what() << std::endl;
            return -1;
        }
        return 0;
    }
}
