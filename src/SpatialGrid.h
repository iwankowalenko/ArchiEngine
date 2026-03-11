#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ECS.h"
#include "Math2D.h"

namespace archi
{
    struct Aabb2D
    {
        Vec2 min{};
        Vec2 max{};

        bool Intersects(const Aabb2D& other) const
        {
            return !(max.x < other.min.x || min.x > other.max.x || max.y < other.min.y || min.y > other.max.y);
        }
    };

    class SpatialGrid final
    {
    public:
        explicit SpatialGrid(float cellSize = 1.0f) : m_cellSize(cellSize > 0.0f ? cellSize : 1.0f) {}

        void Clear();
        void Insert(Entity entity, const Aabb2D& bounds);
        std::vector<Entity> Query(const Aabb2D& bounds) const;

    private:
        static std::int64_t MakeCellKey(int x, int y);
        int CellCoord(float value) const;

    private:
        float m_cellSize = 1.0f;
        std::unordered_map<std::int64_t, std::vector<EntityId>> m_cells{};
    };
}
