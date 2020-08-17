#include "TileSurfaceBuilder.h"
#include "TextFormatter.h"

#include <utility>
#include <algorithm>
#include <iterator>

#include <boost/math/constants/constants.hpp>

#include <tesselator.h>

namespace carto { namespace vt {
    TileSurfaceBuilder::TileSurfaceBuilder(std::shared_ptr<const TileTransformer> transformer) :
        _transformer(std::move(transformer))
    {
    }

    void TileSurfaceBuilder::setOrigin(const cglib::vec3<double>& origin) {
        if (origin != _origin) {
            _tileSurfaceCache.clear();
            _origin = origin;
        }
    }

    void TileSurfaceBuilder::setVisibleTiles(const std::set<TileId>& tileIds) {
        std::map<TileId, TileNeighbours> tileSplitNeighbours;
        for (const TileId& tileId : tileIds) {
            tileSplitNeighbours[tileId] = TileNeighbours();
        }

        for (const TileId& tileId : tileIds) {
            for (int i = 0; i < 4; i++) {
                for (TileId parentId = tileId; parentId.zoom > 0; parentId = parentId.getParent()) {
                    int dx = (i == 0 || i == 1 ? (i == 0 ? 1 : (1 << parentId.zoom) - 1) : 0);
                    int dy = (i == 2 || i == 3 ? (i == 2 ? 1 : -1) : 0);
                    TileId neighbourId(parentId.zoom, (parentId.x + dx) % (1 << parentId.zoom), parentId.y + dy);
                    if (neighbourId.getParent() == parentId.getParent()) {
                        break;
                    }
                    auto it = tileSplitNeighbours.find(neighbourId.getParent());
                    if (it != tileSplitNeighbours.end()) {
                        it->second[i].push_back(tileId);
                    }
                }
            }
        }

        std::map<TileId, std::vector<std::shared_ptr<TileSurface>>> tileSurfaceCache;
        for (auto cacheIt = _tileSurfaceCache.begin(); cacheIt != _tileSurfaceCache.end(); cacheIt++) {
            const TileId& tileId = cacheIt->first;
            if (_tileSplitNeighbours[tileId] == tileSplitNeighbours[tileId]) {
                tileSurfaceCache[tileId] = cacheIt->second;
            }
        }

        _tileSplitNeighbours = std::move(tileSplitNeighbours);
        
        _tileSurfaceCache = std::move(tileSurfaceCache);
    }

    std::vector<std::shared_ptr<TileSurface>> TileSurfaceBuilder::buildTileSurface(const TileId& tileId) const {
        auto cacheIt = _tileSurfaceCache.find(tileId);
        if (cacheIt != _tileSurfaceCache.end()) {
            return cacheIt->second;
        }

        // Tesselate tile along edges to avoid T-vertices between neighbouring tiles.
        TileNeighbours tileNeighbours;
        auto tileNeighboursIt = _tileSplitNeighbours.find(tileId);
        if (tileNeighboursIt != _tileSplitNeighbours.end()) {
            tileNeighbours = tileNeighboursIt->second;
        }
        std::array<std::vector<TileId>, 4> vertexIds = { {
            tesselateTile(tileId, tileNeighbours[0], false),
            tesselateTile(TileId(tileId.zoom, tileId.x + 1, tileId.y), tileNeighbours[1], false),
            tesselateTile(tileId, tileNeighbours[2], true),
            tesselateTile(TileId(tileId.zoom, tileId.x, tileId.y + 1), tileNeighbours[3], true),
        } };

        // Build tile geometry
        VertexArray<cglib::vec2<float>> coords2D;
        VertexArray<cglib::vec3<float>> coords3D;
        VertexArray<cglib::vec2<float>> texCoords;
        VertexArray<cglib::vec3<float>> normals;
        VertexArray<cglib::vec3<float>> binormals;
        VertexArray<std::size_t> indices;
        coords2D.reserve(RESERVED_VERTICES);
        coords3D.reserve(RESERVED_VERTICES);
        texCoords.reserve(RESERVED_VERTICES);
        normals.reserve(RESERVED_VERTICES);
        binormals.reserve(RESERVED_VERTICES);
        indices.reserve(RESERVED_VERTICES);

        if (tileId.y < 0) {
            buildPoleGeometry(-1, vertexIds[3], coords2D, coords3D, texCoords, normals, binormals, indices);
        }
        else if (tileId.y >= (1 << tileId.zoom)) {
            buildPoleGeometry(1, vertexIds[2], coords2D, coords3D, texCoords, normals, binormals, indices);
        }
        else {
            buildTileGeometry(tileId, vertexIds, coords2D, coords3D, texCoords, normals, binormals, indices);
        }

        // Drop normals, if not needed
        if (std::all_of(normals.begin(), normals.end(), [](const cglib::vec3<float>& normal) { return normal(2) == 1; })) {
            normals.clear();
        }

        // Drop binormals, if not needed
        if (std::all_of(binormals.begin(), binormals.end(), [](const cglib::vec3<float>& binormal) { return binormal(1) == 1; })) {
            binormals.clear();
        }

        // Pack geometry and cache the result
        std::vector<std::shared_ptr<TileSurface>> tileSurfaces;
        packGeometry(coords3D, texCoords, normals, binormals, indices, tileSurfaces);
        _tileSurfaceCache[tileId] = tileSurfaces;
        return tileSurfaces;
    }

    void TileSurfaceBuilder::buildTileGeometry(const TileId& tileId, const std::array<std::vector<TileId>, 4>& vertexIds, VertexArray<cglib::vec2<float>>& coords2D, VertexArray<cglib::vec3<float>>& coords3D, VertexArray<cglib::vec2<float>>& texCoords, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec3<float>>& binormals, VertexArray<std::size_t>& indices) const {
        auto appendTilePoint = [&, this](const TileId& vertexId) -> std::size_t {
            int deltaZoom = vertexId.zoom - tileId.zoom;
            float s = 1.0f / (1 << deltaZoom);
            float u = (vertexId.x - (tileId.x << deltaZoom)) * s;
            float v = (vertexId.y - (tileId.y << deltaZoom)) * s;

            cglib::mat4x4<double> matrix = _transformer->calculateTileMatrix(vertexId, 1.0f);
            std::shared_ptr<const TileTransformer::VertexTransformer> transformer = _transformer->createTileVertexTransformer(vertexId);
            cglib::vec3<double> pos = cglib::transform_point(cglib::vec3<double>::convert(transformer->calculatePoint(cglib::vec2<float>(0, 0))), matrix);

            coords2D.append(cglib::vec2<float>(u, v));
            texCoords.append(cglib::vec2<float>(u, v));
            coords3D.append(cglib::vec3<float>::convert(pos - _origin));
            normals.append(transformer->calculateNormal(cglib::vec2<float>(0, 0)));
            binormals.append(cglib::unit(transformer->calculateVector(cglib::vec2<float>(0, 0), cglib::vec2<float>(0, 1))));
            return coords2D.size() - 1;
        };

        auto tesselateTriangle = [&, this](std::size_t i0, std::size_t i1, std::size_t i2, const cglib::mat4x4<double>& matrix, const std::shared_ptr<const TileTransformer::VertexTransformer>& transformer) {
            std::array<std::size_t, 3> srcIndices = { { i0, i1, i2 } };
            transformer->tesselateTriangles(srcIndices.data(), 3, coords2D, texCoords, indices);

            for (std::size_t i = coords3D.size(); i < coords2D.size(); i++) {
                cglib::vec3<double> pos = cglib::transform_point(cglib::vec3<double>::convert(transformer->calculatePoint(coords2D[i])), matrix);
                
                coords3D.append(cglib::vec3<float>::convert(pos - _origin));
                normals.append(transformer->calculateNormal(coords2D[i]));
                binormals.append(cglib::unit(transformer->calculateVector(coords2D[i], cglib::vec2<float>(0, 1))));
            }
        };

        cglib::mat4x4<double> matrix = _transformer->calculateTileMatrix(tileId, 1.0f);
        std::shared_ptr<const TileTransformer::VertexTransformer> transformer = _transformer->createTileVertexTransformer(tileId);

        // Tesselate tiles by carefully calculating edge vertices and tesselating them
        std::size_t i0 = appendTilePoint(vertexIds[0][0]);
        std::size_t i2 = appendTilePoint(vertexIds[2][1]);
        for (std::size_t i = 0; ++i < vertexIds[0].size(); ) {
            std::size_t i1 = appendTilePoint(vertexIds[0][i]);
            tesselateTriangle(i0, i1, i2, matrix, transformer);
            i0 = i1;
        }
        for (std::size_t i = 1; ++i < vertexIds[2].size(); ) {
            std::size_t i1 = appendTilePoint(vertexIds[2][i]);
            tesselateTriangle(i0, i1, i2, matrix, transformer);
            i2 = i1;
        }

        i0 = appendTilePoint(vertexIds[1][vertexIds[1].size() - 1]);
        i2 = appendTilePoint(vertexIds[3][vertexIds[3].size() - 2]);
        for (std::size_t i = vertexIds[1].size() - 1; i-- > 0; ) {
            std::size_t i1 = appendTilePoint(vertexIds[1][i]);
            tesselateTriangle(i0, i1, i2, matrix, transformer);
            i0 = i1;
        }
        for (std::size_t i = vertexIds[3].size() - 2; i-- > 0; ) {
            std::size_t i1 = appendTilePoint(vertexIds[3][i]);
            tesselateTriangle(i0, i1, i2, matrix, transformer);
            i2 = i1;
        }
    }

    void TileSurfaceBuilder::buildPoleGeometry(int poleZ, const std::vector<TileId>& vertexIds, VertexArray<cglib::vec2<float>>& coords2D, VertexArray<cglib::vec3<float>>& coords3D, VertexArray<cglib::vec2<float>>& texCoords, VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec3<float>>& binormals, VertexArray<std::size_t>& indices) const {
        auto calculatePolePoint = [&, this](const TileId& vertexId) -> cglib::vec2<float> {
            float s = 1.0f / (1 << vertexId.zoom);
            float u = vertexId.x * s;
            float v = 0.5f + 0.5f * static_cast<float>(poleZ);
            return cglib::vec2<float>(u, v);
        };

        auto tesselateSegment = [&, this](const cglib::vec2<float>& p0, const cglib::vec2<float>& p1, const cglib::mat4x4<double>& matrix, const std::shared_ptr<const TileTransformer::VertexTransformer>& transformer) {
            cglib::vec2<float> points[2] = { p0, p1 };
            transformer->tesselateLineString(points, 2, coords2D);

            std::size_t i0 = 0;
            for (std::size_t i = coords3D.size(); i < coords2D.size(); i++) {
                std::size_t i1 = coords3D.size();
                cglib::vec3<double> pos = cglib::transform_point(cglib::vec3<double>::convert(transformer->calculatePoint(coords2D[i])), matrix);

                coords3D.append(cglib::vec3<float>::convert(pos - _origin));
                normals.append(transformer->calculateNormal(coords2D[i]));
                binormals.append(cglib::unit(transformer->calculateVector(coords2D[i], cglib::vec2<float>(0, 1))));
                texCoords.append(coords2D[i]);
                if (i0 != 0) {
                    indices.append(0, i0, i1);
                }
                i0 = i1;
            }
        };

        std::shared_ptr<const TileTransformer::VertexTransformer> transformer = _transformer->createTileVertexTransformer(TileId(0, 0, 0));
        cglib::mat4x4<double> matrix = _transformer->calculateTileMatrix(TileId(0, 0, 0), 1.0f);

        // Tesselate poles. We reuse single transformer and rely on buffering.
        cglib::vec2<float> polePos(0.0f, std::numeric_limits<float>::infinity() * poleZ);
        cglib::vec3<double> poleOrigin = cglib::transform_point(cglib::vec3<double>::convert(transformer->calculatePoint(polePos)), matrix);
        
        coords2D.append(cglib::vec2<float>(0, 0));
        coords3D.append(cglib::vec3<float>::convert(poleOrigin - _origin));
        normals.append(transformer->calculateNormal(polePos));
        binormals.append(cglib::unit(transformer->calculateVector(polePos, cglib::vec2<float>(0, 1))));
        texCoords.append(cglib::vec2<float>(0, 0));
        
        cglib::vec2<float> p0 = calculatePolePoint(vertexIds[0]);
        for (std::size_t i = 1; i < vertexIds.size(); i++) {
            cglib::vec2<float> p1 = calculatePolePoint(vertexIds[i]);
            if (poleZ < 0) {
                tesselateSegment(p0, p1, matrix, transformer);
            }
            else {
                tesselateSegment(p1, p0, matrix, transformer);
            }
            p0 = p1;
        }
    }

    void TileSurfaceBuilder::packGeometry(const VertexArray<cglib::vec3<float>>& coords, const VertexArray<cglib::vec2<float>>& texCoords, const VertexArray<cglib::vec3<float>>& normals, VertexArray<cglib::vec3<float>>& binormals, const VertexArray<std::size_t>& indices, std::vector<std::shared_ptr<TileSurface>>& tileSurfaces) const {
        if (coords.size() > 65535) {
            for (std::size_t offset = 0; offset < indices.size(); ) {
                std::size_t count = std::min(std::size_t(65535), indices.size() - offset);

                std::vector<std::size_t> indexTable(indices.size(), 65536);
                VertexArray<cglib::vec3<float>> remappedCoords;
                VertexArray<cglib::vec2<float>> remappedTexCoords;
                VertexArray<cglib::vec3<float>> remappedNormals;
                VertexArray<cglib::vec3<float>> remappedBinormals;
                VertexArray<std::size_t> remappedIndices;
                for (std::size_t i = 0; i < count; i++) {
                    std::size_t index = indices[offset + i];
                    std::size_t remappedIndex = indexTable[index];
                    if (remappedIndex == 65536) {
                        remappedIndex = remappedCoords.size();
                        indexTable[index] = remappedIndex;

                        remappedCoords.append(coords[index]);
                        remappedTexCoords.append(texCoords[index]);
                        if (!normals.empty()) {
                            remappedNormals.append(normals[index]);
                        }
                        if (!binormals.empty()) {
                            remappedBinormals.append(binormals[index]);
                        }
                    }

                    remappedIndices.append(remappedIndex);
                }

                packGeometry(remappedCoords, remappedTexCoords, remappedNormals, remappedBinormals, remappedIndices, tileSurfaces);

                offset += count;
            }
            return;
        }

        // Build geometry layout info
        TileSurface::VertexGeometryLayoutParameters vertexGeomLayoutParams;
        vertexGeomLayoutParams.coordOffset = vertexGeomLayoutParams.vertexSize;
        vertexGeomLayoutParams.vertexSize += 3 * sizeof(float);
        vertexGeomLayoutParams.vertexSize = (vertexGeomLayoutParams.vertexSize + 3) & ~3;

        vertexGeomLayoutParams.texCoordOffset = vertexGeomLayoutParams.vertexSize;
        vertexGeomLayoutParams.vertexSize += 2 * sizeof(std::int16_t);

        if (!normals.empty()) {
            vertexGeomLayoutParams.normalOffset = vertexGeomLayoutParams.vertexSize;
            vertexGeomLayoutParams.vertexSize += 3 * sizeof(std::int16_t);
            vertexGeomLayoutParams.vertexSize = (vertexGeomLayoutParams.vertexSize + 3) & ~3;
        }

        if (!binormals.empty()) {
            vertexGeomLayoutParams.binormalOffset = vertexGeomLayoutParams.vertexSize;
            vertexGeomLayoutParams.vertexSize += 3 * sizeof(std::int16_t);
            vertexGeomLayoutParams.vertexSize = (vertexGeomLayoutParams.vertexSize + 3) & ~3;
        }

        // Interleave, compress actual geometry data
        VertexArray<std::uint8_t> compressedVertexGeometry;
        compressedVertexGeometry.fill(0, coords.size() * vertexGeomLayoutParams.vertexSize);
        for (std::size_t i = 0; i < coords.size(); i++) {
            std::uint8_t* baseCompressedPtr = &compressedVertexGeometry[i * vertexGeomLayoutParams.vertexSize];

            const cglib::vec3<float>& coord = coords[i];
            float* compressedCoordPtr = reinterpret_cast<float*>(baseCompressedPtr + vertexGeomLayoutParams.coordOffset);
            for (int j = 0; j < 3; j++) {
                compressedCoordPtr[j] = coord(j);
            }

            const cglib::vec2<float>& texCoord = texCoords[i];
            std::int16_t* compressedTexCoordPtr = reinterpret_cast<std::int16_t*>(baseCompressedPtr + vertexGeomLayoutParams.texCoordOffset);
            compressedTexCoordPtr[0] = static_cast<std::int16_t>(texCoord(0) * 32767.0f);
            compressedTexCoordPtr[1] = static_cast<std::int16_t>(texCoord(1) * 32767.0f);

            if (!normals.empty()) {
                const cglib::vec3<float>& normal = normals[i];
                std::int16_t* compressedNormalPtr = reinterpret_cast<std::int16_t*>(baseCompressedPtr + vertexGeomLayoutParams.normalOffset);
                for (int j = 0; j < 3; j++) {
                    compressedNormalPtr[j] = static_cast<std::int16_t>(normal(j) * 32767.0f);
                }
            }

            if (!binormals.empty()) {
                const cglib::vec3<float>& binormal = binormals[i];
                std::int16_t* compressedBinormalPtr = reinterpret_cast<std::int16_t*>(baseCompressedPtr + vertexGeomLayoutParams.binormalOffset);
                for (int j = 0; j < 3; j++) {
                    compressedBinormalPtr[j] = static_cast<std::int16_t>(binormal(j) * 32767.0f);
                }
            }
        }

        // Compress indices
        VertexArray<std::uint16_t> compressedIndices;
        compressedIndices.reserve(indices.size());
        for (std::size_t i = 0; i < indices.size(); i++) {
            compressedIndices.append(static_cast<std::uint16_t>(indices[i]));
        }

        auto tileSurface = std::make_shared<TileSurface>(vertexGeomLayoutParams, std::move(compressedVertexGeometry), std::move(compressedIndices));
        tileSurfaces.push_back(std::move(tileSurface));
    }

    std::vector<TileId> TileSurfaceBuilder::tesselateTile(const TileId& baseTileId, const std::vector<TileId>& tileIds, bool xCoord) {
        auto calculatePosition = [&baseTileId, xCoord](const TileId& tileId) -> float {
            int deltaZoom = tileId.zoom - baseTileId.zoom;
            int deltaCoord = xCoord ? tileId.x - (baseTileId.x << deltaZoom) : tileId.y - (baseTileId.y << deltaZoom);
            return 1.0f / (1 << deltaZoom) * deltaCoord;
        };

        std::vector<TileId> vertexIds;
        vertexIds.push_back(TileId(baseTileId.zoom, baseTileId.x, baseTileId.y));
        vertexIds.push_back(TileId(baseTileId.zoom, baseTileId.x + (xCoord ? 1 : 0), baseTileId.y + (xCoord ? 0 : 1)));
        for (TileId tileId : tileIds) {
            int deltaZoom = tileId.zoom - baseTileId.zoom;
            TileId tileId0(tileId.zoom, xCoord ? tileId.x : baseTileId.x << deltaZoom, xCoord ? baseTileId.y << deltaZoom : tileId.y);
            TileId tileId1(tileId.zoom, tileId0.x + (xCoord ? 1 : 0), tileId0.y + (xCoord ? 0 : 1));
            
            float pos0 = calculatePosition(tileId0);
            float pos1 = calculatePosition(tileId1);
            for (std::size_t i = 0; i + 1 < vertexIds.size(); i++) {
                float currPos = calculatePosition(vertexIds[i + 0]);
                float nextPos = calculatePosition(vertexIds[i + 1]);
                if (pos0 >= currPos && pos1 <= nextPos) {
                    if (pos1 == nextPos) {
                        if (tileId1.zoom > vertexIds[i + 1].zoom) {
                            vertexIds[i + 1] = tileId1;
                        }
                    }
                    else {
                        vertexIds.insert(vertexIds.begin() + i + 1, tileId1); // insert before next/after current
                    }
                    if (pos0 == currPos) {
                        if (tileId0.zoom > vertexIds[i + 0].zoom) {
                            vertexIds[i + 0] = tileId0;
                        }
                    }
                    else {
                        vertexIds.insert(vertexIds.begin() + i + 1, tileId0); // insert after current
                    }
                    break;
                }
            }
        }
        return vertexIds;
    }
} }
