/*
 * Label scenarios (LabelScenario.h): real tiles through the style and the culler, checked for the invariants
 * a label placement has to keep whichever way the camera got there. To add a case, drop its tiles in
 * label-scenarios/tiles and describe it below; see tests/README.md.
 *
 * NOT covered here: the fade between passes (a pass finds the last one faded in), a tilted view, and
 * the fonts' real metrics - the glyphs are boxes (ScenarioFontManager.cpp), so only relative sizes carry.
 */

#include "LabelScenario.h"

#include "TestCheck.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <exception>

using namespace massif::vt::scenario;

namespace {
    constexpr int MAX_FINDINGS_PRINTED = 8;
    const std::vector<std::string> CHAMPAGNE_TILES = { "14-8409-5842.pbf", "14-8410-5842.pbf" };

    Camera at(double x, double y, double zoom) { return { 8410 + x / 4096, 5842 + y / 4096, zoom }; }

    // Champagne-au-Mont-d'Or: the POI 'Sushi & Kimchi' (z14 tile 8410, unit 299, 576) beside the street 'Rue Maxime
    // Lalouette', among a dozen POIs of the same priority. Every scenario ends on one camera, reached in different ways.
    Scenario champagne(const std::string& name, std::vector<Path> paths, std::vector<Invariant> knownFailures = {}) {
        return { name, CHAMPAGNE_TILES, std::move(paths), 6, { "Sushi & Kimchi", "RUE MAXIME LALOUETTE" }, std::move(knownFailures) };
    }

    std::vector<Scenario> scenarios() {
        Camera z17 = at(350, 520, 17.0), z174 = at(350, 520, 17.4), z176 = at(350, 520, 17.6), z178 = at(350, 520, 17.8);
        std::vector<Scenario> list;
        // Each scenario twice: the tiles there at once, and streaming in two passes apart (with the passes it takes to have
        // them all), where an arriving tile sets what a run kept free again.
        auto add = [&](const std::string& name, std::vector<Path> paths) {
            list.push_back(champagne(name, paths));
            Scenario streamed = champagne(name + " (tiles stream in)", std::move(paths));
            streamed.tileDelay = 2;
            streamed.settlePasses = 30;
            list.push_back(std::move(streamed));
        };
        add("zoom in to z17.0", { zoomIn(z17, 15.5, 20), jump(z17) });
        add("zoom in to z17.4", { zoomIn(z174, 15.5, 20), jump(z174) });
        add("zoom in to z17.8", { zoomIn(z178, 15.5, 20), jump(z178) });
        add("pan to z17.6 from the west or from the south", { jump(z176), travel("pan-W", at(-250, 520, 17.6), z176, 20), travel("pan-S", at(350, 1120, 17.6), z176, 20) });
        add("zoom out and in to z17.8", { jump(z178), via("out-in", { z178, at(350, 520, 16.0), z178 }, 20), via("pan-in", { at(1150, 120, 16.5), at(350, 520, 16.5), z178 }, 20) });
        return list;
    }

    Quad box(float x0, float y0, float x1, float y1) {
        return { cglib::vec2<float>(x0, y0), cglib::vec2<float>(x1, y0), cglib::vec2<float>(x1, y1), cglib::vec2<float>(x0, y1) };
    }

    Decision placed(const std::string& name, bool line, Quad quad) {
        Decision decision;
        decision.name = name;
        decision.line = line;
        decision.visible = true;
        decision.onScreen = true;
        decision.text = true;
        decision.boxes = { quad };
        return decision;
    }

    // Each invariant on labels built by hand, so a scenario that passes is not one whose check cannot fire.
    void checkInvariantControls() {
        Decisions overlapping = { { { 0, 1 }, placed("A", false, box(0, 0, 10, 10)) }, { { 0, 2 }, placed("B", true, box(5, 5, 15, 15)) } };
        TEST_CHECK(overlaps(overlapping).size() == 1, "two placed boxes that cross are an overlap");
        overlapping[{ 0, 2 }].boxes = { box(10, 0, 20, 10) };
        TEST_CHECK(overlaps(overlapping).empty(), "and two that touch are not");
        overlapping[{ 0, 2 }].boxes = { box(5, 5, 15, 15) };
        overlapping[{ 0, 2 }].exempt = true;
        TEST_CHECK(overlaps(overlapping).empty(), "nor are two when one allows overlap");

        Decision poi = placed("POI", false, box(0, 0, 4, 4));
        poi.text = false;
        poi.textSides = { { box(4, 0, 20, 4) } };
        Decisions crowded = { { { 0, 1 }, poi }, { { 1, 2 }, placed("STREET", true, box(10, 0, 30, 4)) } };
        TEST_CHECK(textLosses(crowded).size() == 1, "a POI with a street on its text side and only an icon lost its text to it");
        crowded[{ 1, 3 }] = placed("SHOP", false, box(5, 0, 12, 4));
        TEST_CHECK(textLosses(crowded).empty(), "but not when another POI holds that side too");
        crowded.erase({ 1, 3 });
        crowded[{ 0, 1 }].text = true;
        TEST_CHECK(textLosses(crowded).empty(), "nor when it has its text");

        Decisions before = { { { 0, 1 }, placed("RUE", true, box(0, 0, 10, 2)) }, { { 0, 2 }, placed("POI", false, box(0, 5, 2, 7)) } };
        Decisions after = before;
        after[{ 0, 1 }].sizeStep = 2;
        after[{ 0, 2 }].visible = false;
        after[{ 0, 2 }].variant = 3;
        TEST_CHECK(differences(before, after).size() == 2, "a changed step and a lost label are differences");
        before[{ 0, 2 }].visible = false;
        TEST_CHECK(differences(before, after).size() == 1, "and the side a hidden label last held is not");
        after[{ 0, 1 }].onScreen = false;
        TEST_CHECK(differences(before, after).size() == 1, "a run on the screen on one path and in the band on the other is a difference");
        before[{ 0, 1 }].onScreen = false;
        TEST_CHECK(differences(before, after).empty(), "and in the band on both, whatever its step, is not");
    }

    std::string slug(const std::string& name) {
        std::string out;
        for (char c : name) {
            out += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(c)) : '-';
        }
        return out;
    }

    void check(const Scenario& scenario) {
        std::printf("  %s\n", scenario.name.c_str());
        Evaluation evaluation = evaluate(scenario);
        bool verbose = std::getenv("LABEL_SCENARIO_VERBOSE") != nullptr;
        for (const PathOutcome& outcome : evaluation.paths) {
            std::printf("    %-4s score %.2f: %d POI names, %d icons only, %d street names, %d flips on the way (%d side changes, %d at the stop), %d overlaps mid-fade (%d at the stop), %d reversals\n", outcome.name.c_str(), outcome.score, outcome.poiNames, outcome.poiIcons, outcome.streets, outcome.churn, outcome.sideFlips, outcome.stopFlips, outcome.transients, outcome.stopTransients, outcome.reversals);
            for (const std::string& name : scenario.watch) {
                for (const auto& [key, decision] : outcome.decisions) {
                    if (decision.name == name) {
                        std::printf("      %s: %s\n", name.c_str(), !decision.visible ? "hidden" : decision.line ? ("step " + std::to_string(decision.sizeStep)).c_str() : decision.text ? ("side " + std::to_string(decision.variant)).c_str() : "icon only");
                    }
                }
            }
            if (verbose) {
                std::printf("%s", describe(outcome).c_str());
            }
        }

        if (const char* directory = std::getenv("LABEL_SCENARIO_HTML")) {
            std::string file = std::string(directory) + "/" + slug(scenario.name) + ".html";
            std::ofstream(file) << renderHtml(scenario, evaluation);
            std::ofstream(std::string(directory) + "/index.html", std::ios::app) << "<p><a href=\"" << slug(scenario.name) << ".html\">" << scenario.name << "</a>\n";
            std::printf("    page: %s\n", file.c_str());
        }

        for (Invariant invariant : { Invariant::SETTLING, Invariant::POI_KEEPS_TEXT, Invariant::NO_OVERLAP }) {
            bool known = std::find(scenario.knownFailures.begin(), scenario.knownFailures.end(), invariant) != scenario.knownFailures.end();
            bool broken = evaluation.violates(invariant);
            int printed = 0;
            for (const Finding& finding : evaluation.findings) {
                if (finding.invariant == invariant && printed++ < MAX_FINDINGS_PRINTED) {
                    std::printf("    %s [%s]: %s\n", toString(invariant), finding.path.c_str(), finding.detail.c_str());
                }
            }
            if (printed > MAX_FINDINGS_PRINTED) {
                std::printf("    %s: and %d more\n", toString(invariant), printed - MAX_FINDINGS_PRINTED);
            }
            std::string what = std::string(toString(invariant)) + " (" + scenario.name + ")";
            if (known && broken) {
                std::printf("XFAIL %s: a known failure\n", what.c_str());
            } else if (known) {
                TEST_CHECK(false, (what + ": listed as a known failure but holds, drop it from the scenario").c_str());
            } else {
                TEST_CHECK(!broken, what.c_str());
            }
        }
    }
}

void testLabelScenarios() {
    TEST_CHECK(std::abs(tileScreenWidth({ 8410.5, 5842.5, 17.0 }) - 256.0) < 0.5, "the scenario camera puts a tile of its zoom on 256 screen units");

    checkInvariantControls();

    try {
        for (const Scenario& scenario : scenarios()) {
            check(scenario);
        }
    } catch (const std::exception& ex) {
        TEST_CHECK(false, ex.what());
    }
}
