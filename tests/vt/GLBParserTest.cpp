// A .glb read into a vt::Model: glTF's Y-up turned Z-up through the node transforms, flat normals
// where the file has none, linear colours turned into display values, unsupported files refused.

#include "GLBParser.h"

#include "TestCheck.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace massif::vt;

namespace {
    void appendU32(std::vector<unsigned char>& out, std::uint32_t value) {
        for (int i = 0; i < 4; i++) {
            out.push_back(static_cast<unsigned char>(value >> (8 * i)));
        }
    }

    std::vector<unsigned char> buildGLB(std::string json, std::vector<unsigned char> bin) {
        while (json.size() % 4 != 0) {
            json += ' ';
        }
        while (bin.size() % 4 != 0) {
            bin.push_back(0);
        }
        std::vector<unsigned char> out;
        appendU32(out, 0x46546C67);
        appendU32(out, 2);
        appendU32(out, static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
        appendU32(out, static_cast<std::uint32_t>(json.size()));
        appendU32(out, 0x4E4F534A);
        out.insert(out.end(), json.begin(), json.end());
        appendU32(out, static_cast<std::uint32_t>(bin.size()));
        appendU32(out, 0x004E4942);
        out.insert(out.end(), bin.begin(), bin.end());
        return out;
    }

    // One triangle: float positions, uint16 indices, normalized RGBA8 colours; `node` is the node's JSON.
    std::vector<unsigned char> triangleGLB(const std::string& node, const std::string& extra = std::string()) {
        float positions[9] = { 0, 0, 0, 1, 0, 0, 0, 2, 0 };
        std::uint16_t indices[3] = { 0, 1, 2 };
        unsigned char colors[12] = { 128, 0, 0, 255, 128, 0, 0, 255, 128, 0, 0, 255 };
        std::vector<unsigned char> bin(sizeof(positions) + sizeof(colors) + sizeof(indices));
        std::memcpy(bin.data(), positions, sizeof(positions));
        std::memcpy(bin.data() + 36, colors, sizeof(colors));
        std::memcpy(bin.data() + 48, indices, sizeof(indices));
        std::string json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[)" + node + R"(],
            "meshes":[{"primitives":[{"attributes":{"POSITION":0,"COLOR_0":1},"indices":2,"material":0}]}],
            "materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.5,0.5,0.5,1]}}],
            "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
                         {"bufferView":1,"componentType":5121,"normalized":true,"count":3,"type":"VEC4"},
                         {"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"}],
            "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12},
                           {"buffer":0,"byteOffset":48,"byteLength":6}],
            "buffers":[{"byteLength":56}])" + extra + "}";
        return buildGLB(json, bin);
    }

    bool near(float a, float b) {
        return std::abs(a - b) < 1e-4f;
    }
}

void testGLBParser() {
    std::string error;
    std::vector<unsigned char> glb = triangleGLB(R"({"mesh":0,"translation":[0,0,5]})");
    std::shared_ptr<Model> model = parseGLB(glb.data(), glb.size(), ModelImageDecoder(), error);
    TEST_CHECK(model && error.empty(), "a minimal triangle GLB parses");
    if (!model) {
        return;
    }
    TEST_CHECK(model->meshes.size() == 1 && model->meshes[0].indices.size() == 3, "one material, one triangle");
    const ModelMesh& mesh = model->meshes[0];
    // glTF (x, y, z) is map (x, -z, y): the +5 z translation lands at y = -5, glTF's up is map z
    TEST_CHECK(near(mesh.positions[2](0), 0) && near(mesh.positions[2](1), -5) && near(mesh.positions[2](2), 2), "Y-up turned Z-up through the node translation");
    TEST_CHECK(near(model->bounds.min(1), -5) && near(model->bounds.max(2), 2) && near(model->bounds.max(0), 1), "bounds in map axes");
    // No NORMAL: flat normal of the face, which faced glTF +z, i.e. map -y
    TEST_CHECK(mesh.normals.size() == 3 && near(mesh.normals[0](1), -1), "a flat normal where the file has none");
    // linear 128/255 is display 0.7367, byte 188
    TEST_CHECK(mesh.colors.size() == 3 && (mesh.colors[0] & 0xff) == 188 && (mesh.colors[0] >> 24) == 255, "vertex colours turned from linear to display values");
    TEST_CHECK(model->materials.size() == 1 && std::abs(model->materials[0].baseColor.rgba()[0] - 0.7354f) < 1e-3f, "base colour factor turned from linear to display values");

    glb = triangleGLB(R"({"mesh":0,"scale":[-1,1,1]})");
    model = parseGLB(glb.data(), glb.size(), ModelImageDecoder(), error);
    TEST_CHECK(model && near(model->meshes[0].normals[0](1), -1), "a mirroring node flips the winding, so the face still points out");

    glb = triangleGLB(R"({"children":[1],"rotation":[0,0.7071068,0,0.7071068]},{"mesh":0,"translation":[1,0,0]})");
    model = parseGLB(glb.data(), glb.size(), ModelImageDecoder(), error);
    // 90 degrees about glTF y turns the child's +x offset into glTF -z, map +y
    TEST_CHECK(model && near(model->meshes[0].positions[0](1), 1) && near(model->meshes[0].positions[0](0), 0), "a child node is placed through its parent's rotation");

    glb = triangleGLB(R"({"mesh":0})", R"(,"extensionsRequired":["KHR_draco_mesh_compression"])");
    model = parseGLB(glb.data(), glb.size(), ModelImageDecoder(), error);
    TEST_CHECK(!model && error.find("KHR_draco_mesh_compression") != std::string::npos, "a required extension is refused by name");

    glb = triangleGLB(R"({"mesh":0})");
    model = parseGLB(glb.data(), glb.size() - 8, ModelImageDecoder(), error);
    TEST_CHECK(!model && !error.empty(), "a truncated file is refused");
}
