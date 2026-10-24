// What a native-owned map view puts in the registry (api/ViewRegistration.h): three ids that
// share the view's id, all or nothing, and gone together.
//
// NOT covered here: MapViewHost itself, which builds a BaseMapView and so needs GL; that is a
// device check (the demo opens, draws and takes a tap).

#include "api/Context.h"
#include "api/ViewRegistration.h"

#include <memory>

using namespace massif;
using namespace massif::api;

#include "TestCheck.h"

void testViewRegistration() {
    auto context = std::make_shared<Context>();
    auto view = std::make_shared<int>(1);
    auto options = std::make_shared<int>(2);
    auto layers = std::make_shared<int>(3);

    ViewHandles handles;
    TEST_CHECK(ViewRegistration::registerView(*context, "main", view, options, layers, handles) == RESULT_OK,
               "a view registers");
    TEST_CHECK(handles.view && handles.map && handles.layers, "with a handle for each of the three");
    TEST_CHECK(context->findObject("view", "main") == handles.view, "the view is under kind view");
    TEST_CHECK(context->findObject("map", "main") == handles.map, "the options under kind map");
    TEST_CHECK(context->findObject("layers", "main") == handles.layers, "the layers under kind layers");
    TEST_CHECK(context->getObject(handles.map) == std::static_pointer_cast<void>(options),
               "the map handle is the options");

    ViewHandles again;
    TEST_CHECK(ViewRegistration::registerView(*context, "main", std::make_shared<int>(4), std::make_shared<int>(5),
                                              std::make_shared<int>(6), again) == RESULT_DUPLICATE_ID,
               "the same id twice is refused");
    TEST_CHECK(!again.view && context->findObject("view", "main") == handles.view,
               "and leaves the first one alone");

    // One id free and one taken: nothing of the second view may stay behind, and the taken one must survive.
    Handle other = NULL_HANDLE;
    context->registerObject("layers", "second", std::make_shared<int>(7), "massif::Layers", other);
    ViewHandles partial;
    TEST_CHECK(ViewRegistration::registerView(*context, "second", std::make_shared<int>(8),
                                              std::make_shared<int>(9), std::make_shared<int>(10), partial)
                   == RESULT_DUPLICATE_ID,
               "a taken layers id refuses the whole view");
    TEST_CHECK(context->findObject("view", "second") == NULL_HANDLE && context->findObject("map", "second") == NULL_HANDLE,
               "the view and map ids it had taken are given back");
    TEST_CHECK(context->findObject("layers", "second") == other, "and the id that was already taken is untouched");

    ViewRegistration::unregisterView(*context, "main");
    TEST_CHECK(context->findObject("view", "main") == NULL_HANDLE && context->findObject("map", "main") == NULL_HANDLE &&
                   context->findObject("layers", "main") == NULL_HANDLE,
               "unregistering drops all three");
    TEST_CHECK(!context->getObject(handles.view), "the context no longer keeps the view alive");
    ViewRegistration::unregisterView(*context, "main");
    ViewHandles reuse;
    TEST_CHECK(ViewRegistration::registerView(*context, "main", view, options, layers, reuse) == RESULT_OK,
               "and the id can be reused, also after a second unregister");
}
