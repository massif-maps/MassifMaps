/*
 * A spec key that addresses ONE entry of an indexed property - "metaData.dem_encoding",
 * "params.costing" - and the two different code paths that apply it.
 *
 * NOT covered here: Spec::create's own loop, which reaches Context::setProperty and has always
 * accepted a path. This is the nested half, which had not.
 */

#include "api/Context.h"
#include "api/SpecBuilders.h"
#include "core/MapPos.h"
#include "core/Variant.h"
#include "projections/EPSG3857.h"
#include "routing/RoutingRequest.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace massif;
using namespace massif::api;

#include "TestCheck.h"

/**
 * A NESTED spec - a layer's source, terrain's source - is applied by applySpecProperties, not by
 * the loop in Spec::create, and it used to match a property by its full name only. A "prefix.name"
 * key was therefore accepted at the top level and dropped inside a nested spec, which is how an
 * example's `metaData.dem_encoding` never reached its DEM source: the terrain fell back to the
 * MapBox decoder on terrarium tiles, inflated to hundreds of kilometres, and swallowed the camera.
 */
void testNestedSpecIndexedKey() {
    auto context = std::make_shared<Context>();
    auto request = std::make_shared<RoutingRequest>(std::make_shared<EPSG3857>(), std::vector<MapPos>());

    ObjectRef object;
    object.obj = request;
    object.cppClass = "massif::RoutingRequest";

    std::map<std::string, Variant> spec;
    spec["params.costing"] = Variant(std::string("bicycle"));
    std::set<std::string> consumed;
    applySpecProperties(*context, object, Variant(spec), consumed);

    TEST_CHECK(consumed.count("params.costing") == 1, "a nested spec consumes an indexed key");
    TEST_CHECK(request->getCustomParameter("costing").getString() == "bicycle",
               "a nested spec applies one entry of an indexed property");

    // A prefix that is not an indexed property must still be reported, not written somewhere.
    std::map<std::string, Variant> bad;
    bad["marzipan.costing"] = Variant(std::string("bicycle"));
    std::set<std::string> badConsumed;
    applySpecProperties(*context, object, Variant(bad), badConsumed);
    TEST_CHECK(badConsumed.empty(), "an unknown dotted prefix is not silently consumed");
}
