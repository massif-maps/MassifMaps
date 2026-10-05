// Decodes a folder of z_x_y.pbf tiles through a CartoCSS project, timed, with the passes it took
// and a hash of what each tile draws - docs/internals/rendering/10-performance.md#decoding-a-tile.
#include "common.h"

#include <mapnikvt/SymbolizerContext.h>
#include <mapnikvt/StyleParameterStore.h>
#include <mapnikvt/MBVTFeatureDecoder.h>
#include <mapnikvt/LayerTileReader.h>
#include <mapnikvt/PredicateUtils.h>
#include <mapnikvt/Rule.h>
#include <mapnikvt/Filter.h>

#include <cartocss/CartoCSSMapLoader.h>

#include <vt/Tile.h>
#include <vt/TileLayer.h>
#include <vt/Transform.h>
#include <vt/TileGeometry.h>
#include <vt/TileLabel.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <tuple>
#include <string>
#include <vector>

using namespace massif;
using namespace massif::cssutils;

namespace {
    using Clock = std::chrono::steady_clock;

    double msSince(Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    class BitmapLoader : public vt::BitmapManager::BitmapLoader {
    public:
        explicit BitmapLoader(std::shared_ptr<AssetLoader> assetLoader) : _assetLoader(std::move(assetLoader)) { }

        virtual std::shared_ptr<const vt::Bitmap> load(const std::string& fileName, float& resolution) const override {
            resolution = 1.0f;
            int width = 0, height = 0, comp = 0;
            unsigned char* buf = nullptr;
            try {
                auto data = _assetLoader->load(fileName);
                buf = stbi_load_from_memory(data->data(), static_cast<int>(data->size()), &width, &height, &comp, 4);
            }
            catch (const std::exception&) {
            }
            // An SVG or a missing file still has to answer, or the symbolizer drops the feature.
            if (!buf) {
                return std::make_shared<vt::Bitmap>(16, 16, std::vector<std::uint32_t>(16 * 16, 0xff000000));
            }
            std::vector<std::uint32_t> pixels(width * height);
            std::memcpy(pixels.data(), buf, width * height * 4);
            stbi_image_free(buf);
            return std::make_shared<vt::Bitmap>(width, height, std::move(pixels));
        }

    private:
        std::shared_ptr<AssetLoader> _assetLoader;
    };

    cglib::mat3x3<float> tileTransform(const vt::TileId& tileId, const vt::TileId& targetTileId) {
        float x0 = 0, y0 = 0, scale = 1;
        for (vt::TileId parent = targetTileId; parent != tileId; parent = parent.getParent()) {
            x0 *= 0.5f; y0 *= 0.5f; scale *= 2;
            x0 += (parent.x - parent.getParent().x * 2) * 0.5f;
            y0 += (parent.y - parent.getParent().y * 2) * 0.5f;
        }
        return cglib::scale3_matrix(cglib::vec3<float>(scale, scale, 1)) * cglib::translate3_matrix(cglib::vec3<float>(-x0, -y0, 1));
    }

    struct Hasher {
        std::uint64_t value = 1469598103934665603ull;

        void add(const void* data, std::size_t size) {
            for (std::size_t i = 0; i < size; i++) {
                value = (value ^ static_cast<const unsigned char*>(data)[i]) * 1099511628211ull;
            }
        }
        template <typename T> void add(const T& item) { add(&item, sizeof(T)); }
        void add(const std::string& str) { add(str.data(), str.size()); }
    };

    // What the tile draws, batch by batch: geometry bytes, pattern, each slot's colour and width at
    // the tile's zoom, label glyphs. Equal only if the style's attachments are too.
    std::uint64_t tileHash(const vt::Tile& tile) {
        Hasher hasher;
        vt::ViewState viewState;
        viewState.zoom = static_cast<float>(tile.getTileId().zoom) + 0.5f;
        for (const std::shared_ptr<vt::TileLayer>& layer : tile.getLayers()) {
            for (const std::shared_ptr<vt::TileGeometry>& geometry : layer->getGeometries()) {
                hasher.add(geometry->getType());
                hasher.add(geometry->getVertexGeometry().data(), geometry->getVertexGeometry().size());
                hasher.add(geometry->getIndices().data(), geometry->getIndices().size() * sizeof(std::uint16_t));
                const vt::TileGeometry::StyleParameters& params = geometry->getStyleParameters();
                for (int i = 0; i < params.parameterCount; i++) {
                    hasher.add(params.colorFuncs[i](viewState).value());
                    hasher.add(params.widthFuncs[i](viewState));
                }
                if (params.pattern) {
                    hasher.add(params.pattern->bitmap->data.data(), params.pattern->bitmap->data.size() * sizeof(std::uint32_t));
                }
            }
            for (const std::shared_ptr<vt::TileLabel>& label : layer->getLabels()) {
                for (const vt::Font::Glyph& glyph : label->getGlyphs()) {
                    hasher.add(glyph.utf32Char);
                }
            }
        }
        return hasher.value;
    }

    // tileHash with the batching taken out, so two styles drawing the same features from different
    // attachments compare equal.
    std::uint64_t contentHash(const vt::Tile& tile) {
        vt::ViewState viewState;
        viewState.zoom = static_cast<float>(tile.getTileId().zoom) + 0.5f;
        std::map<std::pair<int, std::uint64_t>, std::tuple<std::size_t, std::size_t, std::set<std::pair<unsigned, float>>>> groups;
        std::multiset<std::u32string> labels;
        for (const std::shared_ptr<vt::TileLayer>& layer : tile.getLayers()) {
            for (const std::shared_ptr<vt::TileGeometry>& geometry : layer->getGeometries()) {
                const vt::TileGeometry::StyleParameters& params = geometry->getStyleParameters();
                Hasher pattern;
                if (params.pattern) {
                    pattern.add(params.pattern->bitmap->data.data(), params.pattern->bitmap->data.size() * sizeof(std::uint32_t));
                }
                auto& group = groups[std::make_pair(static_cast<int>(geometry->getType()), pattern.value)];
                std::get<0>(group) += geometry->getVertexGeometry().size() / std::max(1, static_cast<int>(geometry->getVertexGeometryLayoutParameters().vertexSize));
                std::get<1>(group) += geometry->getIndicesCount();
                for (int i = 0; i < params.parameterCount; i++) {
                    std::get<2>(group).emplace(params.colorFuncs[i](viewState).value(), params.widthFuncs[i](viewState));
                }
            }
            for (const std::shared_ptr<vt::TileLabel>& label : layer->getLabels()) {
                std::u32string text;
                for (const vt::Font::Glyph& glyph : label->getGlyphs()) {
                    text.push_back(glyph.utf32Char);
                }
                labels.insert(text);
            }
        }
        Hasher hasher;
        for (const auto& group : groups) {
            hasher.add(group.first);
            hasher.add(std::get<0>(group.second));
            hasher.add(std::get<1>(group.second));
            for (const auto& slot : std::get<2>(group.second)) {
                hasher.add(slot);
            }
        }
        for (const std::u32string& text : labels) {
            hasher.add(text.data(), text.size() * sizeof(char32_t));
        }
        return hasher.value;
    }

    struct TileJob {
        std::string path;
        vt::TileId source;
        vt::TileId target;
    };
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: bench-decode project.json tile-dir font.ttf [--runs N] [--overzoom N] [--layers 1] [--param name=value]..." << std::endl;
        return 1;
    }
    std::string projectFile = argv[1], tileDir = argv[2], fontFile = argv[3];
    int runs = 5, overzoom = 0;
    bool listLayers = false;
    std::map<std::string, std::string> paramOverrides;
    for (int i = 4; i + 1 < argc; i += 2) {
        std::string arg = argv[i];
        if (arg == "--runs") {
            runs = std::stoi(argv[i + 1]);
        }
        else if (arg == "--overzoom") {
            overzoom = std::stoi(argv[i + 1]);
        }
        else if (arg == "--layers") {
            listLayers = std::stoi(argv[i + 1]) != 0;
        }
        else if (arg == "--param") {
            std::string kv = argv[i + 1];
            paramOverrides[kv.substr(0, kv.find('='))] = kv.substr(kv.find('=') + 1);
        }
    }

    try {
        auto logger = std::make_shared<Logger>();
        auto [folder, file] = splitProjectPath(projectFile);
        auto assetLoader = std::make_shared<AssetLoader>(folder);

        Clock::time_point loadStart = Clock::now();
        css::CartoCSSMapLoader mapLoader(assetLoader, logger);
        std::shared_ptr<mvt::Map> map = mapLoader.loadMapProject(file);
        double loadMs = msSince(loadStart);

        std::size_t ruleCount = 0;
        for (const std::shared_ptr<mvt::Style>& style : map->getStyles()) {
            ruleCount += style->getRules().size();
        }
        std::cout << "load " << loadMs << " ms, layers " << map->getLayers().size() << ", styles " << map->getStyles().size() << ", rules " << ruleCount << std::endl;

        std::map<std::string, mvt::Value> styleParams;
        for (const auto& param : map->getStyleParameterMap()) {
            auto it = paramOverrides.find(param.first);
            styleParams[param.first] = (it != paramOverrides.end() ? mvt::Value(it->second) : param.second.getDefaultValue());
        }
        auto fontManager = std::make_shared<vt::FontManager>(1024, 1024);
        std::vector<unsigned char> fontData = loadFile(fontFile);
        fontManager->setFontDataLoader([fontData](const std::string&) { return fontData; });
        mvt::SymbolizerContext::Settings settings(256, std::make_shared<mvt::StyleParameterStore>(styleParams), fontManager->getFont("fallback", nullptr));
        mvt::SymbolizerContext context(std::make_shared<vt::BitmapManager>(std::make_shared<BitmapLoader>(assetLoader)), fontManager, std::make_shared<vt::StrokeMap>(128, 512), std::make_shared<vt::GlyphMap>(1024, 1024), settings);
        auto transformer = std::make_shared<vt::DefaultTileTransformer>(1.0f);

        std::vector<TileJob> jobs;
        std::regex re("([0-9]+)_([0-9]+)_([0-9]+)\\.pbf$");
        for (const auto& entry : std::filesystem::directory_iterator(tileDir)) {
            std::string path = entry.path().string();
            std::smatch m;
            if (!std::regex_search(path, m, re)) {
                continue;
            }
            vt::TileId tileId(std::stoi(m[1].str()), std::stoi(m[2].str()), std::stoi(m[3].str()));
            jobs.push_back({ path, tileId, tileId });
            // Overzoomed as the SDK does: the z14 tiles also decode into their (1, 1) descendants
            vt::TileId target = tileId;
            for (int z = 1; z <= overzoom && tileId.zoom == 14; z++) {
                target = target.getChild(1, 1);
                jobs.push_back({ path, tileId, target });
            }
        }
        std::sort(jobs.begin(), jobs.end(), [](const TileJob& a, const TileJob& b) { return std::make_pair(a.target.zoom, a.path) < std::make_pair(b.target.zoom, b.path); });

        std::map<int, std::pair<double, int>> zoomTotals;
        double total = 0;
        for (const TileJob& job : jobs) {
            std::vector<unsigned char> data = loadFile(job.path);
            double best = 1e30;
            std::shared_ptr<vt::Tile> tile;
            for (int run = 0; run < runs; run++) {
                Clock::time_point start = Clock::now();
                mvt::MBVTFeatureDecoder decoder(data, logger);
                decoder.setTransform(tileTransform(job.source, job.target));
                mvt::LayerTileReader reader(map, transformer, context, decoder, logger);
                tile = reader.readTile(job.target, job.target.zoom);
                best = std::min(best, msSince(start));
            }
            // Each style left a rule by TileReader::preFilterStyleRules walks its source layer once
            std::size_t passes = 0, passesWithoutValueTest = 0, visits = 0;
            {
                mvt::ExpressionContext exprContext;
                exprContext.setTileId(job.target);
                exprContext.setAdjustedZoom(job.target.zoom + static_cast<int>(std::lround(map->getSettings().zoomShift(256))));
                exprContext.setStyleParameterStore(context.getSettings().getStyleParameterStore());
                mvt::MBVTFeatureDecoder decoder(data, logger);
                decoder.setTransform(tileTransform(job.source, job.target));
                for (const std::shared_ptr<mvt::Layer>& layer : map->getLayers()) {
                    if (!decoder.hasLayer(layer->getName())) {
                        continue;
                    }
                    std::size_t features = 0;
                    for (auto it = decoder.createLayerFeatureIterator(layer->getName(), nullptr); it->valid(); it->advance()) {
                        features++;
                    }
                    mvt::PredicateFieldValueEvaluator fieldValueEvaluator([&](const std::string& field, const mvt::Value& value) {
                        return decoder.mayHaveFieldValue(layer->getName(), field, value);
                    });
                    for (const std::string& styleName : layer->getStyleNames()) {
                        std::shared_ptr<const mvt::Style> style = map->getStyle(styleName);
                        if (!style) {
                            continue;
                        }
                        bool possible = false, possibleByValue = false;
                        for (const std::shared_ptr<const mvt::Rule>& rule : style->getZoomRules(exprContext.getAdjustedZoom())) {
                            const std::shared_ptr<const mvt::Filter>& filter = rule->getFilter();
                            if (!filter || filter->getType() != mvt::Filter::Type::FILTER || !filter->getPredicate()) {
                                possible = possibleByValue = true;
                            }
                            else if (!bool(!std::visit(mvt::PredicatePreEvaluator(exprContext), *filter->getPredicate()))) {
                                possible = true;
                                possibleByValue = possibleByValue || std::visit(fieldValueEvaluator, *filter->getPredicate());
                            }
                        }
                        passesWithoutValueTest += possible ? 1 : 0;
                        passes += possibleByValue ? 1 : 0;
                        visits += possibleByValue ? features : 0;
                    }
                }
            }
            std::size_t geometries = 0, labels = 0;
            for (const std::shared_ptr<vt::TileLayer>& layer : tile->getLayers()) {
                if (listLayers) {
                    std::cout << "  " << layer->getLayerName() << " geometries " << layer->getGeometries().size() << " labels " << layer->getLabels().size() << std::endl;
                }
                geometries += layer->getGeometries().size();
                labels += layer->getLabels().size();
            }
            std::cout << job.target.zoom << "/" << job.target.x << "/" << job.target.y << " " << best << " ms, passes " << passes << " (" << passesWithoutValueTest << " without the value test), feature visits " << visits << ", tile layers " << tile->getLayers().size() << ", geometry batches " << geometries << ", labels " << labels << ", hash " << std::hex << tileHash(*tile) << ", content " << contentHash(*tile) << std::dec << std::endl;
            zoomTotals[job.target.zoom].first += best;
            zoomTotals[job.target.zoom].second++;
            total += best;
        }
        for (const auto& zoomTotal : zoomTotals) {
            std::cout << "z" << zoomTotal.first << " mean " << zoomTotal.second.first / zoomTotal.second.second << " ms" << std::endl;
        }
        std::cout << "total " << total << " ms over " << jobs.size() << " tiles" << std::endl;
    }
    catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }
    return 0;
}
