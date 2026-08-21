/*
 * Tests for the facade API's property table, handle table and registry.
 * See tests/README.md for what is deliberately out of scope.
 */

#include "api/Context.h"
#include "api/PropertyTable.h"
#include "components/FogOptions.h"
#include "graphics/Color.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace massif;
using namespace massif::api;

static int failures = 0;

#define TEST_CHECK(condition, what)                                              \
    do {                                                                         \
        if (condition) {                                                         \
            std::printf("ok    %s\n", what);                                     \
        } else {                                                                 \
            std::printf("FAIL  %s (%s:%d)\n", what, __FILE__, __LINE__);         \
            failures++;                                                          \
        }                                                                        \
    } while (false)

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

        PropertyValue value;
        value.floatValue = 1.25;
        TEST_CHECK(context->setProperty(handle, "rangeStart", value) == RESULT_OK, "set a float");
        TEST_CHECK(options->getRangeStart() == 1.25f, "the object has the new value");
        // The load-bearing one: the generated thunk calls the class' own setter, so the redraw
        // granularity is inherited rather than reimplemented.
        TEST_CHECK(watcher->seen.size() == 1 && watcher->seen[0] == "RangeStart",
                   "exactly one change notification, correctly named");

        PropertyValue readBack;
        TEST_CHECK(context->getProperty(handle, "rangeStart", readBack) == RESULT_OK &&
                   readBack.floatValue == 1.25, "get returns what was set");

        value = PropertyValue();
        value.boolValue = false;
        TEST_CHECK(context->setProperty(handle, "enabled", value) == RESULT_OK, "set a bool");
        TEST_CHECK(!options->isEnabled(), "the bool took effect");

        value = PropertyValue();
        value.intValue = 0xFF804020;
        TEST_CHECK(context->setProperty(handle, "color", value) == RESULT_OK, "set a colour");
        TEST_CHECK(context->getProperty(handle, "color", readBack) == RESULT_OK &&
                   readBack.intValue == 0xFF804020, "a colour round-trips as ARGB");

        TEST_CHECK(context->setProperty(handle, "nope", value) == RESULT_UNKNOWN_PROPERTY,
                   "an unknown property is reported, not applied");
        TEST_CHECK(context->setProperty(handle + 7777, "rangeStart", value) == RESULT_BAD_HANDLE,
                   "a handle that was never issued is rejected");
    }

    void testPaths(const std::shared_ptr<Context>& context) {
        // Options -> FogOptions cannot be linked standalone, so the happy path is checked on a
        // device. These are the failure modes, which do not need a traversable class.
        Handle handle = context->findObject("options", "fog");
        PropertyValue value;
        value.floatValue = 1;
        TEST_CHECK(context->setProperty(handle, "enabled.foo", value) == RESULT_NOT_TRAVERSABLE,
                   "a dot into a scalar is not traversable");
        TEST_CHECK(context->setProperty(handle, "nosuch.foo", value) == RESULT_UNKNOWN_PROPERTY,
                   "an unknown intermediate segment is reported");
        TEST_CHECK(context->setProperty(handle, "a.b.c", value) == RESULT_UNKNOWN_PROPERTY,
                   "a deep unknown path is reported");
    }

    void testHandles(const std::shared_ptr<Context>& context) {
        Handle handle = context->findObject("options", "fog");
        TEST_CHECK(handle != NULL_HANDLE, "an id resolves to its handle");
        TEST_CHECK(context->findObject("options", "absent") == NULL_HANDLE, "an unknown id does not");

        auto other = std::make_shared<FogOptions>();
        Handle duplicate = NULL_HANDLE;
        TEST_CHECK(context->registerObject("options", "fog", other, "massif::FogOptions", duplicate) ==
                   RESULT_DUPLICATE_ID, "a taken id is refused");

        TEST_CHECK(context->unregisterObject("options", "fog"), "unregister reports it existed");
        TEST_CHECK(!context->unregisterObject("options", "fog"), "and not the second time");
        TEST_CHECK(context->getObjectCount() == 0, "the object is gone");

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
    testHandles(context);

    std::printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
