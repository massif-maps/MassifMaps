#include "LegendResolver.h"
#include "Map.h"
#include "Layer.h"
#include "Style.h"
#include "Rule.h"
#include "Symbolizer.h"
#include "Properties.h"
#include "ExpressionContext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <regex>
#include <stdexcept>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace massif::mvt {
    namespace {
        // Emitted even at their defaults: what a swatch cannot be drawn without.
        const std::map<std::string, std::vector<std::string>>& getKeyProperties() {
            static const std::map<std::string, std::vector<std::string>> keyProperties = {
                { "point", { "file", "opacity" } },
                { "line", { "stroke", "stroke-width", "stroke-opacity" } },
                { "line-pattern", { "file", "fill", "opacity" } },
                { "polygon", { "fill", "fill-opacity" } },
                { "polygon-pattern", { "file", "fill", "opacity" } },
                { "building", { "fill", "fill-opacity", "height" } },
                { "markers", { "file", "fill", "fill-opacity", "width", "height", "stroke", "stroke-width" } },
                { "text", { "name", "size", "fill", "halo-fill", "halo-radius" } },
                { "shield", { "file", "name", "size", "fill", "halo-fill", "halo-radius" } }
            };
            return keyProperties;
        }

        // vt colours are premultiplied; a legend hands out the CSS colour.
        std::string colorString(const vt::Color& color) {
            float alpha = color.alpha();
            std::array<std::uint8_t, 4> rgba = (alpha > 0.0f ? vt::Color(color[0] / alpha, color[1] / alpha, color[2] / alpha, alpha) : color).rgba8();
            char buf[16];
            if (rgba[3] == 255) {
                std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", rgba[0], rgba[1], rgba[2]);
            }
            else {
                std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", rgba[0], rgba[1], rgba[2], rgba[3]);
            }
            return buf;
        }

        FeatureData::GeometryType parseGeometryType(const std::string& geometry) {
            if (geometry == "point") {
                return FeatureData::GeometryType::POINT_GEOMETRY;
            }
            if (geometry == "line") {
                return FeatureData::GeometryType::LINE_GEOMETRY;
            }
            if (geometry == "polygon") {
                return FeatureData::GeometryType::POLYGON_GEOMETRY;
            }
            throw std::invalid_argument("Legend entry geometry must be point, line or polygon: " + geometry);
        }

        std::string getString(const rapidjson::Value& obj, const char* name) {
            auto it = obj.FindMember(name);
            if (it == obj.MemberEnd() || !it->value.IsString()) {
                throw std::invalid_argument(std::string("Legend entry needs a string '") + name + "'");
            }
            return std::string(it->value.GetString(), it->value.GetStringLength());
        }

        Value fromJSON(const rapidjson::Value& json) {
            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            json.Accept(writer);
            return valueFromJSON(std::string(buffer.GetString(), buffer.GetSize()));
        }

        using JSON = rapidjson::Value;
        using Allocator = rapidjson::Document::AllocatorType;

        const Value* findValue(const LegendSymbolizer& symbolizer, const std::string& name) {
            auto it = symbolizer.values.find(name);
            return it != symbolizer.values.end() ? &it->second : nullptr;
        }

        float getNumber(const LegendSymbolizer& symbolizer, const std::string& name, float defaultValue) {
            const Value* value = findValue(symbolizer, name);
            return value && (std::holds_alternative<long long>(*value) || std::holds_alternative<double>(*value)) ? ValueConverter<float>::convert(*value) : defaultValue;
        }

        std::string getText(const LegendSymbolizer& symbolizer, const std::string& name) {
            const Value* value = findValue(symbolizer, name);
            return value ? ValueConverter<std::string>::convert(*value) : std::string();
        }

        void addNumber(JSON& obj, const char* name, float value, Allocator& allocator) {
            obj.AddMember(JSON(name, allocator), JSON(std::round(value * 100.0) / 100.0), allocator);
        }

        void addText(JSON& obj, const char* name, const std::string& value, Allocator& allocator) {
            obj.AddMember(JSON(name, allocator), JSON(value.c_str(), allocator), allocator);
        }

        void addOpacity(JSON& obj, float opacity, Allocator& allocator) {
            if (opacity < 1.0f) {
                addNumber(obj, "opacity", opacity, allocator);
            }
        }

        void addDashArray(JSON& obj, const std::string& dashArray, Allocator& allocator) {
            JSON dashes(rapidjson::kArrayType);
            std::string::size_type pos = 0;
            while ((pos = dashArray.find_first_of("0123456789.", pos)) != std::string::npos) {
                std::string::size_type end = dashArray.find_first_not_of("0123456789.", pos);
                dashes.PushBack(JSON(std::round(std::stod(dashArray.substr(pos, end - pos)) * 100.0) / 100.0), allocator);
                pos = end;
            }
            if (!dashes.Empty()) {
                obj.AddMember("dasharray", dashes, allocator);
            }
        }

        // One stroke of a line swatch; a gap-width casing is drawn as one line under the fill that covers its gap.
        JSON makeLine(const std::string& color, float width, const LegendSymbolizer& symbolizer, Allocator& allocator) {
            JSON line(rapidjson::kObjectType);
            addText(line, "color", color, allocator);
            addNumber(line, "width", width, allocator);
            addDashArray(line, getText(symbolizer, "stroke-dasharray"), allocator);
            if (float offset = getNumber(symbolizer, "offset", 0.0f)) {
                addNumber(line, "offset", offset, allocator);
            }
            addOpacity(line, getNumber(symbolizer, "stroke-opacity", 1.0f) * symbolizer.styleOpacity, allocator);
            return line;
        }

        // A text or shield's text and the plate behind it; true if it has a plate, i.e. it is a road number.
        bool addLabel(JSON& item, const LegendSymbolizer& symbolizer, Allocator& allocator) {
            std::string name = getText(symbolizer, "name");
            std::string fill = getText(symbolizer, "fill");
            if (!name.empty() && !fill.empty() && !item.HasMember("text")) {
                JSON text(rapidjson::kObjectType);
                addText(text, "value", name, allocator);
                addText(text, "color", fill, allocator);
                float haloRadius = getNumber(symbolizer, "halo-radius", 0.0f);
                std::string haloFill = getText(symbolizer, "halo-fill");
                if (haloRadius > 0.0f && !haloFill.empty()) {
                    addText(text, "halo", haloFill, allocator);
                    addNumber(text, "haloWidth", haloRadius, allocator);
                }
                item.AddMember("text", text, allocator);
            }
            for (const char* prefix : { "background-", "icon-background-" }) {
                std::string color = getText(symbolizer, std::string(prefix) + "fill");
                if (color.empty() || item.HasMember("plate")) {
                    continue;
                }
                JSON plate(rapidjson::kObjectType);
                addText(plate, "color", color, allocator);
                std::string border = getText(symbolizer, std::string(prefix) + "border-fill");
                float borderWidth = getNumber(symbolizer, std::string(prefix) + "border-width", 0.0f);
                if (!border.empty() && borderWidth > 0.0f) {
                    addText(plate, "border", border, allocator);
                    addNumber(plate, "borderWidth", borderWidth, allocator);
                }
                addNumber(plate, "radius", getNumber(symbolizer, std::string(prefix) + "radius", 0.0f), allocator);
                item.AddMember("plate", plate, allocator);
                return true;
            }
            return false;
        }

        JSON makeLines(const std::vector<LegendSymbolizer>& symbolizers, Allocator& allocator) {
            JSON lines(rapidjson::kArrayType);
            for (const LegendSymbolizer& symbolizer : symbolizers) {
                if (symbolizer.type == "line-pattern") {
                    JSON line(rapidjson::kObjectType);
                    addText(line, "pattern", getText(symbolizer, "file"), allocator);
                    addOpacity(line, getNumber(symbolizer, "opacity", 1.0f) * symbolizer.styleOpacity, allocator);
                    lines.PushBack(line, allocator);
                }
                std::string stroke = getText(symbolizer, "stroke");
                if (symbolizer.type != "line" || stroke.empty()) {
                    continue;
                }
                float width = getNumber(symbolizer, "stroke-width", 1.0f);
                float gapWidth = getNumber(symbolizer, "gap-width", 0.0f);
                width = gapWidth > 0.0f ? 2 * width + gapWidth : width;
                float borderWidth = getNumber(symbolizer, "border-width", 0.0f);
                std::string borderColor = getText(symbolizer, "border-color");
                if (borderWidth > 0.0f && !borderColor.empty()) {
                    lines.PushBack(makeLine(borderColor, width + 2 * borderWidth, symbolizer, allocator), allocator);
                }
                lines.PushBack(makeLine(stroke, width, symbolizer, allocator), allocator);
            }
            return lines;
        }

        bool addFill(JSON& swatch, const std::vector<LegendSymbolizer>& symbolizers, Allocator& allocator) {
            JSON fill, pattern, outline;
            for (const LegendSymbolizer& symbolizer : symbolizers) {
                std::string color = getText(symbolizer, "fill");
                if ((symbolizer.type == "polygon" || symbolizer.type == "building") && !color.empty()) {
                    fill.SetObject();
                    addText(fill, "color", color, allocator);
                    addOpacity(fill, getNumber(symbolizer, "fill-opacity", 1.0f) * symbolizer.styleOpacity, allocator);
                }
                else if (symbolizer.type == "polygon-pattern") {
                    pattern.SetString(getText(symbolizer, "file").c_str(), allocator);
                }
                else if (symbolizer.type == "line" && !getText(symbolizer, "stroke").empty()) {
                    outline = makeLine(getText(symbolizer, "stroke"), getNumber(symbolizer, "stroke-width", 1.0f), symbolizer, allocator);
                }
            }
            if (fill.IsNull() && pattern.IsNull() && outline.IsNull()) {
                return false;
            }
            if (!fill.IsNull()) {
                for (auto it = fill.MemberBegin(); it != fill.MemberEnd(); it++) {
                    swatch.AddMember(it->name, it->value, allocator);
                }
            }
            if (!pattern.IsNull()) {
                swatch.AddMember("pattern", pattern, allocator);
            }
            if (!outline.IsNull()) {
                swatch.AddMember("outline", outline, allocator);
            }
            return true;
        }

        /**
         * The swatch an app draws for an item, into 'swatch', and its kind: "fill", "line", "shield" (a road
         * number: a plate, or text on an image), "poi" (icon or marker) or "label"; empty when nothing draws it.
         */
        std::string makeSwatch(JSON& swatch, FeatureData::GeometryType geometryType, const std::vector<LegendSymbolizer>& symbolizers, Allocator& allocator) {
            if (geometryType == FeatureData::GeometryType::POLYGON_GEOMETRY && addFill(swatch, symbolizers, allocator)) {
                return "fill";
            }
            if (geometryType == FeatureData::GeometryType::LINE_GEOMETRY) {
                JSON lines = makeLines(symbolizers, allocator);
                if (!lines.Empty()) {
                    swatch.AddMember("lines", lines, allocator);
                    return "line";
                }
            }

            // Whatever the geometry: a road number is drawn along a line, a label can name an area.
            bool shield = false, poi = false;
            for (const LegendSymbolizer& symbolizer : symbolizers) {
                std::string file = getText(symbolizer, "file");
                if ((symbolizer.type == "point" || symbolizer.type == "markers" || symbolizer.type == "shield") && !file.empty() && !swatch.HasMember("icon")) {
                    addText(swatch, "icon", file, allocator);
                    if (!getText(symbolizer, "color").empty()) {
                        addText(swatch, "iconColor", getText(symbolizer, "color"), allocator);
                    }
                    const Value* unlockImage = findValue(symbolizer, "unlock-image");
                    bool beside = (unlockImage && ValueConverter<bool>::convert(*unlockImage)) || !getText(symbolizer, "anchors").empty();
                    shield = shield || (symbolizer.type == "shield" && !beside && !getText(symbolizer, "name").empty());
                    poi = true;
                }
                else if (symbolizer.type == "markers" && file.empty() && !swatch.HasMember("marker")) {
                    JSON marker(rapidjson::kObjectType);
                    addText(marker, "color", getText(symbolizer, "fill"), allocator);
                    addNumber(marker, "size", getNumber(symbolizer, "width", 10.0f), allocator);
                    if (getNumber(symbolizer, "stroke-width", 0.0f) > 0.0f && !getText(symbolizer, "stroke").empty()) {
                        addText(marker, "border", getText(symbolizer, "stroke"), allocator);
                        addNumber(marker, "borderWidth", getNumber(symbolizer, "stroke-width", 0.0f), allocator);
                    }
                    swatch.AddMember("marker", marker, allocator);
                    poi = true;
                }
                if (symbolizer.type == "shield" && !getText(symbolizer, "icon-name").empty() && !swatch.HasMember("glyph")) {
                    JSON glyph(rapidjson::kObjectType);
                    addText(glyph, "char", getText(symbolizer, "icon-name"), allocator);
                    addText(glyph, "font", getText(symbolizer, "icon-face-name"), allocator);
                    addText(glyph, "color", getText(symbolizer, "icon-fill"), allocator);
                    swatch.AddMember("glyph", glyph, allocator);
                    poi = true;
                }
                if (symbolizer.type == "text" || symbolizer.type == "shield") {
                    shield = addLabel(swatch, symbolizer, allocator) || shield;
                }
            }
            if (shield) {
                return "shield";
            }
            if (poi) {
                return "poi";
            }
            return swatch.HasMember("text") ? "label" : std::string();
        }
    }

    std::vector<LegendSymbolizer> resolveLegendEntry(const Map& map, const std::string& layerName, FeatureData::GeometryType geometryType,
                                                     const std::vector<std::pair<std::string, Value>>& fields, float viewZoom,
                                                     const std::shared_ptr<const StyleParameterStore>& styleParameterStore) {
        ExpressionContext exprContext;
        exprContext.setAdjustedZoom(static_cast<int>(std::floor(viewZoom)));
        exprContext.setStyleParameterStore(styleParameterStore);
        exprContext.setFeatureData(std::make_shared<FeatureData>(0, geometryType, fields));
        vt::ViewState viewState;
        viewState.zoom = viewZoom;

        std::vector<LegendSymbolizer> result;
        // Several layers can read one source layer, each drawing its own attachments.
        for (const std::shared_ptr<Layer>& layer : map.getLayers()) {
            if (layer->getName() != layerName) {
                continue;
            }
            for (const std::string& styleName : layer->getStyleNames()) {
                const std::shared_ptr<Style>& style = map.getStyle(styleName);
                if (!style) {
                    continue;
                }
                const std::vector<std::shared_ptr<const Rule>>& rules = style->getZoomRules(exprContext.getAdjustedZoom());
                for (const std::shared_ptr<const Symbolizer>& symbolizer : style->findFeatureSymbolizers(rules, exprContext)) {
                    LegendSymbolizer legendSymbolizer;
                    legendSymbolizer.type = symbolizer->getTypeName();
                    if (legendSymbolizer.type.empty()) {
                        continue;
                    }
                    legendSymbolizer.style = styleName;
                    legendSymbolizer.styleOpacity = style->getOpacity();

                    auto keyIt = getKeyProperties().find(legendSymbolizer.type);
                    for (const std::string& name : symbolizer->getPropertyNames()) {
                        const Property* property = symbolizer->getProperty(name);
                        if (!property) {
                            continue;
                        }
                        bool key = keyIt != getKeyProperties().end() && std::find(keyIt->second.begin(), keyIt->second.end(), name) != keyIt->second.end();
                        if (!property->isDefined() && !key) {
                            continue;
                        }
                        try {
                            Value value = property->evaluate(exprContext, viewState);
                            if (std::holds_alternative<std::monostate>(value)) {
                                continue;
                            }
                            if (dynamic_cast<const ColorFunctionProperty*>(property) || dynamic_cast<const ColorProperty*>(property)) {
                                vt::Color color = Property::convertColor(value);
                                if (color.alpha() <= 0.0f) {
                                    continue; // draws nothing
                                }
                                value = Value(colorString(color));
                            }
                            legendSymbolizer.values[name] = std::move(value);
                        }
                        catch (const std::exception&) {
                            // Unreadable when a tile decodes too, where the symbolizer is dropped.
                        }
                    }
                    result.push_back(std::move(legendSymbolizer));
                }
            }
        }
        return result;
    }

    std::string resolveLegend(const Map& map, const std::string& specJSON, const std::shared_ptr<const StyleParameterStore>& styleParameterStore) {
        rapidjson::Document spec;
        if (spec.Parse(specJSON.c_str()).HasParseError() || !spec.IsObject()) {
            throw std::invalid_argument("Legend spec is not a JSON object");
        }
        rapidjson::Document legend(rapidjson::kObjectType);
        Allocator& allocator = legend.GetAllocator();

        auto readZoom = [](const JSON& obj, float zoom) {
            auto it = obj.FindMember("zoom");
            return it != obj.MemberEnd() && it->value.IsNumber() ? it->value.GetFloat() : zoom;
        };
        // Everything a spec carries besides the features passes through: labels, ids, an app's own keys.
        auto copyExcept = [&allocator](JSON& to, const JSON& from, std::initializer_list<const char*> skip) {
            for (auto it = from.MemberBegin(); it != from.MemberEnd(); it++) {
                if (std::none_of(skip.begin(), skip.end(), [&it](const char* name) { return it->name == name; })) {
                    to.AddMember(JSON(it->name, allocator), JSON(it->value, allocator), allocator);
                }
            }
        };

        auto sectionsIt = spec.FindMember("sections");
        if (sectionsIt == spec.MemberEnd() || !sectionsIt->value.IsArray()) {
            throw std::invalid_argument("Legend spec needs a 'sections' array");
        }
        copyExcept(legend, spec, { "sections", "zoom" });
        float specZoom = readZoom(spec, -1.0f);

        JSON sections(rapidjson::kArrayType);
        for (const JSON& section : sectionsIt->value.GetArray()) {
            auto itemsIt = section.IsObject() ? section.FindMember("items") : section.MemberEnd();
            if (!section.IsObject() || itemsIt == section.MemberEnd() || !itemsIt->value.IsArray()) {
                throw std::invalid_argument("Legend section needs an 'items' array");
            }
            float sectionZoom = readZoom(section, specZoom);
            JSON items(rapidjson::kArrayType);
            for (const JSON& entry : itemsIt->value.GetArray()) {
                if (!entry.IsObject()) {
                    throw std::invalid_argument("Legend item is not an object");
                }
                float zoom = readZoom(entry, sectionZoom);
                if (zoom < 0) {
                    throw std::invalid_argument("Legend item has no zoom, nor has its section or the spec");
                }
                std::vector<std::pair<std::string, Value>> fields;
                auto propertiesIt = entry.FindMember("properties");
                if (propertiesIt != entry.MemberEnd()) {
                    Value properties = fromJSON(propertiesIt->value);
                    if (auto object = std::get_if<std::shared_ptr<const ValueObject>>(&properties)) {
                        fields.assign((*object)->members.begin(), (*object)->members.end());
                    }
                }
                FeatureData::GeometryType geometryType = parseGeometryType(getString(entry, "geometry"));
                std::vector<LegendSymbolizer> symbolizers = resolveLegendEntry(map, getString(entry, "layer"), geometryType, fields, zoom, styleParameterStore);
                // Only the attachments that make the item: an MTB overlay, not the path drawn under it.
                auto attachmentIt = entry.FindMember("attachment");
                if (attachmentIt != entry.MemberEnd() && attachmentIt->value.IsString()) {
                    std::regex attachment(attachmentIt->value.GetString());
                    symbolizers.erase(std::remove_if(symbolizers.begin(), symbolizers.end(), [&attachment](const LegendSymbolizer& symbolizer) {
                        std::string::size_type pos = symbolizer.style.find("::");
                        return !std::regex_search(pos == std::string::npos ? std::string() : symbolizer.style.substr(pos + 2), attachment);
                    }), symbolizers.end());
                }

                JSON swatch(rapidjson::kObjectType);
                std::string kind = makeSwatch(swatch, geometryType, symbolizers, allocator);
                if (kind.empty()) {
                    continue;
                }
                JSON item(rapidjson::kObjectType);
                copyExcept(item, entry, { "layer", "geometry", "properties", "zoom", "attachment" });
                addText(item, "kind", kind, allocator);
                for (auto it = swatch.MemberBegin(); it != swatch.MemberEnd(); it++) {
                    item.AddMember(it->name, it->value, allocator);
                }
                items.PushBack(item, allocator);
            }
            if (!items.Empty()) {
                JSON outSection(rapidjson::kObjectType);
                copyExcept(outSection, section, { "items", "zoom" });
                outSection.AddMember("items", items, allocator);
                sections.PushBack(outSection, allocator);
            }
        }
        legend.AddMember("sections", sections, allocator);

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        legend.Accept(writer);
        return std::string(buffer.GetString(), buffer.GetSize());
    }
}
