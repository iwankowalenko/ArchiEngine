#include "ECSSystems.h"

#include "ECSComponents.h"
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
        struct PreparedRenderItem
        {
            RenderMeshCommand command{};
            Aabb2D bounds{};
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
    }

    void CameraControllerSystem::Update(World& world, const SystemContext& context)
    {
        if (!context.renderer)
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

                    Vec3 direction{ 0.0f, 0.0f, 0.0f };
                    if (context.renderer->IsKeyDown(Key::W))
                        direction.y += 1.0f;
                    if (context.renderer->IsKeyDown(Key::S))
                        direction.y -= 1.0f;
                    if (context.renderer->IsKeyDown(Key::D))
                        direction.x += 1.0f;
                    if (context.renderer->IsKeyDown(Key::A))
                        direction.x -= 1.0f;

                    transform.position += direction * (controller.moveSpeed * dt);

                    if (context.renderer->IsKeyDown(Key::Q))
                        camera.orthographicHalfHeight += controller.zoomSpeed * dt;
                    if (context.renderer->IsKeyDown(Key::E))
                        camera.orthographicHalfHeight -= controller.zoomSpeed * dt;

                    if (camera.orthographicHalfHeight < 0.35f)
                        camera.orthographicHalfHeight = 0.35f;
                    if (camera.orthographicHalfHeight > 4.0f)
                        camera.orthographicHalfHeight = 4.0f;

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
        const Mat4 view = activeCamera && activeCameraTransform
            ? MakeViewMatrix(activeCameraTransform->position, activeCameraTransform->rotation)
            : Mat4::Identity();
        const Mat4 projection = activeCamera
            ? MakeOrthographicProjection(aspectRatio, activeCamera->orthographicHalfHeight, activeCamera->nearPlane, activeCamera->farPlane)
            : MakeOrthographicProjection(aspectRatio, 1.25f, -20.0f, 20.0f);
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

            m_spatialGrid.Insert(entity, item.bounds);
            preparedItems.emplace(entity.id, std::move(item));
        });

        std::vector<Entity> visibleEntities = m_spatialGrid.Query(visibleBounds);
        std::sort(
            visibleEntities.begin(),
            visibleEntities.end(),
            [](Entity lhs, Entity rhs) { return lhs.id < rhs.id; });

        for (const Entity entity : visibleEntities)
        {
            const auto it = preparedItems.find(entity.id);
            if (it == preparedItems.end())
                continue;
            if (!it->second.bounds.Intersects(visibleBounds))
                continue;

            context.renderer->DrawMesh(it->second.command);
        }
    }
}
