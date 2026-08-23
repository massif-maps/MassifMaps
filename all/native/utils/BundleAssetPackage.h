/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_BUNDLEASSETPACKAGE_H_
#define _MASSIF_BUNDLEASSETPACKAGE_H_

#include "utils/AssetPackage.h"

#include <mutex>

namespace massif {

    /**
     * An asset package based on the assets bundled with the application.
     * On Android these live inside the APK, where no file path reaches them - which is what
     * separates this from DirAssetPackage; on iOS and Windows they are the app bundle's own files.
     * Note: assets and directories with names starting with '.' are ignored.
     */
    class BundleAssetPackage : public AssetPackage {
    public:
        /**
         * Constructs a bundle asset package rooted at the specified bundled directory.
         * @param basePath The path of the directory inside the bundle, "" for the bundle root.
         */
        explicit BundleAssetPackage(const std::string& basePath);
        /**
         * Constructs a bundle asset package rooted at the specified bundled directory, with a
         * fallback asset package.
         * @param basePath The path of the directory inside the bundle, "" for the bundle root.
         * @param baseAssetPackage The base asset package. If an asset is not found in the bundle, base asset package is used.
         */
        BundleAssetPackage(const std::string& basePath, const std::shared_ptr<AssetPackage>& baseAssetPackage);
        virtual ~BundleAssetPackage();

        /**
         * Returns the path of the bundled directory the assets are read from.
         * @return The path of the bundled directory containing the assets.
         */
        std::string getBasePath() const;

        /**
         * Returns the list of assets found in the bundle, ignoring the base asset package.
         * @return The list of asset names found in the bundle.
         */
        std::vector<std::string> getLocalAssetNames() const;

        /**
         * Rescans the bundled directory. Bundled assets do not change while the app runs, so this
         * only matters when the listing was built before the platform could answer - on Android
         * the asset manager is connected by the first MapView.
         */
        void reload();

        virtual std::vector<std::string> getAssetNames() const;

        virtual std::shared_ptr<BinaryData> loadAsset(const std::string& name) const;

    private:
        static void ScanAssets(const std::string& basePath, const std::string& subDir, std::vector<std::string>& assetNames);

        const std::string _basePath;
        const std::shared_ptr<AssetPackage> _baseAssetPackage;

        mutable std::vector<std::string> _localAssetNames;
        mutable bool _localAssetNamesValid;

        mutable std::mutex _mutex;
    };

}

#endif
