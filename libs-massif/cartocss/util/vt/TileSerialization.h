#ifndef _CARTO_VT_TILESERIALIZATION_H_
#define _CARTO_VT_TILESERIALIZATION_H_

#include <vt/Tile.h>

#include <cstdlib>

#include <boost/archive/xml_iarchive.hpp>
#include <boost/archive/xml_oarchive.hpp>
#include <boost/serialization/list.hpp>
#include <boost/serialization/array.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/optional.hpp>
#include <boost/serialization/version.hpp>
#include <boost/serialization/shared_ptr.hpp>
#include <boost/serialization/split_member.hpp>

namespace boost::serialization {
    template <class Archive>
    void serialize(Archive& ar, std::vector<std::uint8_t>& data, const unsigned int version) {
        std::size_t size = data.size();
        std::string serialData;
        serialData.reserve(size * 2);
        for (std::uint8_t val : data) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%02x", val);
            serialData += buf;
        }
        ar& make_nvp("size", size);
        ar& make_nvp("data", serialData);
    }

    template <class Archive>
    void serialize(Archive& ar, std::vector<std::uint32_t>& data, const unsigned int version) {
        std::size_t size = data.size();
        std::string serialData;
        serialData.reserve(size * 2);
        for (std::uint32_t val : data) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%08x", val);
            serialData += buf;
        }
        ar& make_nvp("size", size);
        ar& make_nvp("data", serialData);
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::VertexArray<std::uint8_t>& data, const unsigned int version) {
        std::size_t size = data.size();
        std::string serialData;
        serialData.reserve(size * 2);
        for (std::uint8_t val : data) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%02x", val);
            serialData += buf;
        }
        ar & make_nvp("size", size);
        ar & make_nvp("data", serialData);
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::VertexArray<std::uint16_t>& data, const unsigned int version) {
        std::size_t size = data.size();
        std::string serialData;
        serialData.reserve(size * 4);
        for (std::uint16_t val : data) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%04x", val);
            serialData += buf;
        }
        ar & make_nvp("size", size);
        ar & make_nvp("data", serialData);
    }

    template <class Archive, class T, size_t N>
    void serialize(Archive& ar, cglib::vec<T, N>& vec, const unsigned int version) {
        for (int i = 0; i < N; i++) {
            char name[64];
            std::snprintf(name, sizeof(name), "v%d", i);
            auto value = vec(i);
            ar & make_nvp(name, value);
        }
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::Color& color, const unsigned int version) {
        for (int i = 0; i < 4; i++) {
            char name[64];
            std::snprintf(name, sizeof(name), "c%d", i);
            auto value = color[i];
            ar & make_nvp(name, value);
        }
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::Transform& transform, const unsigned int version) {
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                char name[64];
                std::snprintf(name, sizeof(name), "m%d_%d", i, i);
                auto value = transform.matrix3()(i, j);
                ar & make_nvp(name, value);
            }
        }
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::Bitmap& bitmap, const unsigned int version) {
        ar & make_nvp("width", bitmap.width);
        ar & make_nvp("height", bitmap.height);
        ar & make_nvp("data", bitmap.data);
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::BitmapPattern& bitmapPattern, const unsigned int version) {
        ar & make_nvp("width_scale", bitmapPattern.widthScale);
        ar & make_nvp("height_scale", bitmapPattern.heightScale);
        ar & make_nvp("bitmap", bitmapPattern.bitmap);
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::GlyphMap& glyphMap, const unsigned int version) {
        ar & make_nvp("bitmap_pattern", glyphMap.getBitmapPattern());
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::GlyphMap::Glyph& glyph, const unsigned int version) {
        ar & make_nvp("sdf_mode", glyph.mode);
        ar & make_nvp("x", glyph.x);
        ar & make_nvp("y", glyph.y);
        ar & make_nvp("width", glyph.width);
        ar & make_nvp("height", glyph.height);
        ar & make_nvp("origin", glyph.origin);
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::Font::Glyph& glyph, const unsigned int version) {
        ar & make_nvp("utf32_char", glyph.utf32Char);
        ar & make_nvp("code_point", glyph.codePoint);
        ar & make_nvp("base_glyph", glyph.baseGlyph);
        ar & make_nvp("size", glyph.size);
        ar & make_nvp("offset", glyph.offset);
        ar & make_nvp("advance", glyph.advance);
    }

    template <class Archive, class T>
    void serialize(Archive& ar, carto::vt::UnaryFunction<T, carto::vt::ViewState>& func, const unsigned int version) {
        auto value = func.value();
        ar & make_nvp("value", value);
        if (func.function()) {
            std::string funcPtr = "*FUNC*";
            ar & make_nvp("func", funcPtr);
        }
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::TileId& tileId, const unsigned int version) {
        ar & make_nvp("zoom", tileId.zoom);
        ar & make_nvp("x", tileId.x);
        ar & make_nvp("y", tileId.y);
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::TileBackground& tileBackground, const unsigned int version) {
        auto color = tileBackground.getColor();
        ar & make_nvp("color", color);
        ar & make_nvp("pattern", tileBackground.getPattern());
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::TileBitmap& tileBitmap, const unsigned int version) {
        auto type = static_cast<int>(tileBitmap.getType());
        auto format = static_cast<int>(tileBitmap.getFormat());
        auto width = tileBitmap.getWidth();
        auto height = tileBitmap.getHeight();
        ar & make_nvp("type", type);
        ar & make_nvp("format", format);
        ar & make_nvp("width", width);
        ar & make_nvp("height", height);
        ar & make_nvp("data", tileBitmap.getData());
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::TileLabel::Style& style, const unsigned int version) {
        auto orientation = static_cast<int>(style.orientation);
        ar & make_nvp("orientation", orientation);
        ar & make_nvp("color_func", style.colorFunc);
        ar & make_nvp("size_func", style.sizeFunc);
        ar & make_nvp("halo_color_func", style.haloColorFunc);
        ar & make_nvp("halo_radius_func", style.haloRadiusFunc);
        ar & make_nvp("autoflip", style.autoflip);
        ar & make_nvp("scale", style.scale);
        ar & make_nvp("ascent", style.ascent);
        ar & make_nvp("descent", style.descent);
        if (style.transform) {
            ar & make_nvp("transform", *style.transform);
        }
        ar & make_nvp("glyphmap", style.glyphMap);
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::TileLabel::PlacementInfo& placementInfo, const unsigned int version) {
        ar & make_nvp("priority", placementInfo.priority);
        ar & make_nvp("minimumGroupDistance", placementInfo.minimumGroupDistance);
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::TileLabel& tileLabel, const unsigned int version) {
        auto localId = tileLabel.getLocalId();
        auto globalId = tileLabel.getGlobalId();
        auto groupId = tileLabel.getGroupId();
        ar & make_nvp("local_id", localId);
        ar & make_nvp("global_id", globalId);
        ar & make_nvp("group_id", groupId);
        ar & make_nvp("glyphs", tileLabel.getGlyphs());
        if (tileLabel.getPosition()) {
            ar & make_nvp("position", *tileLabel.getPosition());
        }
        ar & make_nvp("vertices", tileLabel.getVertices());
        ar & make_nvp("style", tileLabel.getStyle());
        ar & make_nvp("placement_info", tileLabel.getPlacementInfo());
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::TileGeometry::StyleParameters& styleParams, const unsigned int version) {
        auto compOp = static_cast<int>(styleParams.compOp);
        ar & make_nvp("parameter_count", styleParams.parameterCount);
        for (int i = 0; i < styleParams.parameterCount; i++) {
            ar & make_nvp("color_func", styleParams.colorFuncs[i]);
            ar & make_nvp("width_func", styleParams.widthFuncs[i]);
            ar & make_nvp("offset_func", styleParams.offsetFuncs[i]);
            ar & make_nvp("stroke_scale", styleParams.strokeScales[i]);
        }
        ar & make_nvp("pattern", styleParams.pattern);
        if (styleParams.translate) {
            ar & make_nvp("translate", *styleParams.translate);
        }
        ar & make_nvp("compOp", compOp);
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::TileGeometry::VertexGeometryLayoutParameters& layoutParams, const unsigned int version) {
        ar & make_nvp("vertex_size", layoutParams.vertexSize);
        ar & make_nvp("dimensions", layoutParams.dimensions);
        ar & make_nvp("coord_offset", layoutParams.coordOffset);
        ar & make_nvp("attribs_offset", layoutParams.attribsOffset);
        ar & make_nvp("tex_coord_offset", layoutParams.texCoordOffset);
        ar & make_nvp("normal_offset", layoutParams.normalOffset);
        ar & make_nvp("binormal_offset", layoutParams.binormalOffset);
        ar & make_nvp("height_offset", layoutParams.heightOffset);
        ar & make_nvp("coord_scale", layoutParams.coordScale);
        ar & make_nvp("tex_coord_scale", layoutParams.texCoordScale);
        ar & make_nvp("binormal_scale", layoutParams.binormalScale);
        ar & make_nvp("height_scale", layoutParams.heightScale);
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::TileGeometry& tileGeometry, const unsigned int version) {
        int type = static_cast<int>(tileGeometry.getType());
        float geometryScale = tileGeometry.getGeometryScale();
        ar & make_nvp("type", type);
        ar & make_nvp("geometry_scale", geometryScale);
        ar & make_nvp("style_parameters", tileGeometry.getStyleParameters());
        ar & make_nvp("vertex_geometry_layout_parameters", tileGeometry.getVertexGeometryLayoutParameters());
        ar & make_nvp("vertex_indices", tileGeometry.getIndices());
        ar & make_nvp("vertex_geometry", tileGeometry.getVertexGeometry());
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::TileLayer& tileLayer, const unsigned int version) {
        int layerIndex = tileLayer.getLayerIndex();
        ar & make_nvp("layer_name", tileLayer.getLayerName());
        ar & make_nvp("layer_index", layerIndex);
        if (tileLayer.getCompOp()) {
            int compOp = static_cast<int>(*tileLayer.getCompOp());
            ar & make_nvp("comp_op", compOp);
        }
        ar & make_nvp("backgrounds", tileLayer.getBackgrounds());
        ar & make_nvp("bitmaps", tileLayer.getBitmaps());
        ar & make_nvp("geometries", tileLayer.getGeometries());
        ar & make_nvp("labels", tileLayer.getLabels());
    }

    template <class Archive>
    void serialize(Archive& ar, carto::vt::Tile& tile, const unsigned int version) {
        float tileSize = tile.getTileSize();
        ar & make_nvp("tile_id", tile.getTileId());
        ar & make_nvp("tile_size", tileSize);
        ar & make_nvp("layers", tile.getLayers());
    }
}

#endif
