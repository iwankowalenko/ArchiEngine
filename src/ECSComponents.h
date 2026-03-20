#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "ECS.h"
#include "Math2D.h"
namespace archi
{
    struct Transform
    {
        Vec3 position{ 0.0f, 0.0f, 0.0f };
        Vec3 rotation{ 0.0f, 0.0f, 0.0f };
        Vec3 scale{ 1.0f, 1.0f, 1.0f };
    };

    struct Tag
    {
        std::string name{};
    };

    struct MeshRenderer
    {
        std::string meshAsset{};
        std::string materialAsset{};
        std::string textureAsset{};
        std::string shaderAsset{};
        Vec4 tintColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        bool asyncLoad = true;
    };

    struct Camera
    {
        bool isPrimary = true;
        float orthographicHalfHeight = 1.25f;
        float nearPlane = -20.0f;
        float farPlane = 20.0f;
    };

    struct CameraController
    {
        float moveSpeed = 2.2f;
        float zoomSpeed = 1.2f;
    };

    struct Hierarchy
    {
        Entity parent{};
        std::vector<Entity> children{};
    };

    struct SpinAnimation
    {
        Vec3 angularVelocity{ 0.0f, 0.0f, 0.0f };
        Vec3 translationAxis{ 0.0f, 0.0f, 0.0f };
        float translationAmplitude = 0.0f;
        float translationSpeed = 0.0f;
        Vec3 anchorPosition{ 0.0f, 0.0f, 0.0f };
        double elapsed = 0.0;
    };

    inline void DetachFromParent(World& world, Entity child)
    {
        auto* hierarchy = world.GetComponent<Hierarchy>(child);
        if (!hierarchy || !hierarchy->parent)
            return;

        if (auto* parentHierarchy = world.GetComponent<Hierarchy>(hierarchy->parent))
        {
            auto& children = parentHierarchy->children;
            children.erase(
                std::remove(children.begin(), children.end(), child),
                children.end());
        }

        hierarchy->parent = {};
    }

    inline void AttachToParent(World& world, Entity child, Entity parent)
    {
        if (!world.IsAlive(child) || !world.IsAlive(parent) || child == parent)
            return;

        auto& childHierarchy = world.AddComponent<Hierarchy>(child);
        DetachFromParent(world, child);
        childHierarchy.parent = parent;

        auto& parentHierarchy = world.AddComponent<Hierarchy>(parent);
        const auto existing = std::find(parentHierarchy.children.begin(), parentHierarchy.children.end(), child);
        if (existing == parentHierarchy.children.end())
            parentHierarchy.children.push_back(child);
    }
}
