#include "mbvtbuilder/MBVTTileBuilder.h"

#include <memory>
#include <stdexcept>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>

#include <picojson/picojson.h>

void createTiles(const std::string& geoJSONFile, const std::string& outputDir, int minZoom, int maxZoom) {
    std::ifstream ifs;
    ifs.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    ifs.open(geoJSONFile);
    picojson::value geoJSON;
    if (auto err = picojson::parse(geoJSON, ifs); !err.empty()) {
        throw std::runtime_error("GeoJSON parsing failed: " + err);
    }
    ifs.close();

    carto::mbvtbuilder::MBVTTileBuilder builder(minZoom, maxZoom);
    builder.importGeoJSON(builder.createLayer("layer"), geoJSON);
    builder.buildTiles([&outputDir](int zoom, int tileX, int tileY, const protobuf::encoded_message& msg) {
        std::string fileName = "tile" + std::to_string(zoom) + "_" + std::to_string(tileX) + "_" + std::to_string(tileY) + ".mvt";
        std::ofstream ofs;
        ofs.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        ofs.open(outputDir / std::filesystem::path(fileName), std::ios_base::out | std::ios_base::binary);
        ofs << msg.data();
        ofs.close();
    });
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cerr << "Usage: geojson2mvt input-file output-dir minzoom maxzoom" << std::endl;
    }

    try {
        std::string inputFile = argv[1];
        std::string outputDir = argv[2];
        int minZoom = std::stoi(argv[3]);
        int maxZoom = std::stoi(argv[4]);
        createTiles(inputFile, outputDir, minZoom, maxZoom);
    } catch (const std::exception& ex) {
        std::cerr << "Exception while creating tiles: " << ex.what() << std::endl;
        return -1;
    }
    return 0;
}
