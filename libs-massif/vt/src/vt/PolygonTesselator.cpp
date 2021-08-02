#include "PolygonTesselator.h"

#include <cmath>
#include <algorithm>

#include <boost/math/constants/constants.hpp>

#include <tesselator.h>

namespace carto { namespace vt {
    class PolygonTesselator::PoolAllocator {
    public:
        PoolAllocator() : _first(nullptr) {
            void* buffer = std::malloc(DEFAULT_BLOCK_SIZE);
            try {
                _first = new Block(buffer, (buffer != NULL ? DEFAULT_BLOCK_SIZE : 0), nullptr);
            }
            catch (...) {
                std::free(buffer);
                throw;
            }
        }
        
        PoolAllocator(const PoolAllocator&) = delete;

        ~PoolAllocator() {
            while (_first) {
                Block* next = _first->next;
                std::free(_first->buffer);
                delete _first;
                _first = next;
            }
        }

        void reset() {
            for (Block* current = _first->next; current != nullptr;) {
                Block* next = current->next;
                std::free(current->buffer);
                delete current;
                current = next;
            }
            _first->allocated = 0;
            _first->next = nullptr;
        }

        void* allocate(std::size_t size) {
            size = (size + 7) & ~7; // keep 8-byte alignment
            if (_first->allocated + size > _first->size) {
                if (!reallocate(size)) {
                    return NULL;
                }
            }
            void* ptr = reinterpret_cast<unsigned char*>(_first->buffer) + _first->allocated;
            _first->allocated += size;
            return ptr;
        }

        PoolAllocator& operator = (const PoolAllocator&) = delete;

    private:
        static constexpr int DEFAULT_BLOCK_SIZE = 65536;

        struct Block {
            void* const buffer;
            std::size_t const size;
            std::size_t allocated;
            Block* next;

            Block(void* buffer, std::size_t size, Block* next) : buffer(buffer), size(size), allocated(0), next(next) { }
        };

        bool reallocate(std::size_t size) {
            std::size_t blockSize = std::max(size, _first->size * 2); // increase block size two-fold each time reallocation is required
            void* buffer = std::malloc(blockSize);
            if (buffer == NULL) {
                return false;
            }
            try {
                _first = new Block(buffer, blockSize, _first);
                return true;
            }
            catch (...) {
                std::free(buffer);
                return false;
            }
        }

        Block* _first;
    };

    PolygonTesselator::PolygonTesselator() {
    }

    void PolygonTesselator::clear() {
        if (_poolAllocator) {
            _poolAllocator->reset();
        }
        _elements.clear();
        _vertices.clear();
    }

    bool PolygonTesselator::tesselate(const VerticesList& polygon) {
        // Quick sanity test
        std::size_t totalVertices = 0;
        for (const Vertices& ring : polygon) {
            totalVertices += ring.size();
            if (totalVertices > MAX_TOTAL_VERTICES) {
                return false;
            }
        }
        
        // Store current vertex offset
        int offset = static_cast<int>(_vertices.size());

        // Test if we have simple and convex polygon. In that case the tesselation is trivial.
        if (polygon.size() == 1 && polygon.front().size() >= 3 && polygon.front().size() <= MAX_CONVEX_VERTICES) {
            const Vertices& ring = polygon.front();
            std::size_t ringSize = ring.size();
            if (isRingSimpleAndConvex(ring, ringSize)) {
                _vertices.insert(_vertices.end(), ring.begin(), ring.begin() + ringSize);

                _elements.reserve((ringSize - 2) * 3);
                for (int i = 2; i < static_cast<int>(ringSize); i++) {
                    _elements.push_back(offset + 0);
                    _elements.push_back(offset + i - 1);
                    _elements.push_back(offset + i);
                }
                return true;
            }
        }

        // Polygon is convex or complex. We need to use external tesselator.
        if (!_poolAllocator) {
            _poolAllocator = std::make_unique<PoolAllocator>();
        }
        _poolAllocator->reset(); // reuse last allocated block from the start

        TESSalloc ma;
        memset(&ma, 0, sizeof(ma));
        ma.memalloc = [](void* userData, unsigned int size) { return reinterpret_cast<PoolAllocator*>(userData)->allocate(size); };
        ma.memfree = [](void* userData, void* ptr) { };
        ma.userData = _poolAllocator.get();
        ma.extraVertices = 256; // realloc not provided, allow 256 extra vertices.

        TESStesselator* tessPtr = tessNewTess(&ma);
        if (!tessPtr) {
            return false;
        }
        std::shared_ptr<TESStesselator> tess(tessPtr, tessDeleteTess);
        
        for (const Vertices& ring : polygon) {
            TESSreal* coords = reinterpret_cast<TESSreal*>(_poolAllocator->allocate(ring.size() * 2 * sizeof(TESSreal)));
            if (!coords) {
                return false;
            }
            for (std::size_t i = 0; i < ring.size(); i++) {
                coords[i * 2 + 0] = static_cast<TESSreal>(ring[i](0));
                coords[i * 2 + 1] = static_cast<TESSreal>(ring[i](1));
            }
            tessAddContour(tess.get(), 2, coords, 2 * sizeof(TESSreal), static_cast<int>(ring.size()));
        }
        tessTesselate(tess.get(), TESS_WINDING_ODD, TESS_POLYGONS, 3, 2, 0);

        int vertexCount = tessGetVertexCount(tess.get());
        _vertices.reserve(_vertices.size() + static_cast<std::size_t>(vertexCount));
        const TESSreal* coords = tessGetVertices(tess.get());
        for (int i = 0; i < vertexCount; i++) {
            _vertices.emplace_back(static_cast<float>(coords[i * 2 + 0]), static_cast<float>(coords[i * 2 + 1]));
        }

        int elementCount = tessGetElementCount(tess.get());
        _elements.reserve(_elements.size() + static_cast<std::size_t>(elementCount) * 3);
        const int* elements = tessGetElements(tess.get());
        for (int i = 0; i < elementCount * 3; i += 3) {
            int i0 = elements[i + 0];
            int i1 = elements[i + 1];
            int i2 = elements[i + 2];
            if (i0 == TESS_UNDEF || i1 == TESS_UNDEF || i2 == TESS_UNDEF) {
                continue;
            }

            _elements.push_back(offset + i0);
            _elements.push_back(offset + i1);
            _elements.push_back(offset + i2);
        }
        return true;
    }

    bool PolygonTesselator::isRingSimpleAndConvex(const Vertices& ring, std::size_t& ringSize) {
        static const float EPSILON = 1.0e-5f;
        
        if (!ring.empty() && ring.front() == ring.back()) {
            --ringSize;
        }
        if (ringSize <= 3) {
            return true;
        }

        float angleSum = 0;
        float angleAbsSum = 0;
        Vertex p0 = ring[ringSize - 2];
        Vertex p1 = ring[ringSize - 1];
        if (p1 == p0) {
            return false;
        }
        float angle0 = 0;
        float angle1 = std::atan2(p1(1) - p0(1), p1(0) - p0(0));
        for (std::size_t i = 0; i < ringSize; i++) {
            p0 = p1;
            p1 = ring[i];
            if (p1 == p0) {
                return false;
            }
            angle0 = angle1;
            angle1 = std::atan2(p1(1) - p0(1), p1(0) - p0(0));
            
            float angleDiff = angle1 - angle0;
            if (angleDiff > boost::math::constants::pi<float>()) {
                angleDiff -= 2 * boost::math::constants::pi<float>();
            }
            else if (angleDiff < -boost::math::constants::pi<float>()) {
                angleDiff += 2 * boost::math::constants::pi<float>();
            }

            angleSum += angleDiff;
            angleAbsSum += std::abs(angleDiff);
            if (std::abs(angleSum) != angleAbsSum) {
                return false;
            }
        }
        return angleAbsSum <= 2 * boost::math::constants::pi<float>() + EPSILON && angleAbsSum >= 2 * boost::math::constants::pi<float>() - EPSILON;
    }
} }
