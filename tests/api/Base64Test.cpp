/*
 * The base64 decoder behind a `data` / `bitmap` spec's "base64" key, which is how a binding with no
 * file to point at hands the SDK bytes. The spec factory itself needs the bitmap codecs and is
 * checked on a device.
 */

#include "api/Base64.h"

#include <string>
#include <vector>

using namespace massif::api;

#include "TestCheck.h"

namespace {

    std::string decoded(const std::string& text, bool& ok) {
        std::vector<unsigned char> bytes;
        ok = decodeBase64(text, bytes);
        return std::string(bytes.begin(), bytes.end());
    }

}

void testBase64() {
    bool ok = false;
    TEST_CHECK(decoded("TWFu", ok) == "Man" && ok, "a whole group decodes");
    TEST_CHECK(decoded("TWE=", ok) == "Ma" && ok, "one padding char drops a byte");
    TEST_CHECK(decoded("TQ==", ok) == "M" && ok, "two padding chars drop two");
    TEST_CHECK(decoded("TWE", ok) == "Ma" && ok, "padding is optional");
    TEST_CHECK(decoded("TW\nFu\r\n", ok) == "Man" && ok, "line breaks are skipped, as MIME wraps them");

    std::vector<unsigned char> bytes;
    TEST_CHECK(decodeBase64("+/8=", bytes) && bytes.size() == 2 && bytes[0] == 0xFB && bytes[1] == 0xFF,
               "'+' and '/' are 62 and 63, and high bytes stay unsigned");

    TEST_CHECK(!decodeBase64("TW-u", bytes) && bytes.empty(), "the url-safe alphabet is refused, not misread");
    TEST_CHECK(!decodeBase64("T", bytes) && bytes.empty(), "a lone sextet cannot be a byte and is refused");
    TEST_CHECK(decodeBase64("", bytes) && bytes.empty(), "empty text is empty bytes; the factory refuses it");
}
