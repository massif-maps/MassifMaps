/*
 * A table style parameter is carried as text in two places - the compiled Mapnik XML and the
 * string-valued parameter API - so a container has to survive a JSON round trip unchanged.
 */

#include "TestCheck.h"

#include <mapnikvt/Value.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mvt = massif::mvt;

namespace {
    mvt::Value makeObject(std::map<std::string, mvt::Value> members) {
        return mvt::Value(std::make_shared<const mvt::ValueObject>(std::move(members)));
    }

    mvt::Value makeArray(std::vector<mvt::Value> elements) {
        return mvt::Value(std::make_shared<const mvt::ValueArray>(std::move(elements)));
    }
}

void testValueJSON() {
    // 1. An object parameter - a colour per activity, what an app owns and a style reads with get.
    {
        mvt::Value colors = makeObject({ { "climbing", mvt::Value(std::string("#D95D39")) },
                                         { "hiking", mvt::Value(std::string("#35944A")) } });
        std::string json = mvt::valueToJSON(colors);
        TEST_CHECK(json == "{\"climbing\":\"#D95D39\",\"hiking\":\"#35944A\"}", "object writes as JSON");

        mvt::Value parsed = mvt::valueFromJSON(json);
        TEST_CHECK(mvt::isContainerValue(parsed), "object reads back as a container");
        TEST_CHECK(mvt::getValueSize(parsed) == 2, "object keeps its members");
        TEST_CHECK(mvt::getValueElement(parsed, mvt::Value(std::string("climbing"))) == mvt::Value(std::string("#D95D39")),
                   "object keeps a member value");
    }

    // 2. An array parameter, indexed by position, with the scalar types the style can hold.
    {
        mvt::Value widths = makeArray({ mvt::Value(4LL), mvt::Value(2.5), mvt::Value(true), mvt::Value(std::string("auto")) });
        std::string json = mvt::valueToJSON(widths);
        TEST_CHECK(json == "[4,2.5,true,\"auto\"]", "array writes as JSON, scalars keep their type");

        mvt::Value parsed = mvt::valueFromJSON(json);
        TEST_CHECK(mvt::getValueSize(parsed) == 4, "array keeps its elements");
        TEST_CHECK(mvt::getValueElement(parsed, mvt::Value(0LL)) == mvt::Value(4LL), "an integer stays an integer");
        TEST_CHECK(mvt::getValueElement(parsed, mvt::Value(1LL)) == mvt::Value(2.5), "a float stays a float");
        TEST_CHECK(mvt::getValueElement(parsed, mvt::Value(2LL)) == mvt::Value(true), "a bool stays a bool");
    }

    // 3. Nesting, and a scalar - valueToJSON is what a value of ANY type is written with.
    {
        mvt::Value nested = makeObject({ { "widths", makeArray({ mvt::Value(1LL) }) } });
        mvt::Value parsed = mvt::valueFromJSON(mvt::valueToJSON(nested));
        TEST_CHECK(mvt::getValueSize(mvt::getValueElement(parsed, mvt::Value(std::string("widths")))) == 1,
                   "a nested container survives");
        TEST_CHECK(!mvt::isContainerValue(mvt::Value(std::string("#fff"))), "a scalar is not a container");
    }

    // 4. Text that is not a table: the parameter keeps no half-read value.
    {
        TEST_CHECK(!mvt::isContainerValue(mvt::valueFromJSON("{oops")), "a broken document reads as unset");
        TEST_CHECK(!mvt::isContainerValue(mvt::valueFromJSON("\"#fff\"")), "a scalar document is not a container");
    }
}
