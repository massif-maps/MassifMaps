/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_CSSUTILS_COMMON_H_
#define _MASSIF_CSSUTILS_COMMON_H_

#include <mapnikvt/Logger.h>

#include <cartocss/CartoCSSMapLoader.h>

#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace massif::cssutils {
    class Logger : public massif::mvt::Logger {
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

    inline std::vector<unsigned char> loadFile(const std::string& filePath) {
        std::FILE* fpRaw = std::fopen(filePath.c_str(), "rb");
        if (fpRaw == NULL) {
            throw std::runtime_error("Failed to open " + filePath);
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

    class AssetLoader : public massif::css::CartoCSSMapLoader::AssetLoader {
    public:
        explicit AssetLoader(const std::string& folder) : _folder(folder) { }

        virtual std::shared_ptr<const std::vector<unsigned char>> load(const std::string& fileNameOrig) const override {
            return std::make_shared<std::vector<unsigned char>>(loadFile(_folder + "/" + fileNameOrig));
        }

    protected:
        const std::string _folder;
    };

    // Splits a project path into the folder the assets are resolved against and the file itself,
    // which is what CartoCSSMapLoader expects (it takes an AssetLoader plus a bare file name).
    inline std::pair<std::string, std::string> splitProjectPath(const std::string& projectFile) {
        std::string::size_type pos = projectFile.find_last_of("/\\");
        if (pos == std::string::npos) {
            return { ".", projectFile };
        }
        return { projectFile.substr(0, pos), projectFile.substr(pos + 1) };
    }
}

#endif
