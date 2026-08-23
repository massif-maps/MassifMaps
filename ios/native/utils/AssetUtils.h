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
     */
    class AssetUtils {
    public:
        /**
         * Loads the specified bundled resource.
         * @param path The path of the resource to load. The path is relative to application root folder.
         * @return The loaded resource as a byte vector or null if the resource was not found or could not be loaded.
         */
        static std::shared_ptr<BinaryData> LoadAsset(const std::string& path);

        /**
         * Returns true if the specified bundled asset exists and is a FILE (not a directory).
         * Unlike loadAsset this does not read or log anything, so it can be used to probe.
         * @param path The path of the asset, relative to the application root folder.
         * @return True if the asset exists as a file.
         */
        static bool AssetExists(const std::string& path);

        /**
         * Lists the names of the assets directly inside the specified bundled directory. The
         * listing is NOT recursive and does not tell files and directories apart - an entry is a
         * directory if listing it returns something, a file if assetExists returns true for it.
         * The contract matches Android's, where the NDK asset API cannot do better.
         * @param path The directory path, relative to the application root folder ("" is the root).
         * @return The names of the entries in that directory, without any path prefix.
         */
        static std::vector<std::string> ListAssets(const std::string& path);

        /**
         * Calculates path for the bundled resource.
         * @param resourceName The name of the resource.
         * @return The full path for the resource. Result will be empty string if the resource was not found.
         */
        static std::string CalculateResourcePath(const std::string& resourceName);
        
        /**
         * Calculates writable path for the given file name.
         * @param fileName The file name to use.
         * @return The full path for the given file name.
         */
        static std::string CalculateWritablePath(const std::string& fileName);

    private:
        AssetUtils();
    };

}

#endif
