// A layer-name filter that remembers its answers (vt/LayerNameFilter.h). Not covered: that the renderer and
// the tile reader call it instead of std::regex_match - GLTileRenderer drags GL in, so that is a device
// check, read off the CullWorker share of a simpleperf profile.

#include "LayerNameFilter.h"

#include <string>
#include <thread>
#include <vector>

using namespace massif::vt;

#include "TestCheck.h"

namespace {
    // The shape CompositeVectorTileLayer::buildFilterString builds: a group of style names, each with its attachments.
    const char* GROUP = "^((water|landuse|road)(::.*)?)";

    std::vector<std::string> names() {
        return { "water", "water::fill", "landuse", "landuse::label", "road::casing", "waterway", "roads", "", "building", "Water" };
    }
}

void testLayerNameFilter() {
    std::regex regex(GROUP);
    LayerNameFilter filter(regex);

    for (const std::string& name : names()) {
        TEST_CHECK(filter.matches(name) == std::regex_match(name, regex), "the first answer is regex_match's");
    }
    bool stable = true;
    for (int round = 0; round < 3; round++) {
        for (const std::string& name : names()) {
            stable = stable && filter.matches(name) == std::regex_match(name, regex);
        }
    }
    TEST_CHECK(stable, "a remembered answer, true or false, is the one regex_match gives");
    TEST_CHECK(filter.matches("water::fill") && !filter.matches("waterway"), "the match is a full match, not a prefix");

    LayerNameFilter none(std::regex("[^\\s\\S]"));
    LayerNameFilter background(std::regex("^$"));
    TEST_CHECK(!none.matches("") && !none.matches("water"), "the match-nothing pattern keeps rejecting after its first answer");
    TEST_CHECK(background.matches("") && !background.matches("water"), "the background pattern keeps its empty name");

    LayerNameFilter other(std::regex("road.*"));
    TEST_CHECK(!filter.matches("roads") && other.matches("roads"), "two filters over one name do not share answers");

    std::vector<int> wrong(4, 0);
    LayerNameFilter shared(regex);
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&shared, &regex, &wrong, t] {
            for (int round = 0; round < 200; round++) {
                for (const std::string& name : names()) {
                    wrong[t] += shared.matches(name) != std::regex_match(name, regex);
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    TEST_CHECK(wrong[0] + wrong[1] + wrong[2] + wrong[3] == 0, "threads asking for the same names get regex_match's answers");
}
