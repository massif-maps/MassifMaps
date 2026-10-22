/*
 * Tests for WeakSharedMap (all/native/layers/WeakSharedMap.h), which makes a frame of an animated
 * raster layer ONE bitmap: it is the second raster of one tile and the first of the next, and a
 * copy each would upload it twice.
 */

#include "layers/WeakSharedMap.h"

#include <memory>
#include <string>
#include <utility>

using namespace massif;

#include "TestCheck.h"

void testWeakSharedMap() {
    WeakSharedMap<int, std::string> map;

    TEST_CHECK(map.find(1) == nullptr, "an unknown key finds nothing");

    std::shared_ptr<std::string> first = std::make_shared<std::string>("a");
    TEST_CHECK(map.share(1, first) == first, "the first object for a key is adopted");
    TEST_CHECK(map.find(1) == first, "and found again");

    std::shared_ptr<std::string> second = std::make_shared<std::string>("b");
    TEST_CHECK(map.share(1, second) == first, "a second candidate for a live key gets the first one");
    TEST_CHECK(map.share(2, second) == second, "another key is independent");

    first.reset();
    TEST_CHECK(map.find(1) == nullptr, "an object nobody holds is not kept alive");
    std::shared_ptr<std::string> third = std::make_shared<std::string>("c");
    TEST_CHECK(map.share(1, third) == third, "and its key can be shared again");

    // Entries of dropped objects are swept once the map grows, live ones stay.
    std::shared_ptr<std::string> kept = std::make_shared<std::string>("kept");
    map.share(1000, kept);
    for (int key = 0; key < 300; key++) {
        map.share(2000 + key, std::make_shared<std::string>("dropped"));
    }
    TEST_CHECK(map.find(1000) == kept, "a held object survives the sweep");
    TEST_CHECK(map.find(2000) == nullptr, "a dropped one is gone");
}
