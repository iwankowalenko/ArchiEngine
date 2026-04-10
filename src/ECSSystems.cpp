#include "ECSSystems.h"

#include "ECSComponents.h"
#include "EventBus.h"
#include "InputManager.h"
#include "Logger.h"
#include "RenderAdapter.h"
#include "ResourceManager.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace archi
{
    namespace
    {
        constexpr float kGravityStrength = 9.81f;
        constexpr float kPlayerMoveSpeed = 4.4f;
        constexpr float kPlayerJumpSpeed = 5.4f;
        constexpr float kHorizontalDamping = 10.0f;
        constexpr float kPhysicsGridCellSize = 2.5f;
        constexpr float kGroundFrictionDampingMin = 0.6f;
        constexpr float kGroundFrictionDampingMax = 18.0f;
        constexpr float kGroundFrictionStopSpeedMin = 0.04f;
        constexpr float kGroundFrictionStopSpeedMax = 1.10f;

        struct PreparedRenderItem
        {
            RenderMeshCommand command{};
            Aabb2D bounds{};
        };

        struct PhysicsAabb
        {
            Vec3 center{ 0.0f, 0.0f, 0.0f };
            Vec3 halfExtents{ 0.5f, 0.5f, 0.5f };
            Vec3 min{ -0.5f, -0.5f, -0.5f };
            Vec3 max{ 0.5f, 0.5f, 0.5f };
        };

        struct PhysicsSphere
        {
            Vec3 center{ 0.0f, 0.0f, 0.0f };
            float radius = 0.5f;
        };

        struct ColliderEntry
        {
            Entity entity{};
            Transform* transform = nullptr;
            Collider* collider = nullptr;
            Rigidbody* body = nullptr;
            PhysicsAabb bounds{};
        };

        struct PhysicsGridCell
        {
            int x = 0;
            int y = 0;
            int z = 0;

            bool operator==(const PhysicsGridCell& rhs) const
            {
                return x == rhs.x && y == rhs.y && z == rhs.z;
            }
        };

        struct PhysicsGridCellHasher
        {
            std::size_t operator()(const PhysicsGridCell& cell) const noexcept
            {
                std::size_t seed = 0;
                auto combine = [&seed](int value) {
                    seed ^= std::hash<int>{}(value) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
                };
                combine(cell.x);
                combine(cell.y);
                combine(cell.z);
                return seed;
            }
        };

        struct CollisionResolution
        {
            Vec3 displacementFirst{ 0.0f, 0.0f, 0.0f };
            Vec3 displacementSecond{ 0.0f, 0.0f, 0.0f };
        };

        Vec4 MultiplyColor(const Vec4& lhs, const Vec4& rhs)
        {
            return {
                lhs.x * rhs.x,
                lhs.y * rhs.y,
                lhs.z * rhs.z,
                lhs.w * rhs.w
            };
        }

        Mat4 ResolveWorldMatrix(
            const World& world,
            Entity entity,
            std::map<EntityId, Mat4>& cache,
            std::set<EntityId>& recursionStack)
        {
            const auto cached = cache.find(entity.id);
            if (cached != cache.end())
                return cached->second;

            const auto* transform = world.GetComponent<Transform>(entity);
            if (!transform)
                return Mat4::Identity();

            if (recursionStack.find(entity.id) != recursionStack.end())
            {
                Logger::Warn("Hierarchy cycle detected for entity ", entity.id, ". Falling back to local transform.");
                return MakeTransformMatrix(transform->position, transform->rotation, transform->scale);
            }

            recursionStack.insert(entity.id);

            Mat4 worldMatrix = MakeTransformMatrix(transform->position, transform->rotation, transform->scale);
            if (const auto* hierarchy = world.GetComponent<Hierarchy>(entity))
            {
                if (hierarchy->parent && world.IsAlive(hierarchy->parent) && world.HasComponent<Transform>(hierarchy->parent))
                    worldMatrix = ResolveWorldMatrix(world, hierarchy->parent, cache, recursionStack) * worldMatrix;
            }

            recursionStack.erase(entity.id);
            cache[entity.id] = worldMatrix;
            return worldMatrix;
        }

        void IncludeTransformedPoint(Aabb2D& bounds, const Mat4& model, const Vec3& point, bool& initialized)
        {
            const Vec3 worldPoint = TransformPoint(model, point);
            if (!initialized)
            {
                bounds.min = { worldPoint.x, worldPoint.y };
                bounds.max = { worldPoint.x, worldPoint.y };
                initialized = true;
                return;
            }

            bounds.min.x = std::min(bounds.min.x, worldPoint.x);
            bounds.min.y = std::min(bounds.min.y, worldPoint.y);
            bounds.max.x = std::max(bounds.max.x, worldPoint.x);
            bounds.max.y = std::max(bounds.max.y, worldPoint.y);
        }

        Aabb2D ComputeMeshBounds(const MeshData& mesh, const Mat4& model)
        {
            Aabb2D bounds{};
            bool initialized = false;

            auto include = [&](float x, float y, float z) {
                IncludeTransformedPoint(bounds, model, Vec3{ x, y, z }, initialized);
            };

            const Vec3& min = mesh.boundsMin;
            const Vec3& max = mesh.boundsMax;
            include(min.x, min.y, min.z);
            include(max.x, min.y, min.z);
            include(max.x, max.y, min.z);
            include(min.x, max.y, min.z);
            include(min.x, min.y, max.z);
            include(max.x, min.y, max.z);
            include(max.x, max.y, max.z);
            include(min.x, max.y, max.z);

            if (!initialized)
                bounds = Aabb2D{ { 0.0f, 0.0f }, { 0.0f, 0.0f } };

            return bounds;
        }

        void UpdateCameraVectors(Camera& camera)
        {
            const float cosPitch = std::cos(camera.pitch);
            camera.forward = Normalize({
                std::cos(camera.yaw) * cosPitch,
                std::sin(camera.pitch),
                std::sin(camera.yaw) * cosPitch
            });

            if (Length(camera.forward) <= 0.0001f)
                camera.forward = { 0.0f, 0.0f, -1.0f };
            camera.up = { 0.0f, 1.0f, 0.0f };
        }

        void SelectActiveCamera(const World& world, const Camera*& outCamera, const Transform*& outTransform)
        {
            outCamera = nullptr;
            outTransform = nullptr;

            const Camera* firstCamera = nullptr;
            const Transform* firstTransform = nullptr;

            world.ForEach<Camera, Transform>([&](Entity, const Camera& camera, const Transform& transform) {
                if (!firstCamera)
                {
                    firstCamera = &camera;
                    firstTransform = &transform;
                }

                if (!outCamera && camera.isPrimary)
                {
                    outCamera = &camera;
                    outTransform = &transform;
                }
            });

            if (!outCamera)
            {
                outCamera = firstCamera;
                outTransform = firstTransform;
            }
        }

        Mat4 BuildCameraView(const Camera* camera, const Transform* transform)
        {
            if (!camera || !transform)
                return Mat4::Identity();

            if (camera->usePerspective)
                return MakeLookAtMatrix(transform->position, transform->position + camera->forward, camera->up);

            return MakeViewMatrix(transform->position, transform->rotation);
        }

        Mat4 BuildCameraProjection(const Camera* camera, float aspectRatio)
        {
            if (!camera)
                return MakeOrthographicProjection(aspectRatio, 1.25f, -20.0f, 20.0f);

            if (camera->usePerspective)
                return MakePerspectiveProjection(camera->fovRadians, aspectRatio, camera->nearPlane, camera->farPlane);

            return MakeOrthographicProjection(aspectRatio, camera->orthographicHalfHeight, camera->nearPlane, camera->farPlane);
        }

        Aabb2D MakeCameraBounds(const Camera* camera, const Transform* transform, float aspectRatio)
        {
            const float halfHeight = camera ? camera->orthographicHalfHeight : 1.25f;
            const float halfWidth = halfHeight * aspectRatio;
            const Vec3 center = transform ? transform->position : Vec3{};
            return {
                { center.x - halfWidth, center.y - halfHeight },
                { center.x + halfWidth, center.y + halfHeight }
            };
        }

        PhysicsAabb ComputeColliderAabb(const Transform& transform, const Collider& collider)
        {
            const Vec3 absScale{
                std::abs(transform.scale.x),
                std::abs(transform.scale.y),
                std::abs(transform.scale.z)
            };

            PhysicsAabb result{};
            result.center = transform.position + collider.offset;

            if (collider.type == ColliderType::Sphere)
            {
                const float radius = collider.radius * std::max({ absScale.x, absScale.y, absScale.z });
                result.halfExtents = { radius, radius, radius };
            }
            else
            {
                result.halfExtents = {
                    std::max(0.01f, collider.halfExtents.x * absScale.x),
                    std::max(0.01f, collider.halfExtents.y * absScale.y),
                    std::max(0.01f, collider.halfExtents.z * absScale.z)
                };
            }

            result.min = result.center - result.halfExtents;
            result.max = result.center + result.halfExtents;
            return result;
        }

        PhysicsSphere ComputeColliderSphere(const Transform& transform, const Collider& collider)
        {
            const Vec3 absScale{
                std::abs(transform.scale.x),
                std::abs(transform.scale.y),
                std::abs(transform.scale.z)
            };

            PhysicsSphere result{};
            result.center = transform.position + collider.offset;
            result.radius = std::max(
                0.05f,
                collider.radius * std::max({ absScale.x, absScale.y, absScale.z }));
            return result;
        }

        Vec3 ClosestPointOnAabb(const Vec3& point, const PhysicsAabb& box)
        {
            return {
                Clamp(point.x, box.min.x, box.max.x),
                Clamp(point.y, box.min.y, box.max.y),
                Clamp(point.z, box.min.z, box.max.z)
            };
        }

        bool IntersectAabb(const PhysicsAabb& lhs, const PhysicsAabb& rhs, Vec3& outNormal, float& outPenetration)
        {
            const float overlapX = std::min(lhs.max.x, rhs.max.x) - std::max(lhs.min.x, rhs.min.x);
            if (overlapX <= 0.0f)
                return false;

            const float overlapY = std::min(lhs.max.y, rhs.max.y) - std::max(lhs.min.y, rhs.min.y);
            if (overlapY <= 0.0f)
                return false;

            const float overlapZ = std::min(lhs.max.z, rhs.max.z) - std::max(lhs.min.z, rhs.min.z);
            if (overlapZ <= 0.0f)
                return false;

            const Vec3 delta = rhs.center - lhs.center;
            outPenetration = overlapX;
            outNormal = { delta.x < 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f };

            if (overlapY < outPenetration)
            {
                outPenetration = overlapY;
                outNormal = { 0.0f, delta.y < 0.0f ? -1.0f : 1.0f, 0.0f };
            }

            if (overlapZ < outPenetration)
            {
                outPenetration = overlapZ;
                outNormal = { 0.0f, 0.0f, delta.z < 0.0f ? -1.0f : 1.0f };
            }

            return true;
        }

        bool IntersectSphereSphere(const PhysicsSphere& lhs, const PhysicsSphere& rhs, Vec3& outNormal, float& outPenetration)
        {
            const Vec3 delta = rhs.center - lhs.center;
            const float distance = Length(delta);
            const float radiusSum = lhs.radius + rhs.radius;
            if (distance >= radiusSum)
                return false;

            if (distance <= 0.0001f)
                outNormal = { 1.0f, 0.0f, 0.0f };
            else
                outNormal = delta / distance;

            outPenetration = radiusSum - distance;
            return true;
        }

        bool IntersectSphereAabb(const PhysicsSphere& sphere, const PhysicsAabb& box, Vec3& outNormal, float& outPenetration)
        {
            const Vec3 closest = ClosestPointOnAabb(sphere.center, box);
            const Vec3 delta = closest - sphere.center;
            const float distance = Length(delta);

            if (distance > 0.0001f)
            {
                if (distance >= sphere.radius)
                    return false;

                outNormal = delta / distance;
                outPenetration = sphere.radius - distance;
                return true;
            }

            const float distanceToMinX = sphere.center.x - box.min.x;
            const float distanceToMaxX = box.max.x - sphere.center.x;
            const float distanceToMinY = sphere.center.y - box.min.y;
            const float distanceToMaxY = box.max.y - sphere.center.y;
            const float distanceToMinZ = sphere.center.z - box.min.z;
            const float distanceToMaxZ = box.max.z - sphere.center.z;

            outPenetration = sphere.radius + distanceToMinX;
            outNormal = { -1.0f, 0.0f, 0.0f };

            auto pickFace = [&](float faceDistance, const Vec3& faceNormal) {
                const float penetration = sphere.radius + faceDistance;
                if (penetration < outPenetration)
                {
                    outPenetration = penetration;
                    outNormal = faceNormal;
                }
            };

            pickFace(distanceToMaxX, { 1.0f, 0.0f, 0.0f });
            pickFace(distanceToMinY, { 0.0f, -1.0f, 0.0f });
            pickFace(distanceToMaxY, { 0.0f, 1.0f, 0.0f });
            pickFace(distanceToMinZ, { 0.0f, 0.0f, -1.0f });
            pickFace(distanceToMaxZ, { 0.0f, 0.0f, 1.0f });
            return outPenetration > 0.0f;
        }

        bool IntersectColliders(
            const Transform& transformA,
            const Collider& colliderA,
            const Transform& transformB,
            const Collider& colliderB,
            Vec3& outNormal,
            float& outPenetration)
        {
            if (colliderA.type == ColliderType::Sphere && colliderB.type == ColliderType::Sphere)
                return IntersectSphereSphere(
                    ComputeColliderSphere(transformA, colliderA),
                    ComputeColliderSphere(transformB, colliderB),
                    outNormal,
                    outPenetration);

            if (colliderA.type == ColliderType::Sphere && colliderB.type == ColliderType::Aabb)
                return IntersectSphereAabb(
                    ComputeColliderSphere(transformA, colliderA),
                    ComputeColliderAabb(transformB, colliderB),
                    outNormal,
                    outPenetration);

            if (colliderA.type == ColliderType::Aabb && colliderB.type == ColliderType::Sphere)
            {
                Vec3 normalFromSphereToAabb{};
                if (!IntersectSphereAabb(
                        ComputeColliderSphere(transformB, colliderB),
                        ComputeColliderAabb(transformA, colliderA),
                        normalFromSphereToAabb,
                        outPenetration))
                {
                    return false;
                }

                outNormal = { -normalFromSphereToAabb.x, -normalFromSphereToAabb.y, -normalFromSphereToAabb.z };
                return true;
            }

            return IntersectAabb(
                ComputeColliderAabb(transformA, colliderA),
                ComputeColliderAabb(transformB, colliderB),
                outNormal,
                outPenetration);
        }

        int PhysicsGridCoord(float value)
        {
            return static_cast<int>(std::floor(value / kPhysicsGridCellSize));
        }

        float CombineFriction(const Collider& colliderA, const Collider& colliderB)
        {
            return Clamp((colliderA.friction + colliderB.friction) * 0.5f, 0.0f, 1.0f);
        }

        float CombineBounciness(const Collider& colliderA, const Collider& colliderB)
        {
            return Clamp(std::max(colliderA.bounciness, colliderB.bounciness), 0.0f, 1.0f);
        }

        void ApplyGroundFriction(Rigidbody& body, const Collider& collider, float dt)
        {
            if (!body.isGrounded || body.isStatic)
                return;

            const float friction01 = Clamp(collider.friction, 0.0f, 1.0f);
            const float dampingPerSecond =
                kGroundFrictionDampingMin +
                (kGroundFrictionDampingMax - kGroundFrictionDampingMin) * friction01;
            const float dampingFactor = std::max(0.0f, 1.0f - dampingPerSecond * dt);
            body.velocity.x *= dampingFactor;
            body.velocity.z *= dampingFactor;

            const float horizontalSpeed = Length(Vec3{ body.velocity.x, 0.0f, body.velocity.z });
            const float stopThreshold =
                kGroundFrictionStopSpeedMin +
                (kGroundFrictionStopSpeedMax - kGroundFrictionStopSpeedMin) * friction01;
            if (horizontalSpeed < stopThreshold)
            {
                body.velocity.x = 0.0f;
                body.velocity.z = 0.0f;
            }
        }

        float InverseMass(const Rigidbody* body)
        {
            if (!body || body->isStatic || body->mass <= 0.0001f)
                return 0.0f;
            return 1.0f / body->mass;
        }

        CollisionResolution ResolveCollision(
            Transform& transformA,
            Rigidbody* bodyA,
            Transform& transformB,
            Rigidbody* bodyB,
            const Vec3& normal,
            float penetration,
            float restitution,
            float friction)
        {
            CollisionResolution resolution{};
            const float invMassA = InverseMass(bodyA);
            const float invMassB = InverseMass(bodyB);
            const float invMassSum = invMassA + invMassB;
            if (invMassSum <= 0.0f)
                return resolution;

            const Vec3 correction = normal * std::max(0.0f, penetration);
            resolution.displacementFirst = correction * (-invMassA / invMassSum);
            resolution.displacementSecond = correction * (invMassB / invMassSum);
            transformA.position += resolution.displacementFirst;
            transformB.position += resolution.displacementSecond;

            const Vec3 velocityA = bodyA ? bodyA->velocity : Vec3{};
            const Vec3 velocityB = bodyB ? bodyB->velocity : Vec3{};
            const float relativeVelocityAlongNormal = Dot(velocityB - velocityA, normal);
            float normalImpulseMagnitude = 0.0f;
            if (relativeVelocityAlongNormal < 0.0f)
            {
                normalImpulseMagnitude =
                    -(1.0f + Clamp(restitution, 0.0f, 1.0f)) * relativeVelocityAlongNormal / invMassSum;
                const Vec3 impulse = normal * normalImpulseMagnitude;

                if (bodyA && !bodyA->isStatic)
                    bodyA->velocity -= impulse * invMassA;
                if (bodyB && !bodyB->isStatic)
                    bodyB->velocity += impulse * invMassB;
            }

            const Vec3 updatedVelocityA = bodyA ? bodyA->velocity : Vec3{};
            const Vec3 updatedVelocityB = bodyB ? bodyB->velocity : Vec3{};
            const Vec3 relativeVelocity = updatedVelocityB - updatedVelocityA;
            Vec3 tangent = relativeVelocity - normal * Dot(relativeVelocity, normal);
            const float tangentLength = Length(tangent);
            if (tangentLength > 0.0001f)
            {
                tangent = tangent / tangentLength;
                float tangentImpulseMagnitude = -Dot(relativeVelocity, tangent) / invMassSum;
                tangentImpulseMagnitude *= Clamp(friction, 0.0f, 1.0f);

                if (normalImpulseMagnitude > 0.0f)
                {
                    const float maxFrictionImpulse = normalImpulseMagnitude * (0.25f + Clamp(friction, 0.0f, 1.0f));
                    tangentImpulseMagnitude = Clamp(tangentImpulseMagnitude, -maxFrictionImpulse, maxFrictionImpulse);
                }

                const Vec3 tangentImpulse = tangent * tangentImpulseMagnitude;
                if (bodyA && !bodyA->isStatic)
                    bodyA->velocity -= tangentImpulse * invMassA;
                if (bodyB && !bodyB->isStatic)
                    bodyB->velocity += tangentImpulse * invMassB;
            }

            if (bodyA)
            {
                if (normal.y < -0.5f)
                    bodyA->isGrounded = true;
            }

            if (bodyB)
            {
                if (normal.y > 0.5f)
                    bodyB->isGrounded = true;
            }

            return resolution;
        }

        std::pair<EntityId, EntityId> MakeCollisionPair(Entity lhs, Entity rhs)
        {
            return lhs.id < rhs.id ? std::make_pair(lhs.id, rhs.id) : std::make_pair(rhs.id, lhs.id);
        }

        std::set<std::pair<EntityId, EntityId>> BuildPhysicsBroadphasePairs(const std::vector<ColliderEntry>& entries)
        {
            std::unordered_map<PhysicsGridCell, std::vector<std::size_t>, PhysicsGridCellHasher> cells{};
            for (std::size_t index = 0; index < entries.size(); ++index)
            {
                const ColliderEntry& entry = entries[index];
                const PhysicsGridCell minCell{
                    PhysicsGridCoord(entry.bounds.min.x),
                    PhysicsGridCoord(entry.bounds.min.y),
                    PhysicsGridCoord(entry.bounds.min.z)
                };
                const PhysicsGridCell maxCell{
                    PhysicsGridCoord(entry.bounds.max.x),
                    PhysicsGridCoord(entry.bounds.max.y),
                    PhysicsGridCoord(entry.bounds.max.z)
                };

                for (int z = minCell.z; z <= maxCell.z; ++z)
                {
                    for (int y = minCell.y; y <= maxCell.y; ++y)
                    {
                        for (int x = minCell.x; x <= maxCell.x; ++x)
                            cells[PhysicsGridCell{ x, y, z }].push_back(index);
                    }
                }
            }

            std::set<std::pair<EntityId, EntityId>> candidatePairs{};
            for (const auto& [_, cellEntries] : cells)
            {
                if (cellEntries.size() < 2)
                    continue;

                for (std::size_t i = 0; i < cellEntries.size(); ++i)
                {
                    const ColliderEntry& entryA = entries[cellEntries[i]];
                    for (std::size_t j = i + 1; j < cellEntries.size(); ++j)
                    {
                        const ColliderEntry& entryB = entries[cellEntries[j]];
                        if (!entryA.entity || !entryB.entity || entryA.entity == entryB.entity)
                            continue;

                        const bool involvesTrigger =
                            (entryA.collider && entryA.collider->isTrigger) ||
                            (entryB.collider && entryB.collider->isTrigger);
                        const bool bothStatic =
                            entryA.body && entryA.body->isStatic &&
                            entryB.body && entryB.body->isStatic;
                        if (bothStatic && !involvesTrigger)
                            continue;

                        candidatePairs.insert(MakeCollisionPair(entryA.entity, entryB.entity));
                    }
                }
            }

            return candidatePairs;
        }
    }

    void CameraControllerSystem::Update(World& world, const SystemContext& context)
    {
        if (!context.input)
            return;

        const float dt = static_cast<float>(context.deltaTime);
        if (dt <= 0.0f)
            return;

        bool handledPrimaryCamera = false;

        auto updateCamera = [&](bool primaryOnly) {
            world.ForEach<Transform, Camera, CameraController>(
                [&](Entity, Transform& transform, Camera& camera, CameraController& controller) {
                    if (handledPrimaryCamera)
                        return;
                    if (primaryOnly && !camera.isPrimary)
                        return;

                    const bool mouseLookActive =
                        !controller.requireMouseButtonToLook ||
                        context.input->IsMouseButtonPressed(MouseButton::Right);
                    if (mouseLookActive)
                    {
                        const Vec2 mouseDelta = context.input->MouseDelta();
                        camera.yaw += mouseDelta.x * controller.mouseSensitivity;
                        camera.pitch -= mouseDelta.y * controller.mouseSensitivity;
                        camera.pitch = Clamp(camera.pitch, -1.4835298642f, 1.4835298642f);
                    }

                    UpdateCameraVectors(camera);

                    const float speedMultiplier =
                        context.input->IsKeyPressed(Key::LeftShift) ? controller.boostMultiplier : 1.0f;
                    const float moveSpeed = controller.moveSpeed * speedMultiplier * dt;
                    const float verticalSpeed = controller.verticalSpeed * dt;

                    Vec3 forwardFlat = Normalize(Vec3{ camera.forward.x, 0.0f, camera.forward.z });
                    if (Length(forwardFlat) <= 0.0001f)
                        forwardFlat = { 0.0f, 0.0f, -1.0f };

                    Vec3 right = Normalize(Cross(forwardFlat, camera.up));
                    if (Length(right) <= 0.0001f)
                        right = { 1.0f, 0.0f, 0.0f };

                    if (context.input->IsActionActive("CameraForward"))
                        transform.position += forwardFlat * moveSpeed;
                    if (context.input->IsActionActive("CameraBackward"))
                        transform.position -= forwardFlat * moveSpeed;
                    if (context.input->IsActionActive("CameraRight"))
                        transform.position += right * moveSpeed;
                    if (context.input->IsActionActive("CameraLeft"))
                        transform.position -= right * moveSpeed;
                    if (context.input->IsActionActive("CameraUp"))
                        transform.position.y += verticalSpeed;
                    if (context.input->IsActionActive("CameraDown"))
                        transform.position.y -= verticalSpeed;

                    handledPrimaryCamera = true;
                });
        };

        updateCamera(true);
        if (!handledPrimaryCamera)
            updateCamera(false);
    }

    void SpinSystem::Update(World& world, const SystemContext& context)
    {
        const float dt = static_cast<float>(context.deltaTime);
        if (dt <= 0.0f)
            return;

        world.ForEach<Transform, SpinAnimation>([dt](Entity, Transform& transform, SpinAnimation& animation) {
            animation.elapsed += dt;
            transform.rotation += animation.angularVelocity * dt;

            const Vec3 axis = Normalize(animation.translationAxis);
            if (animation.translationAmplitude > 0.0f && Length(axis) > 0.0f)
            {
                const float offset =
                    std::sin(static_cast<float>(animation.elapsed) * animation.translationSpeed) *
                    animation.translationAmplitude;
                transform.position = animation.anchorPosition + axis * offset;
            }
        });
    }

    void PhysicsSystem::Update(World& world, const SystemContext& context)
    {
        const float dt = static_cast<float>(context.deltaTime);
        if (dt <= 0.0f)
            return;

        const Camera* activeCamera = nullptr;
        const Transform* activeCameraTransform = nullptr;
        SelectActiveCamera(world, activeCamera, activeCameraTransform);

        world.ForEach<Transform, Rigidbody>([&](Entity entity, Transform& transform, Rigidbody& body) {
            if (entity == context.controlledEntity && context.input)
            {
                Vec3 movementDirection{ 0.0f, 0.0f, 0.0f };

                if (context.input->IsActionActive("PlayerForward"))
                    movementDirection += Vec3{ 0.0f, 0.0f, -1.0f };
                if (context.input->IsActionActive("PlayerBackward"))
                    movementDirection += Vec3{ 0.0f, 0.0f, 1.0f };
                if (context.input->IsActionActive("PlayerRight"))
                    movementDirection += Vec3{ 1.0f, 0.0f, 0.0f };
                if (context.input->IsActionActive("PlayerLeft"))
                    movementDirection += Vec3{ -1.0f, 0.0f, 0.0f };

                if (Length(movementDirection) > 0.0001f)
                {
                    const Vec3 horizontalVelocity = Normalize(movementDirection) * kPlayerMoveSpeed;
                    body.velocity.x = horizontalVelocity.x;
                    body.velocity.z = horizontalVelocity.z;
                }
                else
                {
                    const float dampingFactor = std::max(0.0f, 1.0f - kHorizontalDamping * dt);
                    body.velocity.x *= dampingFactor;
                    body.velocity.z *= dampingFactor;
                }

                if (context.input->WasActionJustPressed("Jump") && body.isGrounded)
                {
                    body.velocity.y = kPlayerJumpSpeed;
                    body.isGrounded = false;
                }
            }

            const bool shouldUseGravity = body.useGravity && !body.isStatic;
            body.isGrounded = false;

            if (body.isStatic)
                return;

            Vec3 totalAcceleration = body.acceleration;
            if (shouldUseGravity)
                totalAcceleration += Vec3{ 0.0f, -kGravityStrength, 0.0f };

            body.velocity += totalAcceleration * dt;
            transform.position += body.velocity * dt;
        });

        std::vector<ColliderEntry> colliderEntries{};
        std::unordered_map<EntityId, std::size_t> colliderEntryIndices{};
        world.ForEach<Transform, Collider>([&](Entity entity, Transform& transform, Collider& collider) {
            ColliderEntry entry{};
            entry.entity = entity;
            entry.transform = &transform;
            entry.collider = &collider;
            entry.body = world.GetComponent<Rigidbody>(entity);
            entry.bounds = ComputeColliderAabb(transform, collider);
            colliderEntryIndices[entity.id] = colliderEntries.size();
            colliderEntries.push_back(entry);
        });

        const std::set<std::pair<EntityId, EntityId>> broadphasePairs = BuildPhysicsBroadphasePairs(colliderEntries);

        std::set<std::pair<EntityId, EntityId>> currentCollisionPairs{};

        for (const auto& pairKey : broadphasePairs)
        {
            const auto entryItA = colliderEntryIndices.find(pairKey.first);
            const auto entryItB = colliderEntryIndices.find(pairKey.second);
            if (entryItA == colliderEntryIndices.end() || entryItB == colliderEntryIndices.end())
                continue;

            ColliderEntry& entryA = colliderEntries[entryItA->second];
            ColliderEntry& entryB = colliderEntries[entryItB->second];
            if (!entryA.transform || !entryB.transform || !entryA.collider || !entryB.collider)
                continue;

            const bool involvesTrigger = entryA.collider->isTrigger || entryB.collider->isTrigger;
            const bool bothStatic =
                entryA.body && entryA.body->isStatic &&
                entryB.body && entryB.body->isStatic;
            if (bothStatic && !involvesTrigger)
                continue;

            Vec3 collisionNormal{};
            float penetration = 0.0f;
            if (!IntersectColliders(
                    *entryA.transform,
                    *entryA.collider,
                    *entryB.transform,
                    *entryB.collider,
                    collisionNormal,
                    penetration))
            {
                continue;
            }

            currentCollisionPairs.insert(pairKey);
            const bool collisionBegan = (m_previousCollisionPairs.find(pairKey) == m_previousCollisionPairs.end());

            CollisionResolution resolution{};
            if (!involvesTrigger)
            {
                resolution = ResolveCollision(
                    *entryA.transform,
                    entryA.body,
                    *entryB.transform,
                    entryB.body,
                    collisionNormal,
                    penetration,
                    CombineBounciness(*entryA.collider, *entryB.collider),
                    CombineFriction(*entryA.collider, *entryB.collider));
            }

            if (context.events && collisionBegan)
            {
                if (involvesTrigger)
                {
                    context.events->Publish(TriggerEvent{
                        entryA.entity,
                        entryB.entity,
                        collisionNormal,
                        penetration,
                        entryA.transform->position,
                        entryB.transform->position,
                        true
                    });
                }
                else
                {
                    context.events->Publish(CollisionEvent{
                        entryA.entity,
                        entryB.entity,
                        collisionNormal,
                        penetration,
                        resolution.displacementFirst,
                        resolution.displacementSecond,
                        entryA.transform->position,
                        entryB.transform->position,
                        true
                    });
                }
            }
        }

        world.ForEach<Transform, Rigidbody, Collider>([dt](Entity, Transform& transform, Rigidbody& body, Collider& collider) {
            if (body.isStatic)
                return;

            ApplyGroundFriction(body, collider, dt);

            if (collider.type != ColliderType::Sphere)
                return;

            const Vec3 horizontalVelocity{ body.velocity.x, 0.0f, body.velocity.z };
            const float horizontalSpeed = Length(horizontalVelocity);
            if (horizontalSpeed <= 0.02f)
                return;

            const float radius = std::max(
                0.05f,
                collider.radius * std::max({ std::abs(transform.scale.x), std::abs(transform.scale.y), std::abs(transform.scale.z) }));
            const Vec3 travelDirection = horizontalVelocity / horizontalSpeed;
            const Vec3 rollingAxis = Normalize(Cross(Vec3{ 0.0f, 1.0f, 0.0f }, travelDirection));
            if (Length(rollingAxis) <= 0.0001f)
                return;

            const float rotationAmount = (horizontalSpeed * dt) / radius;
            transform.rotation += rollingAxis * rotationAmount;
        });

        m_previousCollisionPairs = std::move(currentCollisionPairs);
    }

    void RenderSystem::Update(World& world, const SystemContext& context)
    {
        if (!context.renderer || !context.resources)
            return;

        std::map<EntityId, Mat4> matrixCache{};
        std::set<EntityId> recursionStack{};

        const Camera* activeCamera = nullptr;
        const Transform* activeCameraTransform = nullptr;
        SelectActiveCamera(world, activeCamera, activeCameraTransform);

        const float aspectRatio = context.renderer->AspectRatio();
        const Mat4 view = BuildCameraView(activeCamera, activeCameraTransform);
        const Mat4 projection = BuildCameraProjection(activeCamera, aspectRatio);
        const bool usePerspective = activeCamera ? activeCamera->usePerspective : false;
        const Aabb2D visibleBounds = MakeCameraBounds(activeCamera, activeCameraTransform, aspectRatio);

        m_spatialGrid.Clear();
        std::unordered_map<EntityId, PreparedRenderItem> preparedItems{};

        world.ForEach<Transform, MeshRenderer>([&](Entity entity, Transform&, MeshRenderer& renderer) {
            const ResourcePtr<MeshAsset> meshResource =
                renderer.asyncLoad
                ? context.resources->LoadMeshAsync(renderer.meshAsset)
                : context.resources->Load<MeshAsset>(renderer.meshAsset);
            if (!meshResource)
                return;

            ResourcePtr<MeshAsset> effectiveMesh = meshResource;
            if (!meshResource->IsReady() || meshResource->value.gpuHandle == InvalidMeshHandle)
                effectiveMesh = context.resources->PlaceholderMesh();
            if (!effectiveMesh || effectiveMesh->value.gpuHandle == InvalidMeshHandle)
                return;

            ResourcePtr<MaterialAsset> materialResource{};
            if (!renderer.materialAsset.empty())
                materialResource = context.resources->Load<MaterialAsset>(renderer.materialAsset);
            else if (meshResource->IsReady() && !meshResource->value.importedMaterialAsset.empty())
                materialResource = context.resources->Load<MaterialAsset>(meshResource->value.importedMaterialAsset);
            else
                materialResource = context.resources->DefaultMaterial();

            std::string shaderAsset = renderer.shaderAsset;
            std::string textureAsset = renderer.textureAsset;
            Vec4 baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
            bool useTexture = false;

            if (materialResource && materialResource->IsReady())
            {
                shaderAsset = materialResource->value.shaderAsset;
                if (textureAsset.empty())
                    textureAsset = materialResource->value.textureAsset;
                baseColor = materialResource->value.baseColor;
                useTexture = materialResource->value.useTexture && !textureAsset.empty();
            }
            else if (!shaderAsset.empty() || !textureAsset.empty())
            {
                useTexture = !textureAsset.empty();
            }
            else if (const auto defaultMaterial = context.resources->DefaultMaterial(); defaultMaterial && defaultMaterial->IsReady())
            {
                shaderAsset = defaultMaterial->value.shaderAsset;
                baseColor = defaultMaterial->value.baseColor;
                useTexture = false;
            }

            if (shaderAsset.empty())
                shaderAsset = "shaders/textured.shader.json";

            const ResourcePtr<ShaderAsset> shaderResource = context.resources->Load<ShaderAsset>(shaderAsset);
            if (!shaderResource || !shaderResource->IsReady() || shaderResource->value.gpuHandle == InvalidShaderHandle)
                return;

            ResourcePtr<TextureAsset> textureResource{};
            if (useTexture && !textureAsset.empty())
            {
                textureResource =
                    renderer.asyncLoad
                    ? context.resources->LoadTextureAsync(textureAsset)
                    : context.resources->Load<TextureAsset>(textureAsset);

                if (!textureResource || !textureResource->IsReady() || textureResource->value.gpuHandle == InvalidTextureHandle)
                    textureResource = context.resources->PlaceholderTexture();
            }

            PreparedRenderItem item{};
            item.command.mesh = effectiveMesh->value.gpuHandle;
            item.command.shader = shaderResource->value.gpuHandle;
            item.command.texture = textureResource ? textureResource->value.gpuHandle : InvalidTextureHandle;
            item.command.model = ResolveWorldMatrix(world, entity, matrixCache, recursionStack);
            item.command.view = view;
            item.command.projection = projection;
            item.command.color = MultiplyColor(baseColor, renderer.tintColor);
            item.command.useTexture = useTexture && textureResource != nullptr;
            item.bounds = ComputeMeshBounds(effectiveMesh->value.mesh, item.command.model);

            if (!usePerspective)
                m_spatialGrid.Insert(entity, item.bounds);
            preparedItems.emplace(entity.id, std::move(item));
        });

        std::vector<Entity> visibleEntities{};
        if (usePerspective)
        {
            visibleEntities.reserve(preparedItems.size());
            for (const auto& [entityId, _] : preparedItems)
                visibleEntities.push_back(Entity{ entityId });
        }
        else
        {
            visibleEntities = m_spatialGrid.Query(visibleBounds);
        }

        std::sort(visibleEntities.begin(), visibleEntities.end(), [](Entity lhs, Entity rhs) { return lhs.id < rhs.id; });

        for (const Entity entity : visibleEntities)
        {
            const auto it = preparedItems.find(entity.id);
            if (it == preparedItems.end())
                continue;
            if (!usePerspective && !it->second.bounds.Intersects(visibleBounds))
                continue;

            context.renderer->DrawMesh(it->second.command);
        }
    }

    void DebugRenderSystem::Update(World& world, const SystemContext& context)
    {
        if (!context.renderer || !context.debugPhysics)
            return;

        const Camera* activeCamera = nullptr;
        const Transform* activeCameraTransform = nullptr;
        SelectActiveCamera(world, activeCamera, activeCameraTransform);

        const Mat4 view = BuildCameraView(activeCamera, activeCameraTransform);
        const Mat4 projection = BuildCameraProjection(activeCamera, context.renderer->AspectRatio());

        world.ForEach<Transform, Collider>([&](Entity entity, Transform& transform, Collider& collider) {
            const Vec4 color = entity == context.controlledEntity
                ? Vec4{ 0.25f, 1.0f, 0.35f, 0.95f }
                : collider.debugColor;

            if (collider.type == ColliderType::Sphere)
            {
                const PhysicsSphere sphere = ComputeColliderSphere(transform, collider);

                RenderDebugSphereCommand command{};
                command.model =
                    MakeTranslationMatrix(sphere.center) *
                    MakeScaleMatrix({ sphere.radius * 2.0f, sphere.radius * 2.0f, sphere.radius * 2.0f });
                command.view = view;
                command.projection = projection;
                command.color = color;
                context.renderer->DrawDebugSphere(command);
                return;
            }

            const PhysicsAabb bounds = ComputeColliderAabb(transform, collider);

            RenderDebugBoxCommand command{};
            command.model = MakeTranslationMatrix(bounds.center) * MakeScaleMatrix(bounds.halfExtents * 2.0f);
            command.view = view;
            command.projection = projection;
            command.color = color;

            context.renderer->DrawDebugBox(command);
        });
    }
}
