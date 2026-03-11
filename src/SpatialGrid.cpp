#include "SpatialGrid.h"

#include <cmath>

namespace archi
{
    void SpatialGrid::Clear()
    {
        m_cells.clear();
    }

    void SpatialGrid::Insert(Entity entity, const Aabb2D& bounds)
    {
        const int minX = CellCoord(bounds.min.x);
        const int maxX = CellCoord(bounds.max.x);
        const int minY = CellCoord(bounds.min.y);
        const int maxY = CellCoord(bounds.max.y);

        for (int y = minY; y <= maxY; ++y)
        {
            for (int x = minX; x <= maxX; ++x)
                m_cells[MakeCellKey(x, y)].push_back(entity.id);
        }
    }

    std::vector<Entity> SpatialGrid::Query(const Aabb2D& bounds) const
    {
        std::unordered_set<EntityId> seen{};
        std::vector<Entity> result{};

        const int minX = CellCoord(bounds.min.x);
        const int maxX = CellCoord(bounds.max.x);
        const int minY = CellCoord(bounds.min.y);
        const int maxY = CellCoord(bounds.max.y);

        for (int y = minY; y <= maxY; ++y)
        {
            for (int x = minX; x <= maxX; ++x)
            {
                const auto it = m_cells.find(MakeCellKey(x, y));
                if (it == m_cells.end())
                    continue;

                for (const EntityId id : it->second)
                {
                    if (seen.insert(id).second)
                        result.push_back(Entity{ id });
                }
            }
        }

        return result;
    }

    std::int64_t SpatialGrid::MakeCellKey(int x, int y)
    {
        return (static_cast<std::int64_t>(x) << 32) ^ (static_cast<std::uint32_t>(y));
    }

    int SpatialGrid::CellCoord(float value) const
    {
        return static_cast<int>(std::floor(value / m_cellSize));
    }
}
