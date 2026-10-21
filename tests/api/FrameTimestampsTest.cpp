/*
 * Tests for the timestamp -> frameNr rule (all/native/utils/FrameTimestamps.h) behind
 * AnimatedRasterTileLayer. The number keys every tile cache, including the persistent one, so it
 * must name the same image tomorrow, never be shared inside a loop, and fit the tile id.
 */

#include "utils/FrameTimestamps.h"

#include <set>
#include <string>
#include <vector>

using namespace massif;

#include "TestCheck.h"

void testFrameTimestamps() {
    TEST_CHECK(FrameNrFromTimestamp("1700000000") == (1700000000ull / 60) % 65536, "epoch seconds give minutes mod 65536");
    TEST_CHECK(FrameNrFromTimestamp("1700000030") == FrameNrFromTimestamp("1700000000"), "seconds inside a minute are one frame");

    TEST_CHECK(FrameNrFromTimestamp("2026-10-21T10:00:00Z") == FrameNrFromTimestamp("2026-10-21T10:00:00Z"), "a hash is stable");
    TEST_CHECK(FrameNrFromTimestamp("2026-10-21T10:00:00Z") != FrameNrFromTimestamp("2026-10-21T10:10:00Z"), "and tells ISO steps apart");
    TEST_CHECK(FrameNrFromTimestamp("") >= 0, "an empty string is a frame too");
    TEST_CHECK(FrameNrFromTimestamp("99999999999999999") < FRAME_NR_RANGE, "a long number stays in range");

    // RainViewer: 12 steps, 10 minutes apart.
    std::vector<std::string> loop;
    for (int i = 0; i < 12; i++) {
        loop.push_back(std::to_string(1700000000ull + i * 600ull));
    }
    std::vector<int> frameNrs = AssignFrameNrs(loop);
    TEST_CHECK(std::set<int>(frameNrs.begin(), frameNrs.end()).size() == 12, "a loop's frames are all different");
    TEST_CHECK(frameNrs[1] - frameNrs[0] == 10, "ten minutes apart is ten frames apart");

    // Tomorrow's list holds the same instants: they keep their numbers, which is the point.
    std::vector<std::string> rolled(loop.begin() + 2, loop.end());
    rolled.push_back("1700007800");
    std::vector<int> rolledNrs = AssignFrameNrs(rolled);
    TEST_CHECK(rolledNrs[0] == frameNrs[2], "a frame keeps its number when the list rolls");

    std::vector<int> twins = AssignFrameNrs({ "1700000000", "1700000010", "1700000020" });
    TEST_CHECK(std::set<int>(twins.begin(), twins.end()).size() == 3, "timestamps in one minute still get different numbers");

    int max = *std::max_element(frameNrs.begin(), frameNrs.end());
    TEST_CHECK(max < 98000, "every number fits the tile id");
}
