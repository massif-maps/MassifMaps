/*
 * A build without Android debug properties (every host build, every release) reads no switch:
 * each call returns its default, so a call site needs no #if. The property parsing itself only
 * runs on a device.
 */

#include "utils/DebugSwitch.h"

#include "TestCheck.h"

using namespace massif;

void testDebugSwitch() {
    TEST_CHECK(DebugSwitch::text("anything").empty(), "an unreadable switch has no text");
    TEST_CHECK(DebugSwitch::flag("anything", true), "a flag defaults on when it asks for on");
    TEST_CHECK(!DebugSwitch::flag("anything", false), "a flag defaults off when it asks for off");
    TEST_CHECK(DebugSwitch::number("anything", 16.0, 0.0, false) == 16.0, "a number returns its default");
}
