/*
 * Tests for reading positions in another projection: the name registry, where the source
 * projection comes from, the per-read argument and the per-subscription default.
 */

#include "api/Context.h"
#include "api/Projections.h"
#include "api/StructCodec.h"
#include "geometry/GeoJSONGeometryWriter.h"
#include "projections/EPSG3857.h"
#include "projections/EPSG4326.h"

#include <cmath>
#include <memory>

#include "TestCheck.h"

using namespace massif;
using namespace massif::api;

namespace {

    bool near(double a, double b, double epsilon) {
        return std::fabs(a - b) < epsilon;
    }

    /**
     * Registers an EPSG3857 object as a plain Projection, so its bounds read as a position.
     *
     * 3857 rather than 4326 because a projection's own bounds are the value under test, and the
     * WGS84 ones reach the poles - which Mercator sends to infinity.
     */
    Handle registerMercator(const std::shared_ptr<Context>& context, const std::string& id) {
        Handle handle = NULL_HANDLE;
        context->registerObject("projection", id, std::make_shared<EPSG3857>(),
                                "massif::Projection", handle);
        return handle;
    }

    struct Seen {
        std::string pos;
        Context* context = nullptr;
        Handle target = NULL_HANDLE;
        std::string perRead;   // asked for explicitly inside the handler, when set
    };

    bool readingHandler(void* userData, std::uint32_t, const char*, std::uint32_t) {
        Seen* seen = static_cast<Seen*>(userData);
        PropertyValue value;
        seen->context->getProperty(seen->target, "bounds", value, seen->perRead);
        seen->pos = value.stringValue;
        return false;
    }

}

void testProjections() {
    // The name registry, which is how a C or JavaScript caller names a projection at all.
    TEST_CHECK(Projections::find("EPSG:4326") != nullptr, "a built-in projection resolves");
    TEST_CHECK(Projections::find("epsg:3857") != nullptr, "case does not matter");
    TEST_CHECK(Projections::find("EPSG:9999") == nullptr, "an unknown name resolves to nothing");
    TEST_CHECK(Projections::find("") == nullptr, "and so does an empty one");
    TEST_CHECK(Projections::find("EPSG:4326")->getName() == "EPSG:4326", "and it is the right one");

    // A class that declares a projection answers for itself, whatever it calls the property.
    auto context = std::make_shared<Context>();
    auto writer = std::make_shared<GeoJSONGeometryWriter>();
    writer->setSourceProjection(std::make_shared<EPSG3857>());
    Handle writerHandle = NULL_HANDLE;
    context->registerObject("writer", "w", writer, "massif::GeoJSONGeometryWriter", writerHandle);
    auto declared = context->getObjectProjection(writerHandle);
    TEST_CHECK(declared && declared->getName() == "EPSG:3857",
               "a declared projection is found without the facade naming the property");

    // A class that declares none uses the one attached to it - the payload case.
    Handle handle = registerMercator(context, "p");
    TEST_CHECK(!context->getObjectProjection(handle), "with nothing declared or attached, none");
    context->setObjectProjection(handle, std::make_shared<EPSG3857>());
    TEST_CHECK(context->getObjectProjection(handle)->getName() == "EPSG:3857", "attached is used");

    // Without a projection asked for, the value stays exactly as the object has it.
    PropertyValue value;
    TEST_CHECK(context->getProperty(handle, "bounds", value) == RESULT_OK, "bounds read");
    MapBounds unchanged;
    TEST_CHECK(StructCodec::decode(value.stringValue, unchanged) &&
               near(unchanged.getMax().getX(), 20037508.34, 1.0),
               "and is left in the source projection");

    // Asked for, it converts: the right edge of the Mercator world is 180 degrees east.
    TEST_CHECK(context->getProperty(handle, "bounds", value, "EPSG:4326") == RESULT_OK,
               "a per-read projection is accepted");
    MapBounds converted;
    TEST_CHECK(StructCodec::decode(value.stringValue, converted), "and still decodes as bounds");
    TEST_CHECK(near(converted.getMax().getX(), 180, 1e-6),
               "with the position converted, not just relabelled");
    TEST_CHECK(near(converted.getMin().getX(), -180, 1e-6), "both corners");
    TEST_CHECK(near(converted.getMax().getY(), 85.0511, 1e-3), "including the latitude");

    // The same projection is a no-op rather than a round trip through WGS84.
    TEST_CHECK(context->getProperty(handle, "bounds", value, "EPSG:3857") == RESULT_OK &&
               StructCodec::decode(value.stringValue, unchanged) &&
               near(unchanged.getMax().getX(), 20037508.34, 1.0),
               "asking for the source projection is a no-op");

    // Only positions convert: a projection's name is not a coordinate.
    PropertyValue name;
    TEST_CHECK(context->getProperty(handle, "name", name, "EPSG:4326") == RESULT_OK &&
               name.stringValue == "EPSG:3857", "a non-position property is untouched");

    TEST_CHECK(context->getProperty(handle, "bounds", value, "EPSG:9999") == RESULT_UNKNOWN_TYPE,
               "an unknown projection is an error, not a silent pass-through");

    // With no source projection to convert FROM, the value is left alone rather than guessed at.
    Handle bare = registerMercator(context, "bare");
    TEST_CHECK(context->getProperty(bare, "bounds", value, "EPSG:4326") == RESULT_OK &&
               StructCodec::decode(value.stringValue, unchanged) &&
               near(unchanged.getMax().getX(), 20037508.34, 1.0),
               "an object with no known projection is left unchanged");

    // A conversion Mercator cannot represent fails rather than emitting "inf", which is not JSON.
    Handle wgs84 = NULL_HANDLE;
    context->registerObject("projection", "wgs84", std::make_shared<EPSG4326>(),
                            "massif::Projection", wgs84);
    context->setObjectProjection(wgs84, std::make_shared<EPSG4326>());
    TEST_CHECK(context->getProperty(wgs84, "bounds", value, "EPSG:3857") == RESULT_UNSUPPORTED_TYPE,
               "the poles have no Mercator position, and that is an error");
}

void testEventProjection() {
    auto context = std::make_shared<Context>();
    Handle target = registerMercator(context, "p");
    context->setObjectProjection(target, std::make_shared<EPSG3857>());

    Seen seen;
    seen.context = context.get();
    seen.target = target;

    TEST_CHECK(context->subscribe(target, "click", &readingHandler, &seen, false,
                                  DELIVERY_ORIGIN, false, "EPSG:9999") == NULL_SUBSCRIPTION,
               "subscribing with an unknown projection is refused");

    // The subscription's projection applies to reads made from inside its handler.
    Subscription subscription = context->subscribe(target, "click", &readingHandler, &seen, false,
                                                   DELIVERY_ORIGIN, false, "EPSG:4326");
    TEST_CHECK(subscription != NULL_SUBSCRIPTION, "a known one is accepted");
    context->emit(target, "click", NULL_HANDLE);
    MapBounds bounds;
    TEST_CHECK(StructCodec::decode(seen.pos, bounds) && near(bounds.getMax().getX(), 180, 1e-6),
               "a read inside the handler uses the subscription's projection");

    // ...and only for the duration of the call, which is why the per-read form is the reliable one.
    PropertyValue value;
    context->getProperty(target, "bounds", value);
    TEST_CHECK(StructCodec::decode(value.stringValue, bounds) &&
               near(bounds.getMax().getX(), 20037508.34, 1.0),
               "a read after the handler has returned is back to the source projection");

    // A per-read name wins over the subscription's.
    seen.perRead = "EPSG:3857";
    context->emit(target, "click", NULL_HANDLE);
    TEST_CHECK(StructCodec::decode(seen.pos, bounds) && near(bounds.getMax().getX(), 20037508.34, 1.0),
               "an explicit projection beats the subscription's default");
    context->unsubscribe(subscription);

    // A queued handler gets it too, on the thread the drain runs on.
    seen.perRead.clear();
    seen.pos.clear();
    context->setUiDispatcher([](void*, void (*)(void*), void*) {}, nullptr);
    context->subscribe(target, "click", &readingHandler, &seen, false, DELIVERY_UI, false,
                       "EPSG:4326");
    context->emit(target, "click", NULL_HANDLE);
    TEST_CHECK(seen.pos.empty(), "a queued handler has not run yet");
    context->drainQueue();
    TEST_CHECK(StructCodec::decode(seen.pos, bounds) && near(bounds.getMax().getX(), 180, 1e-6),
               "and the drain applies the same projection");
}
