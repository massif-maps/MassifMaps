#ifndef _MASSIF_VT_MODEL_H_
#define _MASSIF_VT_MODEL_H_

#include "Bitmap.h"
#include "Color.h"

#include <cstdint>
#include <memory>
#include <vector>

#include <cglib/vec.h>
#include <cglib/bbox.h>

namespace massif::vt {
    struct ModelMaterial final {
        Color baseColor = Color(1, 1, 1, 1); // display (sRGB) values, converted from glTF's linear factor
        std::shared_ptr<const Bitmap> baseColorTexture;
        float alphaCutoff = -1.0f; // glTF MASK; negative for OPAQUE and BLEND
        bool blend = false;
        bool doubleSided = false;
    };

    // One material's triangles in model space: metres, Z up, node transforms already applied.
    struct ModelMesh final {
        int material = -1;
        std::vector<cglib::vec3<float>> positions;
        std::vector<cglib::vec3<float>> normals;
        std::vector<cglib::vec2<float>> texCoords; // empty when the material has no texture
        std::vector<std::uint32_t> colors;         // RGBA8 display values, empty when the mesh has none
        std::vector<std::uint32_t> indices;
    };

    struct Model final {
        std::vector<ModelMaterial> materials;
        std::vector<ModelMesh> meshes;
        cglib::bbox3<float> bounds = cglib::bbox3<float>::smallest();

        std::size_t getResidentSize() const {
            std::size_t size = 64;
            for (const ModelMesh& mesh : meshes) {
                size += mesh.positions.size() * 24 + mesh.texCoords.size() * 8 + mesh.colors.size() * 4 + mesh.indices.size() * 4;
            }
            for (const ModelMaterial& material : materials) {
                size += material.baseColorTexture ? material.baseColorTexture->data.size() * 4 : 0;
            }
            return size;
        }
    };
}

#endif
