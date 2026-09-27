/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_DIRASSETPACKAGE_H_
#define _MASSIF_DIRASSETPACKAGE_H_

#include "utils/AssetPackage.h"

#include <mutex>

namespace massif {

    /**
     * An asset package based on a file system directory. Assets can be modified on the fly
     * without repackaging them into a ZIP archive, which is mostly useful for style development.
     * Note: assets and directories with names starting with '.' are ignored.
     */
    class DirAssetPackage : public AssetPackage {
    public:
        /**
         * Constructs a directory asset package from the specified directory.
         * @param dirPath The full path of the directory containing the assets.
         * @throws std::exception If the directory could not be opened.
         */
        explicit DirAssetPackage(const std::string& dirPath);
        /**
         * Constructs a directory asset package from the specified directory and a fallback asset package.
         * @param dirPath The full path of the directory containing the assets.
         * @param baseAssetPackage The base asset package. If an asset is not found in the directory, base asset package is used.
         * @throws std::exception If the directory could not be opened.
         */
        DirAssetPackage(const std::string& dirPath, const std::shared_ptr<AssetPackage>& baseAssetPackage);
        virtual ~DirAssetPackage();

        /**
         * Returns the full path of the directory containing the assets.
         * @return The full path of the directory containing the assets.
         */
        std::string getDirPath() const;

        /**
         * Returns the list of assets stored in the directory, ignoring the base asset package.
         * @return The list of asset names stored in the directory.
         */
        std::vector<std::string> getLocalAssetNames() const;

        /**
         * Rescans the directory. Use this if assets were added to or removed from the directory
         * after the asset package was created.
         */
        void reload();

        virtual std::vector<std::string> getAssetNames() const;

        virtual std::shared_ptr<BinaryData> loadAsset(const std::string& name) const;

    private:
        static bool IsDirectory(const std::string& path);
        static void ScanDir(const std::string& dirPath, const std::string& subDir, std::vector<std::string>& assetNames);

        const std::string _dirPath;
        const std::shared_ptr<AssetPackage> _baseAssetPackage;

        mutable std::vector<std::string> _localAssetNames;
        mutable bool _localAssetNamesValid;

        mutable std::mutex _mutex;
    };

}

#endif
