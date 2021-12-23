#include <mapnikvt/MapParser.h>
#include <mapnikvt/MapGenerator.h>
#include <mapnikvt/SymbolizerParser.h>
#include <mapnikvt/SymbolizerGenerator.h>
#include <mapnikvt/SymbolizerContext.h>
#include <mapnikvt/MBVTFeatureDecoder.h>
#include <mapnikvt/MBVTTileReader.h>

#include <cartocss/CartoCSSMapLoader.h>

#include <vt/Transform.h>

#include "vt/TileSerialization.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include <cassert>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>
#include <iostream>
#include <fstream>
#include <regex>
#include <filesystem>

#include <pugixml.hpp>

#include <utf8.h>

static std::string u8Path(const std::filesystem::path& path) {
    auto wpath = std::wstring(path.c_str());
    std::string u8path;
    utf8::utf32to8(wpath.begin(), wpath.end(), std::back_inserter(u8path));
    return u8path;
}

static std::vector<unsigned char> loadFile(const std::string& filePath) {
    std::FILE* fpRaw = std::fopen(filePath.c_str(), "rb");
    if (fpRaw == NULL) {
        throw std::runtime_error("Failed to open input file " + filePath);
    }
    std::shared_ptr<std::FILE> fp(fpRaw, std::fclose);
    std::fseek(fpRaw, 0, SEEK_END);
    long size = std::ftell(fpRaw);
    if (size < 0) {
        throw std::runtime_error("Failed to load " + filePath);
    }
    std::fseek(fpRaw, 0, SEEK_SET);
    std::vector<unsigned char> fileData(size);
    std::fread(fileData.data(), sizeof(unsigned char), fileData.size(), fpRaw);
    return fileData;
}

static void saveFile(const std::string& filePath, const std::vector<unsigned char>& fileData) {
    std::FILE* fpRaw = std::fopen(filePath.c_str(), "wb");
    if (fpRaw == NULL) {
        throw std::runtime_error("Failed to open output file " + filePath);
    }
    std::shared_ptr<std::FILE> fp(fpRaw, std::fclose);
    std::fwrite(fileData.data(), sizeof(unsigned char), fileData.size(), fpRaw);
}

class Logger : public carto::mvt::Logger {
public:
    virtual void write(Severity severity, const std::string& msg) override {
        if (severity == Severity::WARNING) {
            std::cerr << "Warning: " << msg << std::endl;
        }
        else if (severity != Severity::INFO) {
            std::cerr << "Error: " << msg << std::endl;
        }
    }
};

class AssetLoader : public carto::css::CartoCSSMapLoader::AssetLoader {
public:
    explicit AssetLoader(const std::string& folder) : _folder(folder) { }

    virtual std::shared_ptr<const std::vector<unsigned char>> load(const std::string& fileNameOrig) const override {
        std::string filePath = _folder + "/";
        filePath.append(fileNameOrig);
        std::vector<unsigned char> fileData = loadFile(filePath);
        return std::make_shared<std::vector<unsigned char>>(fileData);
    }

protected:
    const std::string _folder;
};

class BitmapLoader : public carto::vt::BitmapManager::BitmapLoader {
public:
    BitmapLoader(const std::shared_ptr<AssetLoader>& assetLoader) : _assetLoader(assetLoader) { }
    
    virtual std::shared_ptr<const carto::vt::Bitmap> load(const std::string& fileNameOrig, float& resolution) const override {
        auto fileData = _assetLoader->load(fileNameOrig);

        int width = 0, height = 0, comp = 0;
        auto buf = stbi_load_from_memory(fileData->data(), static_cast<int>(fileData->size()), &width, &height, &comp, 4);
        std::vector<std::uint32_t> data(width * height);
        std::memcpy(data.data(), buf, width * height * 4);
        stbi_image_free(buf);

        resolution = 1.0f;
        return std::make_shared<carto::vt::Bitmap>(width, height, std::move(data));
    }

protected:
    const std::shared_ptr<AssetLoader> _assetLoader;
};

class MVTSerializer {
public:
    MVTSerializer(const std::string& sourceProjectFile) {
        std::string folder = ".";
        std::string file = sourceProjectFile;
        std::string::size_type pos = sourceProjectFile.find_last_of("/\\");
        if (pos != std::string::npos) {
            folder = sourceProjectFile.substr(0, pos);
            file = sourceProjectFile.substr(pos + 1);
        }

        _logger = std::make_shared<Logger>();
        _loader = std::make_shared<AssetLoader>(folder);

        carto::css::CartoCSSMapLoader cartoCSSLoader(_loader, _logger);
        _map = cartoCSSLoader.loadMapProject(file);

        std::map<std::string, carto::mvt::Value> nutiParams; 
        for (auto it = _map->getNutiParameterMap().begin(); it != _map->getNutiParameterMap().end(); it++) {
            nutiParams[it->first] = it->second.getDefaultValue();
        }
        auto bitmapManager = std::make_shared<carto::vt::BitmapManager>(std::make_shared<BitmapLoader>(_loader));
        auto strokeMap = std::make_shared<carto::vt::StrokeMap>(128, 512);
        auto glyphMap = std::make_shared<carto::vt::GlyphMap>(1024, 1024);
        carto::mvt::SymbolizerContext::Settings settings(256, nutiParams, {});
        auto fontManager = std::make_shared<carto::vt::FontManager>(1024, 1024);
        _context = std::make_shared<carto::mvt::SymbolizerContext>(bitmapManager, fontManager, strokeMap, glyphMap, settings);

        std::string fontPrefix = _map->getSettings().fontDirectory;
        if (fontPrefix.size() > 0 && fontPrefix[fontPrefix.size() - 1] != '/') {
            fontPrefix += "/";
        }
        for (const auto& entry : std::filesystem::directory_iterator(fontPrefix)) {
            auto fontPath = u8Path(entry.path());
            fontManager->loadFontData(loadFile(fontPath));
        }

        _transformer = std::make_shared<carto::vt::DefaultTileTransformer>(1.0f);
    }

    void serializeTile(const carto::vt::TileId& tileId, const carto::vt::TileId& targetTileId, const std::string& sourceFilePath, const std::string& destFilePath) {
        auto tileData = loadFile(sourceFilePath);
        carto::mvt::MBVTFeatureDecoder decoder(tileData, _logger);
        decoder.setTransform(calculateTileTransform(tileId, targetTileId));

        carto::mvt::MBVTTileReader reader(_map, _transformer, *_context, decoder, _logger);
        auto vectorTile = reader.readTile(targetTileId);

        std::ofstream ofs(destFilePath.c_str());
        if (!ofs.good()) {
            throw std::runtime_error("Failed to create output file " + destFilePath);
        }
        boost::archive::xml_oarchive oa(ofs);
        oa << boost::serialization::make_nvp("vector_tile", *vectorTile);
    }

protected:
    static cglib::mat3x3<float> calculateTileTransform(const carto::vt::TileId& tileId, const carto::vt::TileId& targetTileId) {
        assert(targetTileId.zoom >= tileId.zoom && targetTileId.intersects(tileId));

        float x0 = 0, y0 = 0, scale = 1;
        for (carto::vt::TileId parentTileId = targetTileId; parentTileId != tileId; parentTileId = parentTileId.getParent()) {
            x0 *= 0.5f; y0 *= 0.5f; scale *= 2;
            for (int i = 0; i < 4; i++) {
                if (parentTileId == parentTileId.getParent().getChild(i % 2, i / 2)) {
                    x0 += (i % 2) * 0.5f; y0 += (i / 2) * 0.5f;
                    break;
                }
            }
        }
        return cglib::scale3_matrix(cglib::vec3<float>(scale, scale, 1)) * cglib::translate3_matrix(cglib::vec3<float>(-x0, -y0, 1));
    }

    std::shared_ptr<Logger> _logger;
    std::shared_ptr<AssetLoader> _loader;
    std::shared_ptr<carto::mvt::Map> _map;
    std::shared_ptr<carto::mvt::SymbolizerContext> _context;
    std::shared_ptr<carto::vt::TileTransformer> _transformer;
};

void processTiles(const std::string& sourceProjectFile, const std::string& inputTileDir, const std::string& outputTileDir) {
    MVTSerializer serializer(sourceProjectFile);

    for (const auto& entry : std::filesystem::directory_iterator(inputTileDir)) {
        auto inPath = u8Path(entry.path());
        
        std::regex re("^.*[^0-9]([0-9]+)_([0-9]+)_([0-9]+).pbf$");
        std::smatch m;
        if (std::regex_search(inPath, m, re)) {
            auto outPath = u8Path(std::filesystem::path(outputTileDir) / (entry.path().filename().string() + ".xml"));

            int zoom = std::stoi(m[1].str());
            int x = std::stoi(m[2].str());
            int y = std::stoi(m[3].str());

            carto::vt::TileId tileId(zoom, x, y);
            serializer.serializeTile(tileId, tileId, inPath, outPath);

            std::cout << "Processed " << inPath << std::endl;
        }
        else {
            std::cout << "Ignoring " << inPath << std::endl;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: mvt2xml source-project-file input-tile-dir output-tile-dir" << std::endl;
    }

    try {
        std::string sourceProjectFile = argv[1];
        std::string inputTileDir = argv[2];
        std::string outputTileDir = argv[3];
        processTiles(sourceProjectFile, inputTileDir, outputTileDir);
    } catch (const std::exception& ex) {
        std::cerr << "Exception while processing: " << ex.what() << std::endl;
        return -1;
    }
    return 0;
}
