/*
 * A HOST declaration of the platform AssetUtils, so the shared BundleAssetPackage can be tested
 * off a device. The real ones are android/native/utils, ios/native/utils and winphone/native/utils;
 * only the three entry points BundleAssetPackage uses are declared, and the test defines them.
 *
 * Keep the signatures in step with those three - a mismatch is a link error here, which is the
 * point: this file is what notices when one platform's contract drifts.
 */

#ifndef _MASSIF_ASSETUTILS_H_
#define _MASSIF_ASSETUTILS_H_

#include <memory>
#include <string>
#include <vector>

namespace massif {
    class BinaryData;

    class AssetUtils {
    public:
        static std::shared_ptr<BinaryData> LoadAsset(const std::string& path);
        static bool AssetExists(const std::string& path);
        static std::vector<std::string> ListAssets(const std::string& path);

    private:
        AssetUtils();
    };

}

#endif
