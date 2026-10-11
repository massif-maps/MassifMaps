#include "GLBParser.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>

#include <cglib/mat.h>
#include <rapidjson/document.h>

namespace massif::vt {
    namespace {
        using JsonValue = rapidjson::Value;

        constexpr std::uint32_t GLB_MAGIC = 0x46546C67;
        constexpr std::uint32_t CHUNK_JSON = 0x4E4F534A;
        constexpr std::uint32_t CHUNK_BIN = 0x004E4942;
        constexpr int MAX_NODE_DEPTH = 64;

        std::uint32_t readU32(const unsigned char* p) {
            return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
        }

        int getInt(const JsonValue& obj, const char* key, int defaultValue) {
            auto it = obj.FindMember(key);
            return it != obj.MemberEnd() && it->value.IsInt() ? it->value.GetInt() : defaultValue;
        }

        double getNumber(const JsonValue& obj, const char* key, double defaultValue) {
            auto it = obj.FindMember(key);
            return it != obj.MemberEnd() && it->value.IsNumber() ? it->value.GetDouble() : defaultValue;
        }

        const JsonValue* getElement(const JsonValue& doc, const char* key, int index) {
            auto it = doc.FindMember(key);
            if (index < 0 || it == doc.MemberEnd() || !it->value.IsArray() || index >= static_cast<int>(it->value.Size())) {
                return nullptr;
            }
            const JsonValue& element = it->value[index];
            return element.IsObject() ? &element : nullptr;
        }

        float linearToDisplay(float c) {
            return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
        }

        std::uint32_t packColor(float r, float g, float b, float a) {
            auto byte = [](float c) { return static_cast<std::uint32_t>(std::lround(std::min(1.0f, std::max(0.0f, c)) * 255.0f)); };
            return byte(r) | (byte(g) << 8) | (byte(b) << 16) | (byte(a) << 24);
        }

        class Reader {
        public:
            Reader(const JsonValue& doc, const unsigned char* bin, std::size_t binSize, const ModelImageDecoder& imageDecoder, std::string& error) : _doc(doc), _bin(bin), _binSize(binSize), _imageDecoder(imageDecoder), _error(error) { }

            // Reads an accessor as doubles, `components` per element; normalized integers map to [0, 1] / [-1, 1]
            bool readAccessor(int index, std::vector<double>& values, int& components) const {
                const JsonValue* accessor = getElement(_doc, "accessors", index);
                if (!accessor) {
                    return fail("missing accessor");
                }
                if (accessor->HasMember("sparse")) {
                    return fail("sparse accessors are not supported");
                }
                std::string type = (*accessor).HasMember("type") && (*accessor)["type"].IsString() ? (*accessor)["type"].GetString() : "";
                components = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : 0;
                int componentType = getInt(*accessor, "componentType", 0);
                int componentSize = (componentType == 5120 || componentType == 5121) ? 1 : (componentType == 5122 || componentType == 5123) ? 2 : (componentType == 5125 || componentType == 5126) ? 4 : 0;
                int count = getInt(*accessor, "count", -1);
                if (components == 0 || componentSize == 0 || count < 0) {
                    return fail("unsupported accessor layout");
                }
                const JsonValue* view = getElement(_doc, "bufferViews", getInt(*accessor, "bufferView", -1));
                if (!view || getInt(*view, "buffer", 0) != 0 || !_bin) {
                    return fail("accessor without a GLB buffer view");
                }
                std::size_t elementSize = static_cast<std::size_t>(components) * componentSize;
                std::size_t stride = getInt(*view, "byteStride", 0) > 0 ? static_cast<std::size_t>(getInt(*view, "byteStride", 0)) : elementSize;
                std::size_t viewOffset = static_cast<std::size_t>(std::max(0, getInt(*view, "byteOffset", 0)));
                std::size_t viewLength = static_cast<std::size_t>(std::max(0, getInt(*view, "byteLength", 0)));
                std::size_t start = viewOffset + static_cast<std::size_t>(std::max(0, getInt(*accessor, "byteOffset", 0)));
                if (viewOffset + viewLength > _binSize || (count > 0 && start + stride * (count - 1) + elementSize > viewOffset + viewLength)) {
                    return fail("accessor out of bounds");
                }
                bool normalized = (*accessor).HasMember("normalized") && (*accessor)["normalized"].IsBool() && (*accessor)["normalized"].GetBool();

                values.resize(static_cast<std::size_t>(count) * components);
                for (int i = 0; i < count; i++) {
                    const unsigned char* element = _bin + start + stride * i;
                    for (int c = 0; c < components; c++) {
                        const unsigned char* p = element + c * componentSize;
                        double value = 0;
                        switch (componentType) {
                        case 5120: { std::int8_t v; std::memcpy(&v, p, 1); value = normalized ? std::max(v / 127.0, -1.0) : v; break; }
                        case 5121: { std::uint8_t v; std::memcpy(&v, p, 1); value = normalized ? v / 255.0 : v; break; }
                        case 5122: { std::int16_t v; std::memcpy(&v, p, 2); value = normalized ? std::max(v / 32767.0, -1.0) : v; break; }
                        case 5123: { std::uint16_t v; std::memcpy(&v, p, 2); value = normalized ? v / 65535.0 : v; break; }
                        case 5125: { std::uint32_t v; std::memcpy(&v, p, 4); value = v; break; }
                        default: { float v; std::memcpy(&v, p, 4); value = v; break; }
                        }
                        values[static_cast<std::size_t>(i) * components + c] = value;
                    }
                }
                return true;
            }

            bool readMaterials(std::vector<ModelMaterial>& materials) const {
                auto it = _doc.FindMember("materials");
                if (it == _doc.MemberEnd() || !it->value.IsArray()) {
                    return true;
                }
                std::map<int, std::shared_ptr<const Bitmap>> images;
                for (const JsonValue& source : it->value.GetArray()) {
                    ModelMaterial material;
                    if (!source.IsObject()) {
                        materials.push_back(material);
                        continue;
                    }
                    auto pbr = source.FindMember("pbrMetallicRoughness");
                    if (pbr != source.MemberEnd() && pbr->value.IsObject()) {
                        auto factor = pbr->value.FindMember("baseColorFactor");
                        if (factor != pbr->value.MemberEnd() && factor->value.IsArray() && factor->value.Size() == 4) {
                            const JsonValue& f = factor->value;
                            material.baseColor = Color(linearToDisplay(f[0].GetFloat()), linearToDisplay(f[1].GetFloat()), linearToDisplay(f[2].GetFloat()), f[3].GetFloat());
                        }
                        auto textureInfo = pbr->value.FindMember("baseColorTexture");
                        if (textureInfo != pbr->value.MemberEnd() && textureInfo->value.IsObject()) {
                            if (getInt(textureInfo->value, "texCoord", 0) != 0) {
                                return fail("only TEXCOORD_0 is supported");
                            }
                            const JsonValue* texture = getElement(_doc, "textures", getInt(textureInfo->value, "index", -1));
                            int imageIndex = texture ? getInt(*texture, "source", -1) : -1;
                            auto imageIt = images.find(imageIndex);
                            if (imageIt == images.end()) {
                                imageIt = images.emplace(imageIndex, decodeImage(imageIndex)).first;
                            }
                            material.baseColorTexture = imageIt->second;
                        }
                    }
                    std::string alphaMode = source.HasMember("alphaMode") && source["alphaMode"].IsString() ? source["alphaMode"].GetString() : "OPAQUE";
                    if (alphaMode == "MASK") {
                        material.alphaCutoff = static_cast<float>(getNumber(source, "alphaCutoff", 0.5));
                    }
                    material.blend = alphaMode == "BLEND";
                    material.doubleSided = source.HasMember("doubleSided") && source["doubleSided"].IsBool() && source["doubleSided"].GetBool();
                    materials.push_back(material);
                }
                return true;
            }

            bool readNode(int index, const cglib::mat4x4<double>& parentMatrix, int depth, Model& model, std::map<int, std::size_t>& meshIndices) const {
                const JsonValue* node = getElement(_doc, "nodes", index);
                if (!node || depth > MAX_NODE_DEPTH) {
                    return fail("invalid node hierarchy");
                }
                cglib::mat4x4<double> matrix = parentMatrix * nodeMatrix(*node);
                if (node->HasMember("mesh")) {
                    const JsonValue* mesh = getElement(_doc, "meshes", getInt(*node, "mesh", -1));
                    if (!mesh || !mesh->HasMember("primitives") || !(*mesh)["primitives"].IsArray()) {
                        return fail("invalid mesh");
                    }
                    for (const JsonValue& primitive : (*mesh)["primitives"].GetArray()) {
                        if (!primitive.IsObject() || getInt(primitive, "mode", 4) != 4) {
                            continue; // points and lines have no surface to draw
                        }
                        if (!readPrimitive(primitive, matrix, model, meshIndices)) {
                            return false;
                        }
                    }
                }
                auto children = node->FindMember("children");
                if (children != node->MemberEnd() && children->value.IsArray()) {
                    for (const JsonValue& child : children->value.GetArray()) {
                        if (!child.IsInt() || !readNode(child.GetInt(), matrix, depth + 1, model, meshIndices)) {
                            return fail("invalid node hierarchy");
                        }
                    }
                }
                return true;
            }

        private:
            bool fail(const std::string& message) const {
                if (_error.empty()) {
                    _error = message;
                }
                return false;
            }

            std::shared_ptr<const Bitmap> decodeImage(int index) const {
                const JsonValue* image = getElement(_doc, "images", index);
                const JsonValue* view = image ? getElement(_doc, "bufferViews", getInt(*image, "bufferView", -1)) : nullptr;
                if (!view || !_bin || !_imageDecoder) {
                    return std::shared_ptr<const Bitmap>(); // external image uris are not fetched
                }
                std::size_t offset = static_cast<std::size_t>(std::max(0, getInt(*view, "byteOffset", 0)));
                std::size_t length = static_cast<std::size_t>(std::max(0, getInt(*view, "byteLength", 0)));
                if (offset + length > _binSize) {
                    return std::shared_ptr<const Bitmap>();
                }
                return _imageDecoder(_bin + offset, length);
            }

            static cglib::mat4x4<double> nodeMatrix(const JsonValue& node) {
                cglib::mat4x4<double> m = cglib::mat4x4<double>::identity();
                auto matrix = node.FindMember("matrix");
                if (matrix != node.MemberEnd() && matrix->value.IsArray() && matrix->value.Size() == 16) {
                    for (int i = 0; i < 16; i++) {
                        m(i % 4, i / 4) = matrix->value[i].GetDouble(); // column-major
                    }
                    return m;
                }
                auto vec = [&node](const char* key, std::size_t size, double* out) {
                    auto it = node.FindMember(key);
                    if (it != node.MemberEnd() && it->value.IsArray() && it->value.Size() == size) {
                        for (std::size_t i = 0; i < size; i++) {
                            out[i] = it->value[static_cast<rapidjson::SizeType>(i)].GetDouble();
                        }
                    }
                };
                double t[3] = { 0, 0, 0 }, q[4] = { 0, 0, 0, 1 }, s[3] = { 1, 1, 1 };
                vec("translation", 3, t);
                vec("rotation", 4, q);
                vec("scale", 3, s);
                double x = q[0], y = q[1], z = q[2], w = q[3];
                double r[3][3] = {
                    { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) },
                    { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
                    { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) }
                };
                for (int i = 0; i < 3; i++) {
                    for (int j = 0; j < 3; j++) {
                        m(i, j) = r[i][j] * s[j];
                    }
                    m(i, 3) = t[i];
                }
                return m;
            }

            bool readPrimitive(const JsonValue& primitive, const cglib::mat4x4<double>& matrix, Model& model, std::map<int, std::size_t>& meshIndices) const {
                auto attributes = primitive.FindMember("attributes");
                if (attributes == primitive.MemberEnd() || !attributes->value.IsObject() || !attributes->value.HasMember("POSITION")) {
                    return fail("primitive without POSITION");
                }
                const JsonValue& attrs = attributes->value;
                std::vector<double> positions, normals, texCoords, colors, indexValues;
                int components = 0;
                if (!readAccessor(getInt(attrs, "POSITION", -1), positions, components) || components != 3) {
                    return fail("invalid POSITION");
                }
                std::size_t vertexCount = positions.size() / 3;
                if (attrs.HasMember("NORMAL") && (!readAccessor(getInt(attrs, "NORMAL", -1), normals, components) || components != 3)) {
                    return fail("invalid NORMAL");
                }
                if (attrs.HasMember("TEXCOORD_0") && (!readAccessor(getInt(attrs, "TEXCOORD_0", -1), texCoords, components) || components != 2)) {
                    return fail("invalid TEXCOORD_0");
                }
                int colorComponents = 0;
                if (attrs.HasMember("COLOR_0") && (!readAccessor(getInt(attrs, "COLOR_0", -1), colors, colorComponents) || colorComponents < 3)) {
                    return fail("invalid COLOR_0");
                }
                if ((!normals.empty() && normals.size() != positions.size()) || (!texCoords.empty() && texCoords.size() / 2 != vertexCount) || (!colors.empty() && colors.size() / colorComponents != vertexCount)) {
                    return fail("attribute counts differ");
                }
                std::vector<std::uint32_t> indices;
                if (primitive.HasMember("indices")) {
                    if (!readAccessor(getInt(primitive, "indices", -1), indexValues, components) || components != 1) {
                        return fail("invalid indices");
                    }
                    indices.reserve(indexValues.size());
                    for (double value : indexValues) {
                        if (value < 0 || value >= vertexCount) {
                            return fail("index out of range");
                        }
                        indices.push_back(static_cast<std::uint32_t>(value));
                    }
                }
                else {
                    for (std::size_t i = 0; i < vertexCount; i++) {
                        indices.push_back(static_cast<std::uint32_t>(i));
                    }
                }
                indices.resize(indices.size() - indices.size() % 3);

                // glTF is Y up, the map Z up: x stays, map y = -gltf z, map z = gltf y
                cglib::mat4x4<double> zUp = cglib::mat4x4<double>::identity();
                zUp(1, 1) = 0; zUp(1, 2) = -1; zUp(2, 1) = 1; zUp(2, 2) = 0;
                cglib::mat4x4<double> world = zUp * matrix;
                cglib::mat3x3<double> linear;
                for (int i = 0; i < 3; i++) {
                    for (int j = 0; j < 3; j++) {
                        linear(i, j) = world(i, j);
                    }
                }
                bool mirrored = cglib::determinant(linear) < 0;
                cglib::mat3x3<double> normalMatrix = cglib::transpose(cglib::inverse(linear));

                int materialIndex = getInt(primitive, "material", -1);
                if (materialIndex < -1 || materialIndex >= static_cast<int>(model.materials.size())) {
                    return fail("invalid material");
                }
                auto meshIt = meshIndices.find(materialIndex);
                if (meshIt == meshIndices.end()) {
                    ModelMesh mesh;
                    mesh.material = materialIndex;
                    model.meshes.push_back(std::move(mesh));
                    meshIt = meshIndices.emplace(materialIndex, model.meshes.size() - 1).first;
                }
                ModelMesh& mesh = model.meshes[meshIt->second];
                std::size_t base = mesh.positions.size();
                bool hasTexCoords = !texCoords.empty() || !mesh.texCoords.empty();
                bool hasColors = !colors.empty() || !mesh.colors.empty();
                if (hasTexCoords && mesh.texCoords.size() < base) {
                    mesh.texCoords.resize(base, cglib::vec2<float>(0, 0));
                }
                if (hasColors && mesh.colors.size() < base) {
                    mesh.colors.resize(base, 0xffffffffu);
                }

                // Without normals glTF asks for flat shading, so every triangle gets its own vertices.
                bool flat = normals.empty();
                auto emitVertex = [&](std::uint32_t i) {
                    cglib::vec3<double> p = cglib::transform_point(cglib::vec3<double>(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]), world);
                    mesh.positions.emplace_back(static_cast<float>(p(0)), static_cast<float>(p(1)), static_cast<float>(p(2)));
                    model.bounds.add(mesh.positions.back());
                    if (!flat) {
                        cglib::vec3<double> n(0, 0, 0);
                        for (int r = 0; r < 3; r++) {
                            n(r) = normalMatrix(r, 0) * normals[i * 3] + normalMatrix(r, 1) * normals[i * 3 + 1] + normalMatrix(r, 2) * normals[i * 3 + 2];
                        }
                        double len = cglib::length(n);
                        n = len > 0 ? n * (1.0 / len) : cglib::vec3<double>(0, 0, 1);
                        mesh.normals.emplace_back(static_cast<float>(n(0)), static_cast<float>(n(1)), static_cast<float>(n(2)));
                    }
                    if (hasTexCoords) {
                        // vt bitmaps are stored bottom-up, glTF's v runs from the top
                        mesh.texCoords.push_back(texCoords.empty() ? cglib::vec2<float>(0, 0) : cglib::vec2<float>(static_cast<float>(texCoords[i * 2]), 1.0f - static_cast<float>(texCoords[i * 2 + 1])));
                    }
                    if (hasColors) {
                        if (colors.empty()) {
                            mesh.colors.push_back(0xffffffffu);
                        }
                        else {
                            const double* c = &colors[i * colorComponents];
                            mesh.colors.push_back(packColor(linearToDisplay(static_cast<float>(c[0])), linearToDisplay(static_cast<float>(c[1])), linearToDisplay(static_cast<float>(c[2])), colorComponents == 4 ? static_cast<float>(c[3]) : 1.0f));
                        }
                    }
                };

                if (flat) {
                    for (std::size_t t = 0; t < indices.size(); t += 3) {
                        std::uint32_t a = indices[t], b = indices[t + (mirrored ? 2 : 1)], c = indices[t + (mirrored ? 1 : 2)];
                        std::size_t first = mesh.positions.size();
                        emitVertex(a);
                        emitVertex(b);
                        emitVertex(c);
                        cglib::vec3<float> n = cglib::vector_product(mesh.positions[first + 1] - mesh.positions[first], mesh.positions[first + 2] - mesh.positions[first]);
                        float len = cglib::length(n);
                        n = len > 0 ? n * (1.0f / len) : cglib::vec3<float>(0, 0, 1);
                        for (int k = 0; k < 3; k++) {
                            mesh.normals.push_back(n);
                            mesh.indices.push_back(static_cast<std::uint32_t>(first + k));
                        }
                    }
                }
                else {
                    for (std::uint32_t i = 0; i < vertexCount; i++) {
                        emitVertex(i);
                    }
                    for (std::size_t t = 0; t < indices.size(); t += 3) {
                        mesh.indices.push_back(static_cast<std::uint32_t>(base + indices[t]));
                        mesh.indices.push_back(static_cast<std::uint32_t>(base + indices[t + (mirrored ? 2 : 1)]));
                        mesh.indices.push_back(static_cast<std::uint32_t>(base + indices[t + (mirrored ? 1 : 2)]));
                    }
                }
                return true;
            }

            const JsonValue& _doc;
            const unsigned char* _bin;
            std::size_t _binSize;
            const ModelImageDecoder& _imageDecoder;
            std::string& _error;
        };
    }

    std::shared_ptr<Model> parseGLB(const unsigned char* data, std::size_t size, const ModelImageDecoder& imageDecoder, std::string& error) {
        error.clear();
        if (!data || size < 20 || readU32(data) != GLB_MAGIC || readU32(data + 4) != 2 || readU32(data + 8) > size) {
            error = "not a glTF 2.0 binary";
            return std::shared_ptr<Model>();
        }
        std::size_t length = readU32(data + 8);
        const unsigned char* json = nullptr;
        std::size_t jsonSize = 0;
        const unsigned char* bin = nullptr;
        std::size_t binSize = 0;
        for (std::size_t offset = 12; offset + 8 <= length; ) {
            std::size_t chunkSize = readU32(data + offset);
            std::uint32_t chunkType = readU32(data + offset + 4);
            if (offset + 8 + chunkSize > length) {
                error = "truncated chunk";
                return std::shared_ptr<Model>();
            }
            if (chunkType == CHUNK_JSON && !json) {
                json = data + offset + 8;
                jsonSize = chunkSize;
            }
            else if (chunkType == CHUNK_BIN && !bin) {
                bin = data + offset + 8;
                binSize = chunkSize;
            }
            offset += 8 + chunkSize;
        }

        rapidjson::Document doc;
        if (!json || doc.Parse(reinterpret_cast<const char*>(json), jsonSize).HasParseError() || !doc.IsObject()) {
            error = "invalid JSON chunk";
            return std::shared_ptr<Model>();
        }
        auto required = doc.FindMember("extensionsRequired");
        if (required != doc.MemberEnd() && required->value.IsArray() && !required->value.Empty()) {
            error = "unsupported required extension";
            if (required->value[0].IsString()) {
                error += std::string(": ") + required->value[0].GetString();
            }
            return std::shared_ptr<Model>();
        }

        auto model = std::make_shared<Model>();
        Reader reader(doc, bin, binSize, imageDecoder, error);
        if (!reader.readMaterials(model->materials)) {
            return std::shared_ptr<Model>();
        }
        std::map<int, std::size_t> meshIndices;
        const JsonValue* scene = getElement(doc, "scenes", getInt(doc, "scene", 0));
        if (!scene || !scene->HasMember("nodes") || !(*scene)["nodes"].IsArray()) {
            error = "no scene";
            return std::shared_ptr<Model>();
        }
        for (const JsonValue& node : (*scene)["nodes"].GetArray()) {
            if (!node.IsInt() || !reader.readNode(node.GetInt(), cglib::mat4x4<double>::identity(), 0, *model, meshIndices)) {
                if (error.empty()) {
                    error = "invalid scene";
                }
                return std::shared_ptr<Model>();
            }
        }
        if (model->meshes.empty()) {
            error = "no triangles";
            return std::shared_ptr<Model>();
        }
        return model;
    }
}
