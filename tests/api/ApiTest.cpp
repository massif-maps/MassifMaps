/*
 * Tests for the facade API's property table, handle table and registry.
 * See tests/README.md for what is deliberately out of scope.
 */

#include "api/Context.h"
#include "api/Spec.h"
#include "api/PropertyTable.h"
#include "components/FogOptions.h"
#include "graphics/Color.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace massif;
using namespace massif::api;

#include "TestCheck.h"

int failures = 0;

void testEvents();
void testDelivery();
void testStructCodec();
void testMoreStructs();
void testVariantPaths();
void testFeaturePos();
void testProjections();
void testEventProjection();
void testObjectWrites();
void testCallArgs();
void testCall();
void testCallAsync();
void testCallCancel();
void testCallConcurrency();
void testCollections();
void testRouting();
void testStatics();
void testGeneratedFactories();
void testCAbi();
void testCAbiEvents();

namespace {

    /** Records the option-changed names, so a test can assert the setter really ran. */
    struct Watcher : public FogOptions::OnChangeListener {
        std::vector<std::string> seen;
        void onFogOptionChanged(const std::string& name) override { seen.push_back(name); }
    };

    void testTable() {
        const ClassEntry* fog = findClass("massif::FogOptions");
        TEST_CHECK(fog != nullptr, "the generated table has FogOptions");
        TEST_CHECK(findClass("massif::NoSuchClass") == nullptr, "an unknown class does not resolve");

        const PropertyEntry* rangeStart = findProperty(fog, "rangeStart");
        TEST_CHECK(rangeStart && rangeStart->type == PT_FLOAT, "rangeStart is a float");
        TEST_CHECK(rangeStart && rangeStart->setter, "rangeStart has a setter");
        TEST_CHECK(findProperty(fog, "color") && findProperty(fog, "color")->type == PT_COLOR,
                   "a Color property is typed as COLOR, not STRUCT");
        TEST_CHECK(findProperty(fog, "nope") == nullptr, "an unknown property does not resolve");
        TEST_CHECK(findProperty(nullptr, "rangeStart") == nullptr, "a null class does not crash");
    }

    void testValues(const std::shared_ptr<Context>& context) {
        auto options = std::make_shared<FogOptions>();
        auto watcher = std::make_shared<Watcher>();
        options->registerOnChangeListener(watcher);

        Handle handle = NULL_HANDLE;
        TEST_CHECK(context->registerObject("options", "fog", options, "massif::FogOptions", handle) == RESULT_OK,
                   "an object registers");
        TEST_CHECK(handle != NULL_HANDLE, "and gets a non-null handle");

        PropertyValue value = PropertyValue::ofDouble(1.25);
        TEST_CHECK(context->setProperty(handle, "rangeStart", value) == RESULT_OK, "set a float");
        TEST_CHECK(options->getRangeStart() == 1.25f, "the object has the new value");
        // The load-bearing one: the generated thunk calls the class' own setter, so the redraw
        // granularity is inherited rather than reimplemented.
        TEST_CHECK(watcher->seen.size() == 1 && watcher->seen[0] == "RangeStart",
                   "exactly one change notification, correctly named");

        PropertyValue readBack;
        TEST_CHECK(context->getProperty(handle, "rangeStart", readBack) == RESULT_OK &&
                   readBack.floatValue == 1.25, "get returns what was set");

        value = PropertyValue::ofBool(false);
        TEST_CHECK(context->setProperty(handle, "enabled", value) == RESULT_OK, "set a bool");
        TEST_CHECK(!options->isEnabled(), "the bool took effect");

        value = PropertyValue::ofLong(0xFF804020);
        TEST_CHECK(context->setProperty(handle, "color", value) == RESULT_OK, "set a colour");
        TEST_CHECK(context->getProperty(handle, "color", readBack) == RESULT_OK &&
                   readBack.intValue == 0xFF804020, "a colour round-trips as ARGB");

        // Reading across types has to coerce: without the stamped type a bool read as a float is
        // 0 and indistinguishable from a real 0.
        TEST_CHECK(context->getProperty(handle, "enabled", readBack) == RESULT_OK &&
                   readBack.type == PT_BOOL && readBack.asDouble() == 0 && !readBack.asBool(),
                   "a false bool reads as 0 through every accessor");
        context->setProperty(handle, "enabled", PropertyValue::ofBool(true));
        TEST_CHECK(context->getProperty(handle, "enabled", readBack) == RESULT_OK &&
                   readBack.asDouble() == 1 && readBack.asLong() == 1 && readBack.asBool(),
                   "a true bool reads as 1 through every accessor");
        TEST_CHECK(context->getProperty(handle, "rangeStart", readBack) == RESULT_OK &&
                   readBack.type == PT_FLOAT && readBack.asLong() == 1,
                   "a float reads as a truncated integer");

        // ...and writing across types has to coerce the same way, or setFloat on a bool writes false.
        value = PropertyValue::ofDouble(0);
        TEST_CHECK(context->setProperty(handle, "enabled", value) == RESULT_OK &&
                   !options->isEnabled(), "a float 0 written to a bool is false");
        value.floatValue = 1;
        TEST_CHECK(context->setProperty(handle, "enabled", value) == RESULT_OK &&
                   options->isEnabled(), "a float 1 written to a bool is true");
        value = PropertyValue::ofLong(2);
        TEST_CHECK(context->setProperty(handle, "rangeStart", value) == RESULT_OK &&
                   options->getRangeStart() == 2.0f, "an integer written to a float converts");

        // ...and so does text, or a binding with only strings - a C caller, a URL query, a
        // scripting language - writes 0 over a real value.
        TEST_CHECK(context->setProperty(handle, "rangeStart", PropertyValue::ofString("3.5")) == RESULT_OK &&
                   options->getRangeStart() == 3.5f, "a numeric string written to a float parses");
        TEST_CHECK(context->setProperty(handle, "enabled", PropertyValue::ofString("false")) == RESULT_OK &&
                   !options->isEnabled(), "\"false\" written to a bool is false, not truthy text");
        TEST_CHECK(context->setProperty(handle, "enabled", PropertyValue::ofString("1")) == RESULT_OK &&
                   options->isEnabled(), "and \"1\" is true");
        TEST_CHECK(context->setProperty(handle, "rangeStart", PropertyValue::ofString("abc")) == RESULT_OK &&
                   options->getRangeStart() == 0.0f, "garbage reads as 0, like every other bad conversion");
        context->setProperty(handle, "rangeStart", PropertyValue::ofDouble(1));

        // The other direction: a number read as text renders rather than coming back empty.
        TEST_CHECK(PropertyValue::ofDouble(2.5).asString() == "2.5" &&
                   PropertyValue::ofLong(7).asString() == "7" &&
                   PropertyValue::ofBool(true).asString() == "true", "asString renders every field");

        value = PropertyValue();
        TEST_CHECK(context->setProperty(handle, "nope", value) == RESULT_UNKNOWN_PROPERTY,
                   "an unknown property is reported, not applied");
        TEST_CHECK(context->setProperty(handle + 7777, "rangeStart", value) == RESULT_BAD_HANDLE,
                   "a handle that was never issued is rejected");
    }

    void testPaths(const std::shared_ptr<Context>& context) {
        // Options -> FogOptions cannot be linked standalone, so the happy path is checked on a
        // device. These are the failure modes, which do not need a traversable class.
        Handle handle = context->findObject("options", "fog");
        PropertyValue value = PropertyValue::ofDouble(1);
        TEST_CHECK(context->setProperty(handle, "enabled.foo", value) == RESULT_NOT_TRAVERSABLE,
                   "a dot into a scalar is not traversable");
        TEST_CHECK(context->setProperty(handle, "nosuch.foo", value) == RESULT_UNKNOWN_PROPERTY,
                   "an unknown intermediate segment is reported");
        TEST_CHECK(context->setProperty(handle, "a.b.c", value) == RESULT_UNKNOWN_PROPERTY,
                   "a deep unknown path is reported");
    }

    /**
     * A factory for a kind that needs no SDK constructor, so create() can be tested without
     * linking every source and layer type. This is the same hook a plugin would use.
     */
    Result fakeFactory(Context&, const Variant& spec, ObjectRef& object,
                       std::set<std::string>& consumed) {
        consumed.insert("type");
        if (spec.containsObjectKey("fail")) {
            return RESULT_UNKNOWN_TYPE;
        }
        object.obj = std::make_shared<FogOptions>();
        object.cppClass = "massif::FogOptions";
        return RESULT_OK;
    }

    void testCreate(const std::shared_ptr<Context>& context) {
        Spec::registerFactory("fake", &fakeFactory);

        Handle handle = NULL_HANDLE;
        const std::string spec = "{\"type\":\"fog\",\"rangeStart\":2.5}";
        TEST_CHECK(Spec::create(*context, "fake", "a", spec, handle) == RESULT_OK, "create from a spec");

        // The key the factory did not consume is applied through the property table, which is what
        // keeps adding an option free.
        PropertyValue value;
        TEST_CHECK(context->getProperty(handle, "rangeStart", value) == RESULT_OK &&
                   value.floatValue == 2.5, "an unconsumed key is applied as a property");

        // Identical spec reuses; a different one under the same id is a conflict, not a replace.
        Handle again = NULL_HANDLE;
        TEST_CHECK(Spec::create(*context, "fake", "a", spec, again) == RESULT_OK && again == handle,
                   "an identical spec reuses the object");
        Handle reordered = NULL_HANDLE;
        TEST_CHECK(Spec::create(*context, "fake", "a", "{\"rangeStart\":2.5,\"type\":\"fog\"}", reordered) ==
                   RESULT_OK && reordered == handle, "key order does not make it a different spec");
        Handle conflict = NULL_HANDLE;
        TEST_CHECK(Spec::create(*context, "fake", "a", "{\"type\":\"other\"}", conflict) == RESULT_DUPLICATE_ID,
                   "a different spec under that id is refused");

        // Tolerant parsing: a key the SDK does not know is dropped, and the object is still built.
        Handle tolerant = NULL_HANDLE;
        TEST_CHECK(Spec::create(*context, "fake", "b", "{\"type\":\"fog\",\"noSuchOption\":1,\"rangeStart\":3}",
                                tolerant) == RESULT_OK, "an unknown key does not fail the create");
        TEST_CHECK(context->getProperty(tolerant, "rangeStart", value) == RESULT_OK && value.floatValue == 3,
                   "and the keys it does know still applied");

        Handle bad = NULL_HANDLE;
        TEST_CHECK(Spec::create(*context, "fake", "c", "not json", bad) == RESULT_BAD_SPEC, "bad JSON is reported");
        TEST_CHECK(Spec::create(*context, "fake", "d", "[1,2]", bad) == RESULT_BAD_SPEC, "a non-object spec is reported");
        TEST_CHECK(Spec::create(*context, "nosuchkind", "e", "{}", bad) == RESULT_UNKNOWN_TYPE, "an unknown kind is reported");
        TEST_CHECK(Spec::create(*context, "fake", "f", "{\"fail\":true}", bad) == RESULT_UNKNOWN_TYPE,
                   "a factory failure is propagated");

        // A plugin adds a TYPE without displacing the kind's own factory, and is reached first.
        Spec::registerFactory("fake", "plugin", &fakeFactory);
        Handle plugin = NULL_HANDLE;
        TEST_CHECK(Spec::create(*context, "fake", "p", "{\"type\":\"plugin\"}", plugin) == RESULT_OK,
                   "a type-level factory is used");
        Handle stillFallback = NULL_HANDLE;
        TEST_CHECK(Spec::create(*context, "fake", "q", "{\"type\":\"anything\"}", stillFallback) == RESULT_OK,
                   "and the kind's fallback still handles the rest");

        context->unregisterObject("fake", "a");
        context->unregisterObject("fake", "b");
        context->unregisterObject("fake", "p");
        context->unregisterObject("fake", "q");
    }

    void testHandles(const std::shared_ptr<Context>& context) {
        Handle handle = context->findObject("options", "fog");
        TEST_CHECK(handle != NULL_HANDLE, "an id resolves to its handle");
        TEST_CHECK(context->findObject("options", "absent") == NULL_HANDLE, "an unknown id does not");

        auto other = std::make_shared<FogOptions>();
        Handle duplicate = NULL_HANDLE;
        TEST_CHECK(context->registerObject("options", "fog", other, "massif::FogOptions", duplicate) ==
                   RESULT_DUPLICATE_ID, "a taken id is refused");

        // Not zero: every all-static class holds a handle from construction - see testStatics.
        std::size_t baseline = context->getObjectCount() - 1;
        TEST_CHECK(context->unregisterObject("options", "fog"), "unregister reports it existed");
        TEST_CHECK(!context->unregisterObject("options", "fog"), "and not the second time");
        TEST_CHECK(context->getObjectCount() == baseline, "the object is gone");

        PropertyValue value;
        TEST_CHECK(context->getProperty(handle, "rangeStart", value) == RESULT_BAD_HANDLE,
                   "the old handle is stale");

        // The generation is the point: the slot comes back, the handle must not.
        Handle reused = NULL_HANDLE;
        context->registerObject("options", "again", other, "massif::FogOptions", reused);
        TEST_CHECK((reused & 0xFFFFF) == (handle & 0xFFFFF), "the slot index is reused");
        TEST_CHECK(reused != handle, "but the handle differs, so the generation moved");
        TEST_CHECK(context->getProperty(handle, "rangeStart", value) == RESULT_BAD_HANDLE,
                   "and the old handle is still rejected");
    }

}

int main() {
    testTable();

    auto context = std::make_shared<Context>();
    testValues(context);
    testPaths(context);
    testCreate(context);
    testHandles(context);
    testEvents();
    testDelivery();
    testStructCodec();
    testMoreStructs();
    testVariantPaths();
    testFeaturePos();
    testProjections();
    testEventProjection();
    testObjectWrites();
    testCallArgs();
    testCall();
    testCallAsync();
    testCallCancel();
    testCallConcurrency();
    testCollections();
    testRouting();
    testStatics();
    testGeneratedFactories();
    testCAbi();
    testCAbiEvents();

    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
