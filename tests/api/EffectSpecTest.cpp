/*
 * The "effect" kind: a post-process effect built from a spec, which is how a binding with only the
 * facade (the web) gets one. Attaching it and its setFloatParameter call need MethodImpls and the
 * renderer, so they are checked in a browser, not here.
 */

#include "api/Context.h"
#include "api/Spec.h"
#include "api/SpecBuilders.h"
#include "renderers/PostProcessEffect.h"

#include <memory>
#include <set>
#include <string>

using namespace massif;
using namespace massif::api;

#include "TestCheck.h"

namespace {

    Result generatedEffect(Context& context, const Variant& spec, ObjectRef& object,
                           std::set<std::string>& consumed) {
        return buildFromConstructor(context, "effect", spec, object, consumed);
    }

}

void testEffectSpec() {
    Spec::registerFactory("effect", &generatedEffect);
    auto context = std::make_shared<Context>();

    Handle effect = NULL_HANDLE;
    TEST_CHECK(Spec::create(*context, "effect", "relief",
                            "{\"type\":\"postprocess\",\"name\":\"relief\","
                            "\"fragmentShader\":\"void main() {}\",\"terrainNormalsRequired\":true}", effect) == RESULT_OK,
               "an effect builds from its constructor");
    PropertyValue value;
    TEST_CHECK(context->getProperty(effect, "name", value) == RESULT_OK && value.stringValue == "relief",
               "with its name");
    TEST_CHECK(context->getProperty(effect, "fragmentShader", value) == RESULT_OK && value.stringValue == "void main() {}",
               "and its shader");
    TEST_CHECK(context->getProperty(effect, "terrainNormalsRequired", value) == RESULT_OK && value.asBool(),
               "a spec key that is not a constructor argument is set as a property");
    TEST_CHECK(context->getProperty(effect, "terrainDepthRequired", value) == RESULT_OK && value.asBool(),
               "and normals imply the depth pre-pass, as the setter documents");

    Handle refused = NULL_HANDLE;
    TEST_CHECK(Spec::create(*context, "effect", "bad", "{\"type\":\"postprocess\",\"name\":\"bad\"}", refused) == RESULT_BAD_SPEC,
               "an effect without a shader is refused");
}
