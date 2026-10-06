/*
 * A style names its fonts per platform ("android:Roboto, ios:Helvetica Neue"). The tag list is
 * what decides whether an entry is a platform tag or part of the name, so a tag the build does
 * not know is silently kept as a font called "web:Inter".
 *
 * chainFontNames runs here with a fake resolver: FontManager needs freetype, so loading a real
 * font through text-face-name or shield-icon-face-name is left to a device.
 */

#include "TestCheck.h"

#include <memory>
#include <string>
#include <vector>

#include <vt/FontNames.h>

using massif::vt::chainFontNames;
using massif::vt::fontFaceMatches;
using massif::vt::parseFontNames;

namespace {
    std::string join(const std::vector<std::string>& names) {
        std::string result;
        for (const std::string& name : names) {
            result += (result.empty() ? "" : "|") + name;
        }
        return result;
    }

    // A "font" is the chain spelled out, main font first: "Roboto>Noto>base".
    std::string chain(const std::string& names, const std::string& base) {
        using FontPtr = std::shared_ptr<const std::string>;
        FontPtr baseFont = base.empty() ? FontPtr() : std::make_shared<const std::string>(base);
        FontPtr font = chainFontNames(parseFontNames(names), baseFont, [](const std::string& name, const FontPtr& fallback) {
            if (name.find("Missing") != std::string::npos) {
                return FontPtr();
            }
            return std::make_shared<const std::string>(fallback ? name + ">" + *fallback : name);
        });
        return font ? *font : "<none>";
    }
}

void testFontNames() {
    TEST_CHECK(join(parseFontNames("Roboto")) == "Roboto", "a single name comes back as one entry");
    TEST_CHECK(join(parseFontNames("Roboto, Helvetica Neue")) == "Roboto|Helvetica Neue", "a list keeps its order");
    TEST_CHECK(join(parseFontNames("  'Noto Sans' ")) == "Noto Sans", "quotes and whitespace are stripped");

    // The host build carries no platform tag, so every tagged entry is dropped and only the
    // untagged one survives - which is what makes this checkable without a device.
    TEST_CHECK(join(parseFontNames("android:Roboto, ios:Helvetica, Noto Sans")) == "Noto Sans", "an entry tagged for another platform is dropped");
    TEST_CHECK(join(parseFontNames("web:Inter, Noto Sans")) == "Noto Sans", "web is a platform tag, not part of the name");

    // An unknown prefix is not a tag: the whole entry stays, colon included.
    TEST_CHECK(join(parseFontNames("wasm:Inter")) == "wasm:Inter", "an unknown prefix stays part of the name");

    // HelveticaNeue.ttc, as iOS hands it over for every one of its twelve faces.
    TEST_CHECK(fontFaceMatches("Helvetica Neue Bold", "Helvetica Neue", "Bold", "HelveticaNeue-Bold"), "a style's bold is the collection's bold face");
    TEST_CHECK(!fontFaceMatches("Helvetica Neue Bold", "Helvetica Neue", "Regular", "HelveticaNeue"), "face 0 is not the bold");
    TEST_CHECK(!fontFaceMatches("Helvetica Neue Bold", "Helvetica Neue", "Bold Italic", "HelveticaNeue-BoldItalic"), "the bold italic is not the bold");
    TEST_CHECK(fontFaceMatches("Helvetica Neue", "Helvetica Neue", "Regular", "HelveticaNeue"), "a bare family is its regular face");
    TEST_CHECK(fontFaceMatches("HelveticaNeue-Italic?size=48", "Helvetica Neue", "Italic", "HelveticaNeue-Italic"), "a PostScript name matches, its query ignored");

    TEST_CHECK(chain("Roboto, Noto", "base") == "Roboto>Noto>base", "the first name is the main font, the rest its fallbacks");
    TEST_CHECK(chain("Missing, Roboto", "") == "Roboto", "an unresolved name is skipped, not the end of the list");
    TEST_CHECK(chain("ios:Helvetica Neue Bold, Roboto Bold", "") == "Roboto Bold", "an icon face list falls through a tag for another platform");
    TEST_CHECK(chain("Missing", "base") == "base", "a list where nothing resolves keeps the base font");
    TEST_CHECK(chain("Missing", "") == "<none>", "with no base font, nothing resolving is reported as no font");
}
