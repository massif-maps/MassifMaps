#include "LabelScenario.h"

#include "Label.h"
#include "LabelCuller.h"

#include <mapnikvt/LayerTileReader.h>
#include <mapnikvt/Logger.h>
#include <mapnikvt/MBVTFeatureDecoder.h>
#include <mapnikvt/MapParser.h>
#include <mapnikvt/SymbolizerParser.h>
#include <vt/Tile.h>
#include <vt/TileTransformer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <stdexcept>

#include <utf8.h>

namespace massif::vt::scenario {
    namespace {
        // What a pass is run with: one phone-sized screen at one pixel per point, the renderer's planar camera.
        constexpr double WORLD_SIZE = 1 << 20;
        constexpr double EARTH_CIRCUMFERENCE = 2.0 * 3.14159265358979323846 * 6378137.0;
        constexpr double TILE_SIZE = 256;
        constexpr double SCREEN_HEIGHT = 512;
        constexpr double ASPECT = 0.75;
        constexpr double FOV = 40.0 * 3.14159265358979323846 / 180.0;
        // LabelCuller::EXTRA_LABEL_BUFFER: the box a label is placed by has one pixel more than the glyphs.
        constexpr float CULLER_BUFFER = 1.0f;
        constexpr int MAX_RETRIES = 4;
        constexpr std::size_t REVERSAL_WINDOW = 3;
        constexpr std::size_t SETTLED_WINDOW = 6; // the last passes at the final camera, after every tile has come

        struct StrictLogger : mvt::Logger {
            void write(Severity severity, const std::string& message) override {
                if (severity == Severity::ERROR) {
                    throw std::runtime_error("label scenario style: " + message);
                }
            }
        };

        std::vector<unsigned char> readFile(const std::string& path) {
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                throw std::runtime_error("label scenario: cannot read " + path);
            }
            return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        // Tiles the style has made, per target tile; they never change, so every path shares them.
        class Fixture {
        public:
            explicit Fixture(const std::vector<std::string>& tileFiles) : _transformer(std::make_shared<DefaultTileTransformer>(static_cast<float>(WORLD_SIZE))) {
                auto logger = std::make_shared<StrictLogger>();
                pugi::xml_document doc;
                if (!doc.load_file(SCENARIO_DIR "/style/scenario.xml")) {
                    throw std::runtime_error("label scenario: cannot read the style");
                }
                _map = mvt::MapParser(std::make_shared<mvt::SymbolizerParser>(logger), logger).parseMap(doc);

                auto fontManager = std::make_shared<FontManager>(64, 64);
                mvt::SymbolizerContext::Settings settings(static_cast<float>(TILE_SIZE), std::make_shared<mvt::StyleParameterStore>(), fontManager->getFont("Scenario", nullptr));
                _context = std::make_unique<mvt::SymbolizerContext>(std::make_shared<BitmapManager>(nullptr), fontManager, std::make_shared<StrokeMap>(512, 512), std::make_shared<GlyphMap>(64, 64), settings);

                for (const std::string& file : tileFiles) {
                    TileId id(0, 0, 0);
                    if (std::sscanf(file.c_str(), "%d-%d-%d", &id.zoom, &id.x, &id.y) != 3) {
                        throw std::runtime_error("label scenario: tile name is not <zoom>-<x>-<y>.pbf: " + file);
                    }
                    _sourceZoom = id.zoom;
                    _decoders.emplace_back(id, std::make_unique<mvt::MBVTFeatureDecoder>(readFile(SCENARIO_DIR "/tiles/" + file), logger));
                }
                _logger = logger;
            }

            int sourceZoom() const { return _sourceZoom; }
            const DefaultTileTransformer& transformer() const { return *_transformer; }

            // The tile `target` is cut from a source tile, or null where no fixture covers it.
            std::shared_ptr<const Tile> tile(const TileId& target) {
                auto key = std::make_tuple(target.zoom, target.x, target.y);
                auto it = _tiles.find(key);
                if (it != _tiles.end()) {
                    return it->second;
                }
                std::shared_ptr<Tile> tile;
                int dz = target.zoom - _sourceZoom;
                for (auto& [source, decoder] : _decoders) {
                    if (source.x != (target.x >> dz) || source.y != (target.y >> dz)) {
                        continue;
                    }
                    // VectorTileDecoder::calculateTileTransform: the source tile's unit square onto the target's.
                    float scale = static_cast<float>(1 << dz);
                    float x = static_cast<float>(target.x & ((1 << dz) - 1)), y = static_cast<float>(target.y & ((1 << dz) - 1));
                    decoder->setTransform(cglib::translate3_matrix(cglib::vec3<float>(-x, -y, 1)) * cglib::scale3_matrix(cglib::vec3<float>(scale, scale, 1)));
                    mvt::LayerTileReader reader(_map, _transformer, *_context, *decoder, _logger);
                    tile = reader.readTile(target, target.zoom);
                }
                _tiles[key] = tile;
                return tile;
            }

        private:
            std::shared_ptr<const DefaultTileTransformer> _transformer;
            std::shared_ptr<mvt::Map> _map;
            std::unique_ptr<mvt::SymbolizerContext> _context;
            std::shared_ptr<mvt::Logger> _logger;
            std::vector<std::pair<TileId, std::unique_ptr<mvt::MBVTFeatureDecoder>>> _decoders;
            std::map<std::tuple<int, int, int>, std::shared_ptr<Tile>> _tiles;
            int _sourceZoom = 0;
        };

        struct Screen {
            ViewState view; // what the renderer hands the culler
            ViewState cullerView; // the same, as the culler holds it (LabelCuller::setViewState)
            cglib::mat4x4<float> projection;
            double distance;
        };

        // The renderer's planar camera over the map centre, straight down: ViewState::calculateViewState.
        Screen makeScreen(const Fixture& fixture, const Camera& camera) {
            double zoomPower = std::pow(2.0, camera.zoom);
            double focusX = (camera.tileX / (1 << fixture.sourceZoom()) - 0.5) * WORLD_SIZE;
            double focusY = (0.5 - camera.tileY / (1 << fixture.sourceZoom())) * WORLD_SIZE;
            double distance = 0.5 * SCREEN_HEIGHT * WORLD_SIZE / (TILE_SIZE * std::tan(FOV * 0.5) * zoomPower);

            cglib::vec3<double> eye(focusX, focusY, distance);
            cglib::mat4x4<double> cameraMatrix = cglib::lookat4_matrix(eye, cglib::vec3<double>(focusX, focusY, 0), cglib::vec3<double>(0, 1, 0));
            cglib::mat4x4<double> projectionMatrix = cglib::perspective4_matrix(FOV, ASPECT, 1.0, distance * 0.1, distance * 10.0);

            ViewState view(projectionMatrix, cameraMatrix, static_cast<float>(camera.zoom), 0.0f, 90.0f, static_cast<float>(ASPECT), static_cast<float>(2 * TILE_SIZE));
            view.planarProjection = true;
            view.focusDistance = static_cast<float>(distance);
            view.deviceResolution = static_cast<float>(SCREEN_HEIGHT);

            Screen screen { view, view, cglib::mat4x4<float>::identity(), distance };
            screen.cullerView.zoomScale *= static_cast<float>(WORLD_SIZE);
            cglib::mat4x4<double> local = cameraMatrix;
            for (int i = 0; i < 3; i++) {
                local(i, 3) = 0;
            }
            screen.projection = cglib::mat4x4<float>::convert(projectionMatrix * local);
            return screen;
        }

        // LabelCuller::projectEnvelope.
        Quad project(const Screen& screen, const std::array<cglib::vec3<float>, 4>& world) {
            Quad quad;
            for (std::size_t i = 0; i < 4; i++) {
                cglib::vec2<float> p = cglib::proj_o(cglib::transform_point(world[i], screen.projection));
                quad[i] = cglib::vec2<float>((p(0) * 0.5f + 0.5f) * screen.view.resolution * screen.view.aspect, (p(1) * 0.5f + 0.5f) * screen.view.resolution);
            }
            return quad;
        }

        // Separating axis over two convex quads; touching is not overlapping.
        bool overlap(const Quad& a, const Quad& b) {
            for (const Quad* quad : { &a, &b }) {
                for (std::size_t i = 0; i < 4; i++) {
                    cglib::vec2<float> edge = (*quad)[(i + 1) % 4] - (*quad)[i];
                    cglib::vec2<float> axis(edge(1), -edge(0));
                    float minA = 1e30f, maxA = -1e30f, minB = 1e30f, maxB = -1e30f;
                    for (std::size_t j = 0; j < 4; j++) {
                        float pa = cglib::dot_product(axis, a[j]), pb = cglib::dot_product(axis, b[j]);
                        minA = std::min(minA, pa); maxA = std::max(maxA, pa);
                        minB = std::min(minB, pb); maxB = std::max(maxB, pb);
                    }
                    if (maxA <= minB || maxB <= minA) {
                        return false;
                    }
                }
            }
            return true;
        }

        bool overlap(const std::vector<Quad>& a, const std::vector<Quad>& b) {
            return std::any_of(a.begin(), a.end(), [&](const Quad& qa) {
                return std::any_of(b.begin(), b.end(), [&](const Quad& qb) { return overlap(qa, qb); });
            });
        }

        std::string textOf(const TileLabel& tileLabel) {
            std::u32string text;
            for (const Font::Glyph& glyph : tileLabel.getGlyphs()) {
                if (!glyph.icon && glyph.codePoint != Font::CR_CODEPOINT && glyph.utf32Char != 0) {
                    text.push_back(glyph.utf32Char);
                }
            }
            std::string utf8Text;
            utf8::utf32to8(text.begin(), text.end(), std::back_inserter(utf8Text));
            return utf8Text;
        }

        // The labels of one path: what GLTileRenderer keeps of them between passes, and no more.
        class Scene {
        public:
            Scene(Fixture& fixture, int tileDelay) : _fixture(fixture), _tileDelay(tileDelay), _culler(static_cast<float>(WORLD_SIZE)) { }

            // One placement pass at `camera`, and then the passes the renderer owes a run it sent to another spot.
            Decisions place(const Camera& camera) {
                Screen screen = makeScreen(_fixture, camera);
                rebuild(screen, camera);

                std::vector<std::shared_ptr<Label>> order = _labels;
                // GLTileRenderer: most important first, then layer, then id.
                std::stable_sort(order.begin(), order.end(), [](const std::shared_ptr<Label>& a, const std::shared_ptr<Label>& b) {
                    if (a->getPriority() != b->getPriority()) {
                        return a->getPriority() > b->getPriority();
                    }
                    if (a->getLayerIndex() != b->getLayerIndex()) {
                        return a->getLayerIndex() < b->getLayerIndex();
                    }
                    return a->getGlobalId() < b->getGlobalId();
                });

                double latitude = (camera.tileY / (1 << _fixture.sourceZoom()) - 0.5) * 2.0 * 3.14159265358979323846;
                for (int retry = 0; retry <= MAX_RETRIES; retry++) {
                    _culler.reset();
                    _culler.setViewState(screen.view);
                    _culler.setMetersToInternal(WORLD_SIZE / EARTH_CIRCUMFERENCE * std::cosh(latitude));
                    std::mutex mutex;
                    std::size_t cursor = 0;
                    _culler.process(order, mutex, cursor);
                    // A pass is placed once the last one has faded: MIN_PLACEMENT_INTERVAL is the fade.
                    for (const std::shared_ptr<Label>& label : _labels) {
                        label->setOpacity(label->isVisible() ? 1.0f : 0.0f);
                        label->setTextOpacity(label->isVisible() && label->drawsText() ? 1.0f : 0.0f);
                    }
                    if (!_culler.consumeRetried()) {
                        break;
                    }
                }
                _pass++;
                return snapshot(screen);
            }

        private:
            using Key = std::pair<int, long long>;

            // Tiles at the zoom below the camera's, covering the screen and the band labels are placed in.
            std::vector<TileId> visibleTiles(const Screen& screen, const Camera& camera) const {
                int zoom = std::max(_fixture.sourceZoom(), static_cast<int>(std::floor(camera.zoom)));
                double tiles = std::pow(2.0, zoom);
                double worldPerPixel = WORLD_SIZE / (TILE_SIZE * std::pow(2.0, camera.zoom));
                double halfHeight = (0.5 * SCREEN_HEIGHT + screen.view.labelPadding) * worldPerPixel;
                double halfWidth = (0.5 * SCREEN_HEIGHT * ASPECT + screen.view.labelPadding) * worldPerPixel;
                double centerX = (camera.tileX / (1 << _fixture.sourceZoom())), centerY = camera.tileY / (1 << _fixture.sourceZoom());
                int x0 = static_cast<int>(std::floor((centerX - halfWidth / WORLD_SIZE) * tiles)), x1 = static_cast<int>(std::floor((centerX + halfWidth / WORLD_SIZE) * tiles));
                int y0 = static_cast<int>(std::floor((centerY - halfHeight / WORLD_SIZE) * tiles)), y1 = static_cast<int>(std::floor((centerY + halfHeight / WORLD_SIZE) * tiles));
                std::vector<TileId> ids;
                for (int y = y0; y <= y1; y++) {
                    for (int x = x0; x <= x1; x++) {
                        ids.emplace_back(zoom, x, y);
                    }
                }
                return ids;
            }

            // GLTileRenderer on a new tile set: the copies of a label the tiles carry merge, and the placement state is handed on.
            // The tiles the renderer draws: a wanted tile is there `_tileDelay` passes after the one wanted before it, and
            // until all have come the tiles they replace stay.
            std::vector<TileId> arrivedTiles(const std::vector<TileId>& wanted) {
                std::vector<TileId> arrived;
                int fresh = 0;
                bool pending = false;
                for (const TileId& id : wanted) {
                    auto key = std::make_tuple(id.zoom, id.x, id.y);
                    auto it = _arrival.find(key);
                    if (it == _arrival.end()) {
                        it = _arrival.emplace(key, _pass + _tileDelay * ++fresh).first;
                    }
                    if (it->second <= _pass) {
                        arrived.push_back(id);
                    } else {
                        pending = true;
                    }
                }
                for (const TileId& id : _tileIds) {
                    if (pending && std::find(wanted.begin(), wanted.end(), id) == wanted.end()) {
                        arrived.push_back(id);
                    }
                }
                return arrived;
            }

            void rebuild(const Screen& screen, const Camera& camera) {
                std::vector<TileId> ids = arrivedTiles(visibleTiles(screen, camera));
                if (ids == _tileIds) {
                    return;
                }
                _tileIds = ids;

                std::map<Key, std::shared_ptr<Label>> next;
                std::map<Key, std::string> names;
                for (const TileId& id : ids) {
                    std::shared_ptr<const Tile> tile = _fixture.tile(id);
                    if (!tile) {
                        continue;
                    }
                    cglib::mat4x4<double> tileMatrix = _fixture.transformer().calculateTileMatrix(id, 1.0f);
                    std::shared_ptr<const TileTransformer::VertexTransformer> vertexTransformer = _fixture.transformer().createTileVertexTransformer(id);
                    for (const std::shared_ptr<TileLayer>& layer : tile->getLayers()) {
                        for (const std::shared_ptr<TileLabel>& tileLabel : layer->getLabels()) {
                            Key key(layer->getLayerIndex(), tileLabel->getGlobalId());
                            auto label = std::make_shared<Label>(*tileLabel, id, layer->getLayerIndex(), tileMatrix, vertexTransformer);
                            auto& held = next[key];
                            if (held) {
                                held->mergeGeometries(*label);
                            } else {
                                held = label;
                                names[key] = textOf(*tileLabel);
                            }
                        }
                    }
                }

                _labels.clear();
                for (auto& [key, label] : next) {
                    auto old = _byKey.find(key);
                    if (old != _byKey.end()) {
                        label->setVisible(old->second->isVisible());
                        label->setOpacity(old->second->getOpacity());
                        label->setTextOpacity(old->second->getTextOpacity());
                        label->snapPlacement(*old->second);
                    }
                    label->setActive(true);
                    _labels.push_back(label);
                }
                _byKey = std::move(next);
                _names = std::move(names);
            }

            Decisions snapshot(const Screen& screen) const {
                Decisions decisions;
                std::vector<std::array<cglib::vec3<float>, 4>> envelopes;
                std::vector<std::vector<std::array<cglib::vec3<float>, 4>>> parts;
                for (const auto& [key, label] : _byKey) {
                    Decision& decision = decisions[key];
                    decision.name = _names.at(key);
                    decision.line = label->isLineRun();
                    decision.exempt = label->getGroupId() < 0;
                    decision.visible = label->isVisible();
                    decision.onScreen = label->isOnScreen();
                    decision.text = decision.visible && label->drawsText();
                    decision.variant = label->getVariantIndex();
                    decision.sizeStep = label->getSizeStep();
                    decision.priority = label->getPriority();

                    float size = (label->getStyle()->sizeFunc)(screen.cullerView);
                    auto collect = [&](int variant, std::vector<Quad>& out) {
                        if (!label->calculateVariantEnvelopes(size, CULLER_BUFFER, screen.cullerView, envelopes, parts)) {
                            return;
                        }
                        std::size_t index = std::min<std::size_t>(variant, envelopes.size() - 1);
                        if (parts[index].empty()) {
                            out.push_back(project(screen, envelopes[index]));
                        }
                        for (const auto& part : parts[index]) {
                            out.push_back(project(screen, part));
                        }
                    };
                    if (decision.visible) {
                        collect(decision.variant, decision.boxes);
                    }
                    // Walking the sides is a cache invalidation on the label, and it is back where it was.
                    int held = label->getVariantIndex();
                    for (std::size_t side = 0; !decision.line && side < label->getVariantCount(); side++) {
                        label->setVariantIndex(static_cast<int>(side));
                        if (label->drawsText()) {
                            collect(static_cast<int>(side), decision.textSides.emplace_back());
                        }
                    }
                    label->setVariantIndex(held);
                }
                return decisions;
            }

            Fixture& _fixture;
            int _tileDelay;
            int _pass = 0;
            std::map<std::tuple<int, int, int>, int> _arrival;
            LabelCuller _culler;
            std::vector<TileId> _tileIds;
            std::map<Key, std::shared_ptr<Label>> _byKey;
            std::map<Key, std::string> _names;
            std::vector<std::shared_ptr<Label>> _labels;
        };

        // A name is not an identity (a street has a run per tile): the id's low digits tell two of them apart.
        std::string labelOf(const std::pair<int, long long>& key, const Decision& decision) {
            char id[16];
            std::snprintf(id, sizeof id, "#%04llx", static_cast<unsigned long long>(key.second) & 0xffff);
            return "'" + decision.name + "' " + id;
        }

        std::string flip(const std::pair<int, long long>& key, const Decision& before, const Decision& after) {
            auto state = [](const Decision& d) {
                return !d.shown() ? std::string("hidden") : d.line ? "step " + std::to_string(d.sizeStep) : d.text ? "side " + std::to_string(d.variant) : std::string("icon only");
            };
            return labelOf(key, after) + " " + state(before) + " -> " + state(after);
        }
    }

    bool Decision::sameAs(const Decision& other) const {
        return shown() == other.shown() && (!shown() || (text == other.text && variant == other.variant && sizeStep == other.sizeStep));
    }

    Path travel(const std::string& name, const Camera& from, const Camera& to, int steps, int passes) {
        return via(name, { from, to }, steps, passes);
    }

    Path zoomIn(const Camera& to, double fromZoom, int steps, int passes) {
        return travel("slow", { to.tileX, to.tileY, fromZoom }, to, steps, passes);
    }

    Path via(const std::string& name, const std::vector<Camera>& stops, int steps, int passes) {
        Path path { name, { stops.front() }, passes };
        for (std::size_t stop = 1; stop < stops.size(); stop++) {
            for (int step = 1; step <= steps; step++) {
                double t = static_cast<double>(step) / steps;
                const Camera &a = stops[stop - 1], &b = stops[stop];
                path.cameras.push_back({ a.tileX + (b.tileX - a.tileX) * t, a.tileY + (b.tileY - a.tileY) * t, a.zoom + (b.zoom - a.zoom) * t });
            }
        }
        return path;
    }

    Path jump(const Camera& to) {
        return { "fast", { to }, 1 };
    }

    const char* toString(Invariant invariant) {
        switch (invariant) {
        case Invariant::SETTLING: return "settling";
        case Invariant::POI_KEEPS_TEXT: return "a POI keeps its text";
        case Invariant::NO_OVERLAP: return "no overlap";
        }
        return "";
    }

    bool Evaluation::violates(Invariant invariant) const {
        return std::any_of(findings.begin(), findings.end(), [&](const Finding& finding) { return finding.invariant == invariant; });
    }

    std::vector<std::string> differences(const Decisions& before, const Decisions& after) {
        std::vector<std::string> flips;
        for (const auto& [key, decision] : after) {
            auto it = before.find(key);
            if (it != before.end() && !it->second.sameAs(decision)) {
                flips.push_back(flip(key, it->second, decision));
            }
        }
        return flips;
    }

    std::vector<std::string> overlaps(const Decisions& decisions) {
        std::vector<std::string> pairs;
        for (auto a = decisions.begin(); a != decisions.end(); a++) {
            for (auto b = std::next(a); b != decisions.end(); b++) {
                if (a->second.visible && b->second.visible && !a->second.exempt && !b->second.exempt && overlap(a->second.boxes, b->second.boxes)) {
                    pairs.push_back(labelOf(a->first, a->second) + " and " + labelOf(b->first, b->second));
                }
            }
        }
        return pairs;
    }

    std::vector<std::string> textLosses(const Decisions& decisions) {
        std::vector<std::string> losses;
        for (const auto& [key, poi] : decisions) {
            if (poi.line || poi.text) {
                continue;
            }
            // A side that a street holds and no other label does was the POI's to take.
            for (const std::vector<Quad>& side : poi.textSides) {
                bool street = false, other = false;
                for (const auto& [otherKey, label] : decisions) {
                    if (otherKey == key || !label.visible || label.exempt || !overlap(side, label.boxes)) {
                        continue;
                    }
                    (label.line ? street : other) = true;
                }
                if (street && !other) {
                    losses.push_back(labelOf(key, poi) + " has " + (poi.visible ? "its icon only" : "nothing") + ", a street name sits on a side of it");
                    break;
                }
            }
        }
        return losses;
    }

    double tileScreenWidth(const Camera& camera) {
        Fixture fixture({});
        Screen screen = makeScreen(fixture, camera);
        // One tile of the camera's zoom to the right of the focus, which is on the middle of the screen.
        cglib::vec3<float> focus(0, 0, static_cast<float>(-screen.view.origin(2)));
        double tile = WORLD_SIZE / std::pow(2.0, std::floor(camera.zoom));
        std::array<cglib::vec3<float>, 4> world = { focus, focus + cglib::vec3<float>(static_cast<float>(tile), 0, 0), focus, focus };
        Quad quad = project(screen, world);
        return quad[1](0) - quad[0](0);
    }

    // Between two passes the renderer fades what changed: the old state out and the new one in, so for a moment a label
    // that left or changed shows over one that arrives or changes where it was.
    static int transientOverlaps(const Decisions& before, const Decisions& after) {
        int count = 0;
        for (auto a = after.begin(); a != after.end(); a++) {
            auto previousA = before.find(a->first);
            for (auto b = std::next(a); previousA != before.end() && b != after.end(); b++) {
                auto previousB = before.find(b->first);
                if (previousB == before.end() || a->second.exempt || b->second.exempt) {
                    continue;
                }
                if (previousA->second.sameAs(a->second) && previousB->second.sameAs(b->second)) {
                    continue;
                }
                bool old = previousA->second.shown() && b->second.shown() && overlap(previousA->second.boxes, b->second.boxes);
                bool young = a->second.shown() && previousB->second.shown() && overlap(a->second.boxes, previousB->second.boxes);
                count += (old || young ? 1 : 0);
            }
        }
        return count;
    }

    static int reversals(const std::vector<Frame>& frames) {
        auto state = [](const Decision& d) {
            return !d.shown() ? 0 : d.line ? 100 + d.sizeStep : d.text ? 10 + d.variant : 1;
        };
        int count = 0;
        for (std::size_t frame = 1; frame < frames.size(); frame++) {
            for (const auto& [key, decision] : frames[frame].decisions) {
                auto it = frames[frame - 1].decisions.find(key);
                if (it == frames[frame - 1].decisions.end() || state(it->second) == state(decision)) {
                    continue;
                }
                for (std::size_t later = frame + 1; later < frames.size() && later <= frame + REVERSAL_WINDOW; later++) {
                    auto back = frames[later].decisions.find(key);
                    if (back != frames[later].decisions.end() && state(back->second) == state(it->second)) {
                        count++;
                        break;
                    }
                }
            }
        }
        return count;
    }

    Evaluation evaluate(const Scenario& scenario) {
        Fixture fixture(scenario.tiles);
        Evaluation evaluation;
        evaluation.width = static_cast<float>(SCREEN_HEIGHT * ASPECT);
        evaluation.height = static_cast<float>(SCREEN_HEIGHT);
        evaluation.band = ViewState::MAX_LABEL_PADDING;
        for (const Path& path : scenario.paths) {
            Scene scene(fixture, scenario.tileDelay);
            PathOutcome outcome;
            outcome.name = path.name;
            for (const Camera& camera : path.cameras) {
                for (int pass = 0; pass < path.passes; pass++) {
                    outcome.frames.push_back({ camera, scene.place(camera) });
                }
            }
            std::vector<Decisions> passes;
            for (int pass = 0; pass < scenario.settlePasses; pass++) {
                passes.push_back(scene.place(path.cameras.back()));
                outcome.frames.push_back({ path.cameras.back(), passes.back() });
            }

            outcome.decisions = passes.back();
            std::size_t moving = path.cameras.size() * path.passes;
            for (std::size_t frame = 1; frame < outcome.frames.size(); frame++) {
                const Decisions &before = outcome.frames[frame - 1].decisions, &after = outcome.frames[frame].decisions;
                int flips = static_cast<int>(differences(before, after).size());
                outcome.churn += flips;
                outcome.stopFlips += (frame >= moving ? flips : 0);
                for (const auto& [key, decision] : after) {
                    auto it = before.find(key);
                    outcome.sideFlips += (it != before.end() && !decision.line && decision.shown() && it->second.shown() && decision.text && it->second.text && decision.variant != it->second.variant ? 1 : 0);
                }
                int overlapsNow = transientOverlaps(before, after);
                outcome.transients += overlapsNow;
                outcome.stopTransients += (frame >= moving ? overlapsNow : 0);
            }
            outcome.reversals = reversals(outcome.frames);
            float lowest = 1e30f, highest = -1e30f;
            for (const auto& [key, decision] : outcome.decisions) {
                lowest = std::min(lowest, decision.priority);
                highest = std::max(highest, decision.priority);
            }
            for (const auto& [key, decision] : outcome.decisions) {
                if (!decision.shown()) {
                    continue;
                }
                (decision.line ? outcome.streets : decision.text ? outcome.poiNames : outcome.poiIcons)++;
                double weight = 1.0 + (highest > lowest ? (decision.priority - lowest) / (highest - lowest) : 0.0);
                outcome.score += decision.text ? weight : 0.25 * weight;
            }

            for (std::size_t pass = std::max<std::size_t>(1, passes.size() > SETTLED_WINDOW ? passes.size() - SETTLED_WINDOW + 1 : 1); pass < passes.size(); pass++) {
                for (const std::string& flip : differences(passes[pass - 1], passes[pass])) {
                    evaluation.findings.push_back({ Invariant::SETTLING, path.name, "pass " + std::to_string(pass) + ": " + flip });
                }
            }
            for (const std::string& loss : textLosses(outcome.decisions)) {
                evaluation.findings.push_back({ Invariant::POI_KEEPS_TEXT, path.name, loss });
            }
            for (const std::string& pair : overlaps(outcome.decisions)) {
                evaluation.findings.push_back({ Invariant::NO_OVERLAP, path.name, pair });
            }

            evaluation.paths.push_back(std::move(outcome));
        }

        return evaluation;
    }

    std::string describe(const PathOutcome& outcome) {
        std::vector<std::string> lines;
        for (const auto& [key, decision] : outcome.decisions) {
            if (decision.shown()) {
                lines.push_back(std::string(decision.line ? "street " : decision.text ? "poi    " : "icon   ") + decision.name + (decision.line ? " (step " + std::to_string(decision.sizeStep) + ")" : decision.text ? " (side " + std::to_string(decision.variant) + ")" : ""));
            }
        }
        std::sort(lines.begin(), lines.end());
        std::ostringstream out;
        for (const std::string& line : lines) {
            out << "      " << line << "\n";
        }
        return out.str();
    }
}
