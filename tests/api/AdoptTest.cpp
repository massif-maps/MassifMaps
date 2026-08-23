/*
 * Tests for adopting an object under an id and reaching it from a spec.
 *
 * The point of MassifApi::adopt(kind, id, AssetPackage) is that a binding's OWN subclass becomes
 * addressable from JSON: a spec's `assets` key that is a string is resolved as an id of kind
 * "assets". MassifApi.cpp itself needs Options, Layers and every source constructor to link, so
 * what is checked here is the half that carries the contract - Context::registerObject plus the
 * childOf lookup the generated builders call.
 *
 * See tests/README.md for what is deliberately out of scope.
 */

#include "api/Context.h"
#include "api/SpecBuilders.h"
#include "core/BinaryData.h"
#include "core/Variant.h"
#include "projections/EPSG3857.h"
#include "utils/AssetPackage.h"

#include <memory>
#include <string>
#include <vector>

using namespace massif;
using namespace massif::api;

#include "TestCheck.h"

namespace {

    /**
     * What a binding's subclass looks like from the SDK's side. A Swig director is registered as
     * the BASE class, because ClassRegistry does not know the director type - so this is exactly
     * the shape adopt() produces for a Java or TypeScript asset package.
     */
    struct FakeAssetPackage : public AssetPackage {
        std::vector<std::string> getAssetNames() const override { return { "style.mss" }; }
        std::shared_ptr<BinaryData> loadAsset(const std::string&) const override {
            return std::make_shared<BinaryData>(std::vector<unsigned char>{ 'b', 'o', 'd', 'y' });
        }
    };

}

void testAdopt() {
    auto context = std::make_shared<Context>();
    auto assets = std::make_shared<FakeAssetPackage>();

    Handle handle = NULL_HANDLE;
    TEST_CHECK(context->registerObject("assets", "shared", assets, "massif::AssetPackage", handle)
                   == RESULT_OK,
               "an app's own AssetPackage adopts under kind assets");

    std::shared_ptr<void> out;
    Variant spec = Variant::FromString("{\"assets\":\"shared\"}");
    TEST_CHECK(childOf(*context, spec, "assets", "assets", "massif::AssetPackage", out) == RESULT_OK,
               "a spec's assets key resolves the adopted id");
    TEST_CHECK(out == std::static_pointer_cast<void>(assets),
               "and resolves to that very object, not a copy");

    out.reset();
    Variant missing = Variant::FromString("{\"assets\":\"nosuch\"}");
    TEST_CHECK(childOf(*context, missing, "assets", "assets", "massif::AssetPackage", out)
                   == RESULT_BAD_HANDLE,
               "an id nobody adopted is refused");

    // The check that stops a wrong handle being cast: ids are per kind, so nothing prevents an app
    // registering something else as "assets" - the CLASS is what has to refuse it.
    Handle wrong = NULL_HANDLE;
    context->registerObject("assets", "notapackage", std::make_shared<EPSG3857>(),
                            "massif::EPSG3857", wrong);
    out.reset();
    Variant mismatched = Variant::FromString("{\"assets\":\"notapackage\"}");
    TEST_CHECK(childOf(*context, mismatched, "assets", "assets", "massif::AssetPackage", out)
                   == RESULT_BAD_HANDLE,
               "an id of the wrong class is refused rather than cast");

    out.reset();
    Variant absent = Variant::FromString("{}");
    TEST_CHECK(childOf(*context, absent, "assets", "assets", "massif::AssetPackage", out)
                   == RESULT_UNKNOWN_PROPERTY,
               "an absent assets key is not an error, so the no-argument overload can be picked");

    // Adoption is by id, so dropping the id drops the reference and a spec written against it
    // fails rather than resurrecting a dead handle.
    TEST_CHECK(context->unregisterObject("assets", "shared"), "the id drops");
    out.reset();
    TEST_CHECK(childOf(*context, spec, "assets", "assets", "massif::AssetPackage", out)
                   == RESULT_BAD_HANDLE,
               "and the spec no longer resolves it");
}
