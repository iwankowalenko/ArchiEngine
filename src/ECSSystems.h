#pragma once

#include "ECS.h"
#include "SpatialGrid.h"

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

    private:
        SpatialGrid m_spatialGrid{ 0.75f };
    };

    class DebugRenderSystem final : public ISystem
    {
    public:
        const char* Name() const override { return "DebugRenderSystem"; }
        SystemPhase Phase() const override { return SystemPhase::Render; }
        void Update(World& world, const SystemContext& context) override;
    };
}
