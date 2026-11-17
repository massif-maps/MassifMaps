/*
 * Label scenarios: real vector tiles and a style go in, the culler's decisions come out. See tests/README.md.
 *
 * A scenario is a few tiles, a camera path per way of getting to one final camera, and the invariants that must
 * hold whichever way the camera went. The scene runs the chain the renderer does, minus GL:
 * tile -> style (mapnikvt) -> TileLabel -> Label -> LabelCuller::process, one pass per camera of a path.
 */

#ifndef _MASSIF_TESTS_LABELSCENARIO_H_
#define _MASSIF_TESTS_LABELSCENARIO_H_

#include <array>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <cglib/vec.h>

namespace massif::vt::scenario {
    // The map centre in tiles of the fixture's source zoom (8410.5 is the middle of tile 8410), and the render zoom.
    struct Camera {
        double tileX, tileY, zoom;
    };

    // The cameras a run passes through, each placed `passes` times. Every path of a scenario ends on the same camera.
    struct Path {
        std::string name;
        std::vector<Camera> cameras;
        int passes = 1;
    };

    // Many small steps from one camera to another, centre and zoom together: a pinch, a pan, or both. The
    // path is named after the way it goes, `name`; a scenario lists the ones it compares.
    Path travel(const std::string& name, const Camera& from, const Camera& to, int steps, int passes = 1);
    // Many small zoom steps to `to`, from `fromZoom` over the same centre: the finger-zoom of a user.
    Path zoomIn(const Camera& to, double fromZoom, int steps, int passes = 1);
    // Cameras visited in turn, each reached by `steps` small steps: out and back in, a detour, a pan then a zoom.
    Path via(const std::string& name, const std::vector<Camera>& stops, int steps, int passes = 1);
    // One camera: the same view reached at once.
    Path jump(const Camera& to);

    enum class Invariant {
        PATH_INDEPENDENCE, // every path ends on the same visible set, POI sides and line-run size steps
        SETTLING,          // placing again at the final camera changes nothing
        POI_KEEPS_TEXT,    // a POI is never left without its text by a line-run name that sits on a side it could have taken
        NO_OVERLAP         // no two placed labels overlap, in the boxes the culler tests
    };
    const char* toString(Invariant invariant);

    struct Scenario {
        std::string name;
        std::vector<std::string> tiles; // file names in label-scenarios/tiles, '<zoom>-<x>-<y>.pbf'
        std::vector<Path> paths;
        int settlePasses = 6;
        // Names whose state is printed per path, to read a case at a glance (a POI and the street beside it).
        std::vector<std::string> watch;
        // Invariants the culler is known to break on this scenario: reported as XFAIL, and the run fails once one holds.
        std::vector<Invariant> knownFailures;
    };

    using Quad = std::array<cglib::vec2<float>, 4>;

    struct Decision {
        std::string name;
        bool line = false;     // a glyph run along a street, else a point label
        bool exempt = false;   // allow-overlap: takes part in no collision
        bool visible = false;
        bool text = false;     // visible and drawing its text
        int variant = 0;       // the side a point label holds
        int sizeStep = 0;      // the shrink a line run holds
        float priority = 0;
        std::vector<Quad> boxes;     // what it collides by, when visible
        std::vector<std::vector<Quad>> textSides; // point label: the boxes of every side that draws its text, one list per side

        bool sameAs(const Decision& other) const;
    };
    using Decisions = std::map<std::pair<int, long long>, Decision>; // by (layer, global id)

    struct Finding {
        Invariant invariant;
        std::string path;
        std::string detail;
    };

    // What one placement pass left behind, with the camera it was placed at.
    struct Frame {
        Camera camera;
        Decisions decisions;
    };

    struct PathOutcome {
        std::string name;
        std::vector<Frame> frames; // every pass, the settle passes at the final camera last
        Decisions decisions; // after the last settle pass
        double score = 0;    // visible names weighted by priority, to compare policies by a number
        int streets = 0, poiNames = 0, poiIcons = 0;
    };

    struct Evaluation {
        // The culler's screen, in its units, and the band outside it that labels are placed in.
        float width = 0, height = 0, band = 0;
        std::vector<PathOutcome> paths;
        std::vector<Finding> findings;
        bool violates(Invariant invariant) const;
    };

    Evaluation evaluate(const Scenario& scenario);

    // The invariants on their own, over decisions a test may build by hand (its negative controls).
    // The labels whose decision differs between two states of one label set, one line each.
    std::vector<std::string> differences(const Decisions& before, const Decisions& after);
    // The pairs of placed labels whose boxes overlap.
    std::vector<std::string> overlaps(const Decisions& decisions);
    // The POIs without their text, with a street name on a side they could have taken and nothing else on it.
    std::vector<std::string> textLosses(const Decisions& decisions);

    // How wide a tile of the camera's zoom is on the culler's screen, in its units: 256 where the camera is calibrated.
    double tileScreenWidth(const Camera& camera);

    // Every path side by side as one HTML page of SVG, with a slider and a play button over its passes: what the
    // culler drew at each. At the final camera, in red what a path does differently from the first (and dashed
    // where it hid a label the first showed); at any, in orange what changed since the pass before.
    std::string renderHtml(const Scenario& scenario, const Evaluation& evaluation);

    // The visible set of one path, one label per line.
    std::string describe(const PathOutcome& outcome);
}

#endif
