#include <mapnikvt/MapParser.h>
#include <mapnikvt/MapGenerator.h>
#include <mapnikvt/SymbolizerParser.h>
#include <mapnikvt/SymbolizerGenerator.h>
#include <mapnikvt/SymbolizerContext.h>

#include "cartocss/CartoCSSMapLoader.h"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>
#include <iostream>

#include <pugixml.hpp>

#include <picojson/picojson.h>

class Logger : public carto::mvt::Logger {
public:
    virtual void write(Severity severity, const std::string& msg) {
        std::cerr << msg << std::endl;
    }
};

class AssetLoader : public carto::css::CartoCSSMapLoader::AssetLoader {
public:
    explicit AssetLoader(const std::string& folder) : _folder(folder) { }

    virtual std::shared_ptr<const std::vector<unsigned char>> load(const std::string& fileNameOrig) const override {
        std::string filePath = _folder + "/";
        filePath.append(fileNameOrig);
        std::FILE* fpRaw = std::fopen(filePath.c_str(), "rb");
        if (fpRaw == NULL) {
            throw std::runtime_error("Failed to open " + fileNameOrig);
        }
        std::shared_ptr<std::FILE> fp(fpRaw, fclose);
        std::fseek(fpRaw, 0, SEEK_END);
        long size = std::ftell(fpRaw);
        if (size < 0) {
            throw std::runtime_error("Failed to load " + fileNameOrig);
        }
        std::fseek(fpRaw, 0, SEEK_SET);
        std::vector<unsigned char> fileData(size);
        std::fread(fileData.data(), sizeof(unsigned char), fileData.size(), fpRaw);
        return std::make_shared<std::vector<unsigned char>>(fileData);
    }

protected:
    const std::string _folder;
};

void compileCartoCSS(const std::string& sourceProjectFile, const std::string& compiledProjectFile) {
    std::string folder = ".";
    std::string file = sourceProjectFile;
    std::string::size_type pos = sourceProjectFile.find_last_of("/\\");
    if (pos != std::string::npos) {
        folder = sourceProjectFile.substr(0, pos);
        file = sourceProjectFile.substr(pos + 1);
    }

    auto logger = std::make_shared<Logger>();
    auto loader = std::make_shared<AssetLoader>(folder);

    carto::css::CartoCSSMapLoader cartoCSSLoader(loader, logger);
    std::shared_ptr<carto::mvt::Map> map = cartoCSSLoader.loadMapProject(file);

    auto symbolizerGenerator = std::make_shared<carto::mvt::SymbolizerGenerator>(logger);
    carto::mvt::MapGenerator mapGen(symbolizerGenerator, logger);
    std::shared_ptr<pugi::xml_document> docPtr = mapGen.generateMap(*map);
    docPtr->save_file(compiledProjectFile.c_str());
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: css2xml input-file output-file" << std::endl;
    }

    try {
        std::string inputFile = argv[1];
        std::string outputFile = argv[2];
        compileCartoCSS(inputFile, outputFile);
    } catch (const std::exception& ex) {
        std::cerr << "Exception while compiling: " << ex.what() << std::endl;
        return -1;
    }
    return 0;
}
