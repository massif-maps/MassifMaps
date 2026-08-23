/*
 * Tests for BundleAssetPackage over a FAKE platform asset layer.
 *
 * The three AssetUtils entry points are defined here rather than linked, because the real ones are
 * per-platform (an APK on Android, the bundle directory on iOS and Windows). What that buys is the
 * part worth checking: the recursive walk over a listing that cannot tell a file from a directory,
 * which is the contract the NDK asset API forces on all three.
 *
 * NOT covered: that any platform's AssetUtils actually answers this way. That is a device check.
 */

#include "core/BinaryData.h"
#include "utils/AssetPackage.h"
#include "utils/AssetUtils.h"
#include "utils/BundleAssetPackage.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace massif;

#include "TestCheck.h"

namespace {

    /** Full path -> contents. A directory is whatever is a PREFIX of one of these. */
    std::map<std::string, std::string> g_files;
    /** Paths LoadAsset was asked for, so a test can assert the base package was not consulted. */
    std::vector<std::string> g_loaded;

    std::vector<std::string> sorted(std::vector<std::string> names) {
        std::sort(names.begin(), names.end());
        return names;
    }

    std::string join(const std::vector<std::string>& names) {
        std::string out;
        for (const std::string& name : names) {
            out += (out.empty() ? "" : ",") + name;
        }
        return out;
    }

    /** A base package, to check the fallback and the precedence between the two. */
    struct StubPackage : public AssetPackage {
        std::vector<std::string> getAssetNames() const override {
            return { "fonts/sans.ttf", "shared.mss" };
        }
        std::shared_ptr<BinaryData> loadAsset(const std::string& name) const override {
            if (name == "fonts/sans.ttf" || name == "shared.mss") {
                return std::make_shared<BinaryData>(std::vector<unsigned char>{ 'b' });
            }
            return std::shared_ptr<BinaryData>();
        }
    };

}

namespace massif {

    std::shared_ptr<BinaryData> AssetUtils::LoadAsset(const std::string& path) {
        g_loaded.push_back(path);
        auto it = g_files.find(path);
        if (it == g_files.end()) {
            return std::shared_ptr<BinaryData>();
        }
        return std::make_shared<BinaryData>(
            std::vector<unsigned char>(it->second.begin(), it->second.end()));
    }

    bool AssetUtils::AssetExists(const std::string& path) {
        return g_files.count(path) != 0;
    }

    std::vector<std::string> AssetUtils::ListAssets(const std::string& path) {
        // The platform contract: one level, files and directories mixed, no way to tell them apart.
        std::string prefix = path.empty() ? "" : path + "/";
        std::set<std::string> names;
        for (const auto& file : g_files) {
            if (file.first.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
            std::string rest = file.first.substr(prefix.size());
            std::string::size_type slash = rest.find('/');
            names.insert(slash == std::string::npos ? rest : rest.substr(0, slash));
        }
        return std::vector<std::string>(names.begin(), names.end());
    }

}

void testBundleAssets() {
    g_files = {
        { "styles/osm/style.mss", "main" },
        { "styles/osm/layers/water.mss", "water" },
        { "styles/osm/.hidden", "no" },
        { "styles/osm/symbols/icons/pin.svg", "pin" },
        { "styles/other/style.mss", "other" },
        { "top.mss", "top" },
    };
    g_loaded.clear();

    BundleAssetPackage package("styles/osm");
    TEST_CHECK(package.getBasePath() == "styles/osm", "the base path survives normalization");
    TEST_CHECK(join(sorted(package.getLocalAssetNames()))
                   == "layers/water.mss,style.mss,symbols/icons/pin.svg",
               "the walk recurses two levels, skips '.hidden', and stays inside the base path");
    TEST_CHECK(join(sorted(package.getAssetNames()))
                   == "layers/water.mss,style.mss,symbols/icons/pin.svg",
               "with no base package the two listings agree");

    auto data = package.loadAsset("layers/water.mss");
    TEST_CHECK(data && data->size() == 5, "a nested asset loads");
    TEST_CHECK(!package.loadAsset("nope.mss"), "a missing one is null, not a throw");
    TEST_CHECK(!package.loadAsset("../other/style.mss"),
               "a name escaping the base path is refused rather than resolved");
    TEST_CHECK(std::find(g_loaded.begin(), g_loaded.end(), "styles/other/style.mss") == g_loaded.end(),
               "and the escape never reaches the platform");

    // A listing built before the platform can answer - on Android the asset manager is connected by
    // the first MapView - has to be droppable, or the package is empty for the life of the process.
    g_files.clear();
    BundleAssetPackage early("styles/osm");
    TEST_CHECK(early.getLocalAssetNames().empty(), "a package built too early lists nothing");
    g_files = { { "styles/osm/style.mss", "main" } };
    TEST_CHECK(early.getLocalAssetNames().empty(), "and the empty listing is cached");
    early.reload();
    TEST_CHECK(join(early.getLocalAssetNames()) == "style.mss", "reload picks the assets up");

    // The base package: local wins, base fills the gaps. This is the SDK's precedence, and it is
    // the OPPOSITE of asking the base first - a style overriding a shared font has to win.
    g_files = { { "styles/osm/shared.mss", "local" } };
    BundleAssetPackage layered("styles/osm", std::make_shared<StubPackage>());
    TEST_CHECK(join(sorted(layered.getAssetNames())) == "fonts/sans.ttf,shared.mss",
               "the two listings merge without duplicating a shared name");
    auto shared = layered.loadAsset("shared.mss");
    TEST_CHECK(shared && shared->size() == 5, "a name in both resolves to the LOCAL one");
    TEST_CHECK(layered.loadAsset("fonts/sans.ttf"), "a name only the base has falls through");
    TEST_CHECK(!layered.loadAsset("neither.mss"), "a name in neither is null");
}
