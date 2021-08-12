/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _CARTO_VT_POLYGONTESSELATOR_H_
#define _CARTO_VT_POLYGONTESSELATOR_H_

#include <vector>
#include <memory>

#include <cglib/vec.h>

namespace carto { namespace vt {
    class PolygonTesselator final {
    public:
        using Vertex = cglib::vec2<float>;
        using Vertices = std::vector<Vertex>;
        using VerticesList = std::vector<Vertices>;

        PolygonTesselator();

        void clear();
        bool tesselate(const VerticesList& polygon);

        const std::vector<int>& getElements() const { return _elements; }
        const std::vector<cglib::vec2<float>>& getVertices() const { return _vertices; }

    private:
        class PoolAllocator;

        static constexpr std::size_t MAX_TOTAL_VERTICES = 64 * 1024 * 1024;
        static constexpr std::size_t MAX_CONVEX_VERTICES = 6;

        static bool isRingSimpleAndConvex(const Vertices& ring, std::size_t& ringSize);

        std::vector<int> _elements;
        std::vector<cglib::vec2<float>> _vertices;

        std::shared_ptr<PoolAllocator> _poolAllocator;
    };
} }

#endif
