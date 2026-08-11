// Host bench for CartoCSSCompiler::compileLayer: loads a style project the same way
// CartoCSSMapLoader does (extends chain, styles list, constants) and times the compile
// per layer. Builds on the host, so a compiler change can be timed and - with CSSBENCH_DUMP -
// checked for an identical result without a device round trip.
//
//   R=<mobile-sdk checkout>
//   clang++ -O2 -w -std=c++17 -o cssbench $R/libs-carto/cartocss/test/CompileBench.cpp \
//     $R/libs-carto/cartocss/src/cartocss/{CartoCSSParser,CartoCSSCompiler,Expression,Predicate}.cpp \
//     $R/libs-carto/mapnikvt/src/mapnikvt/{StringUtils,CSSColorParser,ParserUtils,ParseTables,\
// ExpressionContext,TransformUtils,Predicate,Feature,ExpressionUtils,PredicateUtils,Expression,Value}.cpp \
//     -I $R/libs-carto/cartocss/src -I $R/libs-carto/mapnikvt/src -I $R/libs-carto/vt/src \
//     -I $R/libs-external -I $R/libs-external/picojson -I $R/libs-external/boost \
//     -I $R/libs-external/stdext -I $R/libs-external/cglib -I $R/libs-external/utf8/source
//
//   ./cssbench <style-dir> <project.json> [repeats] [layer]
//   CSSBENCH_DUMP=/tmp/after.txt ./cssbench ... && diff /tmp/before.txt /tmp/after.txt

#include "cartocss/CartoCSSParser.h"
#include "cartocss/CartoCSSCompiler.h"

#include <picojson/picojson.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <memory>
#include <cstdlib>
#include <ostream>

using namespace carto::css;

namespace {
    std::string g_baseDir;

    std::string loadFile(const std::string& name) {
        std::string path = name;
        if (path.compare(0, 2, "./") == 0) {
            path = path.substr(2);
        }
        std::ifstream file(g_baseDir + "/" + path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("could not open " + g_baseDir + "/" + path);
        }
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    Value convertJSONValue(const picojson::value& value) {
        if (value.is<std::string>()) return Value(value.get<std::string>());
        if (value.is<bool>()) return Value(value.get<bool>());
        if (value.is<std::int64_t>()) return Value(static_cast<long long>(value.get<std::int64_t>()));
        if (value.is<double>()) return Value(value.get<double>());
        return Value();
    }

    picojson::value loadMapDocument(const std::string& fileName) {
        picojson::value mapDoc;
        std::string json = loadFile(fileName);
        std::string err = picojson::parse(mapDoc, json);
        if (!err.empty()) {
            throw std::runtime_error("json error: " + err);
        }
        if (mapDoc.contains("extends")) {
            picojson::value extendsMapDoc = loadMapDocument(mapDoc.get("extends").get<std::string>());
            mapDoc.swap(extendsMapDoc);
            const picojson::object& overrideObj = extendsMapDoc.get<picojson::object>();
            const std::set<std::string> mergeCases { "nutiparameters", "constants" };
            for (auto it = overrideObj.begin(); it != overrideObj.end(); it++) {
                if (mergeCases.count(it->first) && mapDoc.contains(it->first)) {
                    picojson::value& subObj = mapDoc.get(it->first);
                    for (const auto& sub : it->second.get<picojson::object>()) {
                        subObj.set(sub.first, sub.second);
                    }
                } else {
                    mapDoc.set(it->first, it->second);
                }
            }
        }
        return mapDoc;
    }

    // Structural digest of a compile result: enough to catch a changed set of property sets,
    // a changed order, changed filters or changed winning properties.
    std::string predicateDigest(const Predicate& pred) {
        std::ostringstream ss;
        ss << "p" << pred.index();
        if (auto p = std::get_if<LayerPredicate>(&pred)) ss << ":" << p->getLayerName();
        if (auto p = std::get_if<ClassPredicate>(&pred)) ss << ":" << p->getClass();
        if (auto p = std::get_if<AttachmentPredicate>(&pred)) ss << ":" << p->getAttachment();
        if (auto p = std::get_if<OpPredicate>(&pred)) ss << ":" << static_cast<int>(p->getOp()) << ":" << p->getFieldOrVar().getName();
        if (auto p = std::get_if<OpConstPredicate>(&pred)) ss << ":" << static_cast<int>(p->getOp()) << ":" << p->getFieldOrVar().getName();
        if (auto p = std::get_if<OpNutiPredicate>(&pred)) ss << ":" << static_cast<int>(p->getOp()) << ":" << p->getFieldOrVar().getName() << ":" << p->getFieldOrVar2().getName();
        return ss.str();
    }

    void dumpLayer(std::ostream& os, const std::string& layerName, const std::map<std::pair<int, int>, std::list<AttachmentPropertySets>>& layerZoomAttachments) {
        for (const auto& zoomRange : layerZoomAttachments) {
            for (const AttachmentPropertySets& attachment : zoomRange.second) {
                for (const PropertySet& propertySet : attachment.getPropertySets()) {
                    os << layerName << " [" << zoomRange.first.first << "," << zoomRange.first.second << "] " << attachment.getAttachment() << " |";
                    for (const auto& filter : propertySet.getFilters()) {
                        os << " " << predicateDigest(*filter);
                    }
                    os << " |";
                    for (const auto& property : propertySet.getProperties()) {
                        const Property::RuleSpecificity& s = property->getSpecificity();
                        os << " " << property->getField() << "(" << std::get<0>(s) << "," << std::get<1>(s) << "," << std::get<2>(s) << "," << std::get<3>(s) << ")";
                    }
                    os << "\n";
                }
            }
        }
    }

    double millis(std::chrono::steady_clock::time_point t0, std::chrono::steady_clock::time_point t1) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() / 1.0e6;
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: cssbench <style-dir> <project.json> [repeats] [layer]\n");
        return 1;
    }
    g_baseDir = argv[1];
    std::string projectName = argv[2];
    int repeats = argc > 3 ? std::atoi(argv[3]) : 3;
    std::string onlyLayer = argc > 4 ? argv[4] : std::string();

    picojson::value mapDoc = loadMapDocument(projectName);

    std::vector<std::string> layerNames;
    for (const picojson::value& v : mapDoc.get("layers").get<picojson::array>()) {
        layerNames.insert(layerNames.begin(), v.get<std::string>());
    }

    std::map<std::string, Value> baseConstantFieldMap;
    if (mapDoc.contains("constants")) {
        for (const auto& c : mapDoc.get("constants").get<picojson::object>()) {
            baseConstantFieldMap[c.first] = convertJSONValue(c.second);
        }
    }

    // Parse the stylesheet fragments once - the parse is not what is measured here.
    auto t0 = std::chrono::steady_clock::now();
    std::vector<StyleSheet::Element> elements;
    for (const picojson::value& v : mapDoc.get("styles").get<picojson::array>()) {
        StyleSheet mss = CartoCSSParser::parse(loadFile(v.get<std::string>()));
        elements.insert(elements.end(), mss.getElements().begin(), mss.getElements().end());
    }
    StyleSheet styleSheet(elements);
    auto t1 = std::chrono::steady_clock::now();
    std::printf("parse: %.1f ms, %zu elements, %zu layers\n", millis(t0, t1), elements.size(), layerNames.size());

    std::unique_ptr<std::ofstream> dumpFile;
    if (const char* dumpPath = std::getenv("CSSBENCH_DUMP")) {
        dumpFile = std::make_unique<std::ofstream>(dumpPath);
    }

    std::map<std::string, double> layerBest;
    double best = 1e30;
    for (int repeat = 0; repeat < repeats; repeat++) {
        std::map<std::string, double> layerMs;
        auto r0 = std::chrono::steady_clock::now();
        for (const std::string& layerName : layerNames) {
            if (!onlyLayer.empty() && layerName != onlyLayer) {
                continue;
            }
            std::map<std::string, Value> constantFieldMap = baseConstantFieldMap;
            std::map<std::pair<int, int>, std::list<AttachmentPropertySets>> layerZoomAttachments;
            auto l0 = std::chrono::steady_clock::now();
            CartoCSSCompiler compiler;
            compiler.compileLayer(styleSheet, layerName, 0, 24 + 1, layerZoomAttachments, constantFieldMap);
            auto l1 = std::chrono::steady_clock::now();
            layerMs[layerName] = millis(l0, l1);
            if (repeat == 0 && dumpFile) {
                dumpLayer(*dumpFile, layerName, layerZoomAttachments);
            }
        }
        auto r1 = std::chrono::steady_clock::now();
        double total = millis(r0, r1);
        std::printf("run %d: %.1f ms\n", repeat, total);
        if (total < best) {
            best = total;
            layerBest = layerMs;
        }
    }

    std::vector<std::pair<double, std::string>> sorted;
    for (const auto& l : layerBest) {
        sorted.emplace_back(l.second, l.first);
    }
    std::sort(sorted.rbegin(), sorted.rend());
    std::printf("\nbest total: %.1f ms\n", best);
    for (std::size_t i = 0; i < sorted.size() && i < 8; i++) {
        std::printf("  %-24s %6.1f ms\n", sorted[i].second.c_str(), sorted[i].first);
    }
    return 0;
}
