/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_ASSETUTILS_H_
#define _MASSIF_ASSETUTILS_H_

#include <memory>
#include <string>
#include <vector>

namespace massif {
    class BinaryData;

    /**
     * A helper class for managing application-bundled assets.
     *
     * On the web there is no bundle: assets live in the emscripten virtual filesystem, under a
     * root the host page fills (--preload-file, or FS.writeFile from JavaScript). Paths are
     * resolved against that root, so an asset path means the same thing here as on a device.
     */
    class AssetUtils {
    public:
        /**
         * Sets the virtual filesystem directory that asset paths are resolved against.
         * Defaults to "/assets".
         * @param root The directory path, without a trailing slash.
         */
        static void SetAssetRoot(const std::string& root);

        /**
         * Loads the specified bundled asset.
         * @param path The path of the asset to load. The path is relative to the asset root.
         * @return The loaded asset as a byte vector or null if the asset was not found or could not be loaded.
         */
        static std::shared_ptr<BinaryData> LoadAsset(const std::string& path);

        /**
         * Returns true if the specified bundled asset exists and is a FILE (not a directory).
         * Unlike loadAsset this does not read or log anything, so it can be used to probe.
         * @param path The path of the asset, relative to the asset root.
         * @return True if the asset exists as a file.
         */
        static bool AssetExists(const std::string& path);

        /**
         * Lists the names of the assets directly inside the specified bundled directory. The
         * listing is NOT recursive and does not tell files and directories apart - an entry is a
         * directory if listing it returns something, a file if assetExists returns true for it.
         * @param path The directory path, relative to the asset root ("" is the root).
         * @return The names of the entries in that directory, without any path prefix.
         */
        static std::vector<std::string> ListAssets(const std::string& path);

    private:
        AssetUtils();

        static std::string GetFullPath(const std::string& path);
    };

}

#endif
