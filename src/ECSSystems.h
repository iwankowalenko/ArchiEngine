#pragma once

#include "ECS.h"
#include "SpatialGrid.h"

#include <chrono>
#include <set>

namespace archi
{
    class CameraControllerSystem final : public ISystem
    {
    public:
        const char* Name() const override { return "CameraControllerSystem"; }
        SystemPhase Phase() const override { return SystemPhase::Update; }
        void Update(World& world, const SystemContext& context) override;
    };

    class SpinSystem final : public ISystem
    {
    public:
        const char* Name() const override { return "SpinSystem"; }
        SystemPhase Phase() const override { return SystemPhase::Update; }
        void Update(World& world, const SystemContext& context) override;
    };

    class PhysicsSystem final : public ISystem
    {
    public:
        const char* Name() const override { return "PhysicsSystem"; }
        SystemPhase Phase() const override { return SystemPhase::Update; }
        void Update(World& world, const SystemContext& context) override;
        std::size_t ActiveCollisionCount() const { return m_previousCollisionPairs.size(); }

    private:
        std::set<std::pair<EntityId, EntityId>> m_previousCollisionPairs{};
    };

    class RenderSystem final : public ISystem
    {
    public:
        RenderSystem() = default;

        const char* Name() const override { return "RenderSystem"; }
        SystemPhase Phase() const override { return SystemPhase::Render; }
        void Update(World& world, const SystemContext& context) override;
        std::size_t LastVisibleObjectCount() const { return m_lastVisibleObjectCount; }
        double LastRenderDurationMs() const { return m_lastRenderDurationMs; }

    private:
        SpatialGrid m_spatialGrid{ 0.75f };
        std::size_t m_lastVisibleObjectCount = 0;
        double m_lastRenderDurationMs = 0.0;
    };

    class DebugRenderSystem final : public ISystem
    {
    public:
        const char* Name() const override { return "DebugRenderSystem"; }
        SystemPhase Phase() const override { return SystemPhase::Render; }
        void Update(World& world, const SystemContext& context) override;
    };
}
