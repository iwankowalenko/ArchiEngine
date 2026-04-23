#include "Application.h"

#include "AssetPaths.h"
#include "EditorLayer.h"
#include "ECSSystems.h"
#include "Logger.h"
#include "OpenGLRenderAdapter.h"
#include "SceneSerializer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace archi
{
    namespace
    {
        constexpr double kFixedPhysicsStep = 1.0 / 60.0;
        constexpr int kMaxFixedStepsPerFrame = 8;

        double ClampDouble(double value, double lo, double hi)
        {
            return (value < lo) ? lo : (value > hi ? hi : value);
        }

        Vec3 ForwardFromAngles(float yaw, float pitch)
        {
            const float cosPitch = std::cos(pitch);
            return Normalize({
                std::cos(yaw) * cosPitch,
                std::sin(pitch),
                std::sin(yaw) * cosPitch
            });
        }

        std::string DescribeEntity(const World& world, Entity entity)
        {
            if (const auto* tag = world.GetComponent<Tag>(entity))
                return tag->name;
            return "Entity " + std::to_string(entity.id);
        }

        std::string FormatVec3(const Vec3& value)
        {
            char buffer[96]{};
            std::snprintf(
                buffer,
                sizeof(buffer),
                "(%.2f, %.2f, %.2f)",
                static_cast<double>(value.x),
                static_cast<double>(value.y),
                static_cast<double>(value.z));
            return std::string(buffer);
        }
    }

    Application::Application() = default;

    Application::~Application()
    {
        Shutdown();
    }

    Transform* Application::ControlledTransform()
    {
        return m_world.GetComponent<Transform>(m_controlledEntity);
    }

    const Transform* Application::ControlledTransform() const
    {
        return m_world.GetComponent<Transform>(m_controlledEntity);
    }

    void Application::ResetControlledEntityTransform()
    {
        if (auto* transform = ControlledTransform())
        {
            transform->position = { 0.0f, 2.0f, 0.0f };
            transform->rotation = { 0.0f, 0.0f, 0.0f };
            transform->scale = { 0.8f, 0.8f, 0.8f };
        }

        if (auto* body = m_world.GetComponent<Rigidbody>(m_controlledEntity))
        {
            body->velocity = { 0.0f, 0.0f, 0.0f };
            body->acceleration = { 0.0f, 0.0f, 0.0f };
            body->isGrounded = false;
        }
    }

    Entity Application::FindEntityByTag(const char* name) const
    {
        if (!name)
            return {};

        Entity found{};
        m_world.ForEach<Tag>([&](Entity entity, const Tag& tag) {
            if (!found && tag.name == name)
                found = entity;
        });
        return found;
    }

    bool Application::SaveScene()
    {
        if (m_scenePath.empty())
            m_scenePath = GetWritableAssetPath("scenes/physics_demo_scene.json");
        return SaveSceneToPath(m_scenePath);
    }

    bool Application::LoadScene()
    {
        if (m_scenePath.empty())
            m_scenePath = GetWritableAssetPath("scenes/physics_demo_scene.json");
        return LoadSceneFromPath(m_scenePath);
    }

    bool Application::SaveSceneToPath(const std::filesystem::path& path)
    {
        return SceneSerializer::SaveWorld(m_world, path);
    }

    bool Application::LoadSceneFromPath(const std::filesystem::path& path)
    {
        if (!SceneSerializer::LoadWorld(m_world, path))
            return false;

        RefreshSceneBindings();
        const bool warmedUp = WarmUpSceneResources();
        if (!warmedUp)
            Logger::Warn("Scene loaded, but some resources failed to warm up");
        return warmedUp;
    }

    bool Application::DeleteSceneEntity(Entity entity)
    {
        if (!m_world.IsAlive(entity))
            return false;

        const auto destroyRecursive = [&](auto&& self, Entity current) -> void {
            if (!m_world.IsAlive(current))
                return;

            std::vector<Entity> children{};
            if (const auto* hierarchy = m_world.GetComponent<Hierarchy>(current))
                children = hierarchy->children;

            for (Entity child : children)
                self(self, child);

            DetachFromParent(m_world, current);
            m_world.DestroyEntity(current);
        };

        destroyRecursive(destroyRecursive, entity);
        RefreshSceneBindings();
        return true;
    }

    bool Application::ResetSceneToDefault()
    {
        BuildDemoScene();
        RefreshSceneBindings();
        m_fixedUpdateAccumulator = 0.0;

        const bool warmedUp = WarmUpSceneResources();
        if (!warmedUp)
            Logger::Warn("Default physics scene reset completed with resource warm-up issues");

        if (!SaveScene())
            Logger::Warn("Failed to persist reset scene to disk");

        Logger::Info("Scene reset to default state");
        return warmedUp;
    }

    bool Application::OpenAdditionalWindow()
    {
        if (!m_renderer)
            return false;

        const RenderConfig cfg{ 640, 480, "ArchiEngine", true };
        return m_renderer->OpenAdditionalWindow(cfg, 0.08f, 0.03f, 0.16f);
    }

    void Application::RequestShaderReload()
    {
        m_resources.ForceReloadShaders();
        Logger::Info("Shader reload requested");
    }

    bool Application::EnterEditorPlayMode()
    {
        if (m_editorPlayMode)
            return true;

        m_editorSnapshotPath = GetWritableAssetPath("scenes/.editor_play_snapshot.json");
        if (!SaveSceneToPath(m_editorSnapshotPath))
        {
            Logger::Error("Failed to create editor play snapshot");
            return false;
        }

        m_editorPlayMode = true;
        Logger::Info("Editor switched to Play mode");
        return true;
    }

    bool Application::ExitEditorPlayMode()
    {
        if (!m_editorPlayMode)
            return true;

        if (!m_editorSnapshotPath.empty() && std::filesystem::exists(m_editorSnapshotPath))
        {
            if (!LoadSceneFromPath(m_editorSnapshotPath))
            {
                Logger::Error("Failed to restore editor play snapshot");
                return false;
            }
        }

        m_fixedUpdateAccumulator = 0.0;
        m_editorPlayMode = false;
        Logger::Info("Editor switched to Edit mode");
        return true;
    }

    std::size_t Application::ActiveCollisionCount() const
    {
        return m_physicsSystem ? m_physicsSystem->ActiveCollisionCount() : 0u;
    }

    std::size_t Application::LastRenderedObjectCount() const
    {
        return m_renderSystem ? m_renderSystem->LastVisibleObjectCount() : 0u;
    }

    double Application::LastRenderDurationMs() const
    {
        return m_renderSystem ? m_renderSystem->LastRenderDurationMs() : 0.0;
    }

    void Application::RefreshSceneBindings()
    {
        m_controlledEntity = FindEntityByTag("Player");

        if (!m_controlledEntity)
        {
            m_world.ForEach<Transform>([&](Entity entity, Transform&) {
                if (!m_controlledEntity)
                    m_controlledEntity = entity;
            });
        }
    }

    void Application::BuildDemoScene()
    {
        m_world.ClearEntities();
        m_controlledEntity = {};

        const std::string checkerMaterial = "materials/checker.material.json";
        const std::string groundMaterial = "materials/ground_brick.material.json";
        const std::string blueMetalMaterial = "materials/blue_metal.material.json";
        const std::string rustyMetalMaterial = "materials/rusty_metal.material.json";
        const std::string woodMaterial = "materials/sapeli_wood.material.json";
        const std::string ballMaterial = "materials/ball_metal.material.json";
        const std::string playerMaterial = "materials/build_material.json";

        const Entity cameraEntity = m_world.CreateEntity();
        m_world.AddComponent<Tag>(cameraEntity).name = "MainCamera";
        auto& cameraTransform = m_world.AddComponent<Transform>(cameraEntity);
        cameraTransform.position = { 0.0f, 4.0f, 9.0f };
        cameraTransform.rotation = { 0.0f, 0.0f, 0.0f };
        cameraTransform.scale = { 1.0f, 1.0f, 1.0f };

        auto& camera = m_world.AddComponent<Camera>(cameraEntity);
        camera.isPrimary = true;
        camera.usePerspective = true;
        camera.nearPlane = 0.1f;
        camera.farPlane = 100.0f;
        camera.fovRadians = Radians(60.0f);
        camera.yaw = -1.57079632679f;
        camera.pitch = -0.34906585039f;
        camera.forward = ForwardFromAngles(camera.yaw, camera.pitch);
        camera.up = { 0.0f, 1.0f, 0.0f };

        auto& cameraController = m_world.AddComponent<CameraController>(cameraEntity);
        cameraController.moveSpeed = 6.5f;
        cameraController.verticalSpeed = 5.0f;
        cameraController.boostMultiplier = 1.8f;
        cameraController.mouseSensitivity = 0.0035f;
        cameraController.requireMouseButtonToLook = true;

        auto createRenderable =
            [this](const char* name,
                   const std::string& meshAsset,
                   const Vec3& position,
                   const Vec3& rotation,
                   const Vec3& scale,
                   const Vec4& tintColor,
                   const std::string& materialAsset,
                   bool asyncLoad) {
                const Entity entity = m_world.CreateEntity();
                m_world.AddComponent<Tag>(entity).name = name;

                auto& transform = m_world.AddComponent<Transform>(entity);
                transform.position = position;
                transform.rotation = rotation;
                transform.scale = scale;

                auto& mesh = m_world.AddComponent<MeshRenderer>(entity);
                mesh.meshAsset = meshAsset;
                mesh.materialAsset = materialAsset;
                mesh.tintColor = tintColor;
                mesh.asyncLoad = asyncLoad;
                return entity;
            };

        const Entity ground = createRenderable(
            "Ground",
            "models/cube.obj",
            { 0.0f, -1.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f },
            { 20.0f, 1.0f, 20.0f },
            { 1.0f, 1.0f, 1.0f, 1.0f },
            groundMaterial,
            false);
        {
            auto& collider = m_world.AddComponent<Collider>(ground);
            collider.type = ColliderType::Aabb;
            collider.halfExtents = { 0.5f, 0.5f, 0.5f };
            collider.friction = 0.95f;
            collider.bounciness = 0.02f;
            collider.debugColor = { 1.0f, 0.25f, 0.25f, 0.7f };

            auto& rigidbody = m_world.AddComponent<Rigidbody>(ground);
            rigidbody.mass = 0.0f;
            rigidbody.useGravity = false;
            rigidbody.isStatic = true;
        }

        m_controlledEntity = createRenderable(
            "Player",
            "models/cube.obj",
            { 0.0f, 2.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f },
            { 0.8f, 0.8f, 0.8f },
            { 1.0f, 1.0f, 1.0f, 1.0f },
            playerMaterial,
            true);
        {
            auto& collider = m_world.AddComponent<Collider>(m_controlledEntity);
            collider.type = ColliderType::Aabb;
            collider.halfExtents = { 0.5f, 0.5f, 0.5f };
            collider.friction = 0.55f;
            collider.bounciness = 0.0f;
            collider.debugColor = { 0.25f, 1.0f, 0.35f, 0.95f };

            auto& rigidbody = m_world.AddComponent<Rigidbody>(m_controlledEntity);
            rigidbody.mass = 1.0f;
            rigidbody.useGravity = true;
        }

        const Entity crateA = createRenderable(
            "CrateA",
            "models/cube.obj",
            { 1.8f, 4.5f, 0.5f },
            { 0.0f, 0.0f, 0.0f },
            { 0.75f, 0.75f, 0.75f },
            { 1.0f, 1.0f, 1.0f, 1.0f },
            blueMetalMaterial,
            true);
        {
            auto& collider = m_world.AddComponent<Collider>(crateA);
            collider.halfExtents = { 0.5f, 0.5f, 0.5f };
            collider.friction = 0.65f;
            collider.bounciness = 0.05f;

            auto& rigidbody = m_world.AddComponent<Rigidbody>(crateA);
            rigidbody.mass = 1.0f;
            rigidbody.useGravity = true;
        }

        const Entity crateB = createRenderable(
            "CrateB",
            "models/pyramid.obj",
            { -2.0f, 6.0f, -1.2f },
            { 0.0f, 0.0f, 0.0f },
            { 0.9f, 0.9f, 0.9f },
            { 1.0f, 1.0f, 1.0f, 1.0f },
            woodMaterial,
            true);
        {
            auto& collider = m_world.AddComponent<Collider>(crateB);
            collider.halfExtents = { 0.55f, 0.65f, 0.55f };
            collider.friction = 0.50f;
            collider.bounciness = 0.12f;

            auto& rigidbody = m_world.AddComponent<Rigidbody>(crateB);
            rigidbody.mass = 1.0f;
            rigidbody.useGravity = true;
        }

        const Entity lowFrictionBox = createRenderable(
            "LowFrictionBox",
            "models/cube.obj",
            { -4.0f, 1.2f, 5.5f },
            { 0.0f, 0.0f, 0.0f },
            { 0.85f, 0.85f, 0.85f },
            { 1.0f, 1.0f, 1.0f, 1.0f },
            blueMetalMaterial,
            true);
        {
            auto& collider = m_world.AddComponent<Collider>(lowFrictionBox);
            collider.type = ColliderType::Aabb;
            collider.halfExtents = { 0.5f, 0.5f, 0.5f };
            collider.friction = 0.05f;
            collider.bounciness = 0.02f;
            collider.debugColor = { 0.25f, 0.85f, 1.0f, 0.95f };

            auto& rigidbody = m_world.AddComponent<Rigidbody>(lowFrictionBox);
            rigidbody.mass = 1.0f;
            rigidbody.useGravity = true;
        }

        const Entity mediumFrictionBox = createRenderable(
            "MediumFrictionBox",
            "models/cube.obj",
            { 0.0f, 1.2f, 5.5f },
            { 0.0f, 0.0f, 0.0f },
            { 0.85f, 0.85f, 0.85f },
            { 1.0f, 1.0f, 1.0f, 1.0f },
            woodMaterial,
            true);
        {
            auto& collider = m_world.AddComponent<Collider>(mediumFrictionBox);
            collider.type = ColliderType::Aabb;
            collider.halfExtents = { 0.5f, 0.5f, 0.5f };
            collider.friction = 0.45f;
            collider.bounciness = 0.02f;
            collider.debugColor = { 1.0f, 0.82f, 0.35f, 0.95f };

            auto& rigidbody = m_world.AddComponent<Rigidbody>(mediumFrictionBox);
            rigidbody.mass = 1.0f;
            rigidbody.useGravity = true;
        }

        const Entity highFrictionBox = createRenderable(
            "HighFrictionBox",
            "models/cube.obj",
            { 4.0f, 1.2f, 5.5f },
            { 0.0f, 0.0f, 0.0f },
            { 0.85f, 0.85f, 0.85f },
            { 1.0f, 1.0f, 1.0f, 1.0f },
            rustyMetalMaterial,
            true);
        {
            auto& collider = m_world.AddComponent<Collider>(highFrictionBox);
            collider.type = ColliderType::Aabb;
            collider.halfExtents = { 0.5f, 0.5f, 0.5f };
            collider.friction = 0.90f;
            collider.bounciness = 0.02f;
            collider.debugColor = { 1.0f, 0.35f, 0.35f, 0.95f };

            auto& rigidbody = m_world.AddComponent<Rigidbody>(highFrictionBox);
            rigidbody.mass = 1.0f;
            rigidbody.useGravity = true;
        }

        const Entity statue = createRenderable(
            "Statue",
            "models/cheburashka.obj",
            { 3.0f, 3.0f, -2.0f },
            { 0.0f, 0.45f, 0.0f },
            { 0.45f, 0.45f, 0.45f },
            { 0.85f, 0.85f, 1.0f, 1.0f },
            checkerMaterial,
            true);
        {
            auto& collider = m_world.AddComponent<Collider>(statue);
            collider.halfExtents = { 0.75f, 1.0f, 0.65f };
            collider.friction = 0.72f;
            collider.bounciness = 0.03f;

            auto& rigidbody = m_world.AddComponent<Rigidbody>(statue);
            rigidbody.mass = 1.6f;
            rigidbody.useGravity = true;
        }

        const Entity wall = createRenderable(
            "Wall",
            "models/cube.obj",
            { -3.5f, 1.25f, -3.0f },
            { 0.0f, 0.0f, 0.0f },
            { 1.0f, 3.5f, 0.8f },
            { 1.0f, 1.0f, 1.0f, 1.0f },
            rustyMetalMaterial,
            false);
        {
            auto& collider = m_world.AddComponent<Collider>(wall);
            collider.halfExtents = { 0.5f, 0.5f, 0.5f };
            collider.friction = 0.80f;
            collider.bounciness = 0.05f;
            collider.debugColor = { 0.9f, 0.4f, 0.2f, 0.7f };

            auto& rigidbody = m_world.AddComponent<Rigidbody>(wall);
            rigidbody.mass = 0.0f;
            rigidbody.useGravity = false;
            rigidbody.isStatic = true;
        }

        const Entity triggerZone = createRenderable(
            "TriggerZone",
            "models/cube.obj",
            { 1.7f, 0.15f, 1.4f },
            { 0.0f, 0.0f, 0.0f },
            { 1.4f, 0.3f, 1.4f },
            { 1.0f, 1.0f, 1.0f, 1.0f },
            blueMetalMaterial,
            false);
        {
            auto& collider = m_world.AddComponent<Collider>(triggerZone);
            collider.type = ColliderType::Aabb;
            collider.halfExtents = { 0.5f, 0.5f, 0.5f };
            collider.isTrigger = true;
            collider.friction = 0.0f;
            collider.bounciness = 0.0f;
            collider.debugColor = { 1.0f, 0.1f, 1.0f, 0.9f };

            auto& rigidbody = m_world.AddComponent<Rigidbody>(triggerZone);
            rigidbody.mass = 0.0f;
            rigidbody.useGravity = false;
            rigidbody.isStatic = true;
        }

        const Entity step = createRenderable(
            "Step",
            "models/negr.obj",
            { 2.5f, 0.06f, 2.5f },
            { 0.0f, 0.0f, 0.0f },
            { 0.7f, 0.7f, 0.7f },
            { 1.0f, 1.0f, 1.0f, 1.0f },
            checkerMaterial,
            true);
        {
            auto& collider = m_world.AddComponent<Collider>(step);
            collider.type = ColliderType::Sphere;
            collider.radius = 0.8f;
            collider.friction = 0.25f;
            collider.bounciness = 0.18f;
            collider.debugColor = { 0.5f, 0.8f, 1.0f, 0.8f };

            auto& rigidbody = m_world.AddComponent<Rigidbody>(step);
            rigidbody.mass = 0.0f;
            rigidbody.useGravity = false;
            rigidbody.isStatic = true;
        }

        const Entity rollingBall = createRenderable(
            "RollingBall",
            "builtin:mesh:sphere",
            { 1.7f, 0.0f, 1.4f },
            { 0.0f, 0.0f, 0.0f },
            { 0.95f, 0.95f, 0.95f },
            { 1.0f, 1.0f, 1.0f, 1.0f },
            ballMaterial,
            true);
        {
            auto& collider = m_world.AddComponent<Collider>(rollingBall);
            collider.type = ColliderType::Sphere;
            collider.radius = 0.5f;
            collider.friction = 0.18f;
            collider.bounciness = 0.38f;
            collider.debugColor = { 0.30f, 0.82f, 1.0f, 0.95f };

            auto& rigidbody = m_world.AddComponent<Rigidbody>(rollingBall);
            rigidbody.mass = 0.75f;
            rigidbody.useGravity = true;
        }
    }

    bool Application::WarmUpSceneResources()
    {
        bool ok = true;

        m_world.ForEach<MeshRenderer>([&](Entity entity, MeshRenderer& renderer) {
            if (renderer.meshAsset.empty())
            {
                Logger::Error("Entity ", entity.id, " has incomplete MeshRenderer resource references");
                ok = false;
                return;
            }

            const ResourcePtr<MeshAsset> meshResource =
                renderer.asyncLoad
                ? m_resources.LoadMeshAsync(renderer.meshAsset)
                : m_resources.Load<MeshAsset>(renderer.meshAsset);
            if (!meshResource)
                ok = false;

            if (!renderer.materialAsset.empty())
            {
                const ResourcePtr<MaterialAsset> materialResource = m_resources.Load<MaterialAsset>(renderer.materialAsset);
                if (!materialResource)
                {
                    ok = false;
                    return;
                }

                if (!materialResource->value.shaderAsset.empty() &&
                    !m_resources.Load<ShaderAsset>(materialResource->value.shaderAsset))
                {
                    ok = false;
                }

                if (materialResource->value.useTexture && !materialResource->value.textureAsset.empty())
                {
                    const ResourcePtr<TextureAsset> textureResource =
                        renderer.asyncLoad
                        ? m_resources.LoadTextureAsync(materialResource->value.textureAsset)
                        : m_resources.Load<TextureAsset>(materialResource->value.textureAsset);
                    if (!textureResource)
                        ok = false;
                }
            }
            else
            {
                if (!renderer.shaderAsset.empty() && !m_resources.Load<ShaderAsset>(renderer.shaderAsset))
                    ok = false;
                if (!renderer.textureAsset.empty())
                {
                    const ResourcePtr<TextureAsset> textureResource =
                        renderer.asyncLoad
                        ? m_resources.LoadTextureAsync(renderer.textureAsset)
                        : m_resources.Load<TextureAsset>(renderer.textureAsset);
                    if (!textureResource)
                        ok = false;
                }
            }
        });

        return ok;
    }

    bool Application::LoadOrCreateScene()
    {
        m_scenePath = GetWritableAssetPath("scenes/physics_demo_scene.json");

        if (std::filesystem::exists(m_scenePath) && LoadScene())
        {
            Logger::Info("Loaded physics scene from JSON");
            return true;
        }

        BuildDemoScene();
        RefreshSceneBindings();

        if (!SaveScene())
            Logger::Warn("Failed to write physics demo scene JSON to disk");

        return WarmUpSceneResources();
    }

    bool Application::Init()
    {
        if (m_initialized)
            return true;

        Logger::Init("archi.log");
        Logger::SetMinLevel(LogLevel::Trace);

        Logger::Info("Application init...");

        m_renderer = std::unique_ptr<IRenderAdapter>(new OpenGLRenderAdapter());
        RenderConfig cfg{};
        cfg.width = 1280;
        cfg.height = 720;
        cfg.title = "ArchiEngine";
        cfg.vsync = true;

        if (!m_renderer->Init(cfg))
        {
            Logger::Error("Renderer init failed");
            return false;
        }

        m_input.BindAction("CameraForward", Key::W);
        m_input.BindAction("CameraBackward", Key::S);
        m_input.BindAction("CameraLeft", Key::A);
        m_input.BindAction("CameraRight", Key::D);
        m_input.BindAction("PlayerForward", Key::Up);
        m_input.BindAction("PlayerBackward", Key::Down);
        m_input.BindAction("PlayerLeft", Key::Left);
        m_input.BindAction("PlayerRight", Key::Right);
        m_input.BindAction("Jump", Key::Space);
        m_input.BindAction("CameraDown", Key::Q);
        m_input.BindAction("CameraUp", Key::E);
        m_input.BindAction("TogglePhysicsDebug", Key::F3);
        m_input.BindAction("MenuAccept", Key::Enter);
        m_input.BindAction("Pause", Key::P);
        m_input.BindAction("OpenWindow", Key::N);
        m_input.BindAction("ReloadAssets", Key::R);
        m_input.BindAction("SaveScene", Key::K);
        m_input.BindAction("LoadScene", Key::L);
        m_input.BindAction("ResetScene", Key::M);

        m_resources.SetRenderAdapter(m_renderer.get());
        m_editor = std::make_unique<EditorLayer>();
        if (!m_editor->Init(*m_renderer))
        {
            Logger::Error("Editor layer init failed");
            return false;
        }

        m_events.Subscribe<CollisionEvent>([this](const CollisionEvent& event) {
            const std::string firstName = DescribeEntity(m_world, event.first);
            const std::string secondName = DescribeEntity(m_world, event.second);
            Logger::Info(
                "COLLISION ENTER | ",
                firstName,
                " <-> ",
                secondName,
                " | normal=",
                FormatVec3(event.normal),
                " | penetration=",
                event.penetration);
            Logger::Info(
                "RESOLVE SHIFT  | ",
                firstName,
                " delta=",
                FormatVec3(event.displacementFirst),
                " -> pos=",
                FormatVec3(event.positionFirst),
                " || ",
                secondName,
                " delta=",
                FormatVec3(event.displacementSecond),
                " -> pos=",
                FormatVec3(event.positionSecond));
        });

        m_events.Subscribe<TriggerEvent>([this](const TriggerEvent& event) {
            const std::string firstName = DescribeEntity(m_world, event.first);
            const std::string secondName = DescribeEntity(m_world, event.second);
            Logger::Info(
                "TRIGGER ENTER  | ",
                firstName,
                " <-> ",
                secondName,
                " | normal=",
                FormatVec3(event.normal),
                " | penetration=",
                event.penetration,
                " | firstPos=",
                FormatVec3(event.positionFirst),
                " | secondPos=",
                FormatVec3(event.positionSecond));
        });

        m_cameraControllerSystem = &m_world.AddSystem<CameraControllerSystem>();
        m_spinSystem = &m_world.AddSystem<SpinSystem>();
        m_physicsSystem = &m_world.AddSystem<PhysicsSystem>();
        m_renderSystem = &m_world.AddSystem<RenderSystem>();
        m_debugRenderSystem = &m_world.AddSystem<DebugRenderSystem>();

        if (!LoadOrCreateScene())
        {
            Logger::Error("Scene initialization failed");
            return false;
        }

        m_timer.Reset();
        m_sceneUpdateEnabled = true;
        m_fixedUpdateAccumulator = 0.0;
        m_editorPlayMode = false;

        m_initialized = true;
        Logger::Info("Application init done");
        return true;
    }

    int Application::Run()
    {
        if (!m_initialized && !Init())
            return -1;

        Logger::Info("Application run...");

        double logAccumulator = 0.0;

        while (!m_quitRequested && !m_renderer->ShouldClose())
        {
            double dt = m_timer.TickSeconds();
            dt = ClampDouble(dt, 0.0, 0.25);

            logAccumulator += dt;
            if (logAccumulator >= 0.5)
            {
                Logger::Info("deltaTime (s) last frame: ", dt, " | approx FPS: ", (dt > 0.0 ? (1.0 / dt) : 0.0));
                logAccumulator = 0.0;
            }
            else
            {
                Logger::Trace("dt=", dt);
            }

            m_renderer->PollEvents();
            m_input.Update(*m_renderer);
            if (m_editor)
            {
                m_editor->BeginFrame();
                m_editor->Build(*this, dt);
            }

            m_resources.UpdateHotReload();

            if (m_sceneUpdateEnabled)
            {
                m_fixedUpdateAccumulator = std::min(
                    m_fixedUpdateAccumulator + dt,
                    kFixedPhysicsStep * static_cast<double>(kMaxFixedStepsPerFrame));

                int fixedSteps = 0;
                while (m_fixedUpdateAccumulator >= kFixedPhysicsStep && fixedSteps < kMaxFixedStepsPerFrame)
                {
                    const SystemContext updateContext{
                        m_renderer.get(),
                        &m_resources,
                        &m_input,
                        &m_events,
                        m_controlledEntity,
                        m_debugPhysicsEnabled,
                        m_editor ? m_editor->IsPlayMode() : true,
                        m_editor ? m_editor->AllowGameplayInput() : true,
                        m_editor ? m_editor->AllowCameraInput() : true,
                        m_editor ? m_editor->SceneAspectRatio() : 0.0f,
                        kFixedPhysicsStep
                    };
                    m_world.RunSystems(SystemPhase::Update, updateContext);
                    m_fixedUpdateAccumulator -= kFixedPhysicsStep;
                    ++fixedSteps;
                }
            }
            else
            {
                m_fixedUpdateAccumulator = 0.0;
            }

            m_renderer->BeginFrame();
            const SystemContext renderContext{
                m_renderer.get(),
                &m_resources,
                &m_input,
                &m_events,
                m_controlledEntity,
                m_debugPhysicsEnabled,
                m_editor ? m_editor->IsPlayMode() : true,
                false,
                false,
                m_editor ? m_editor->SceneAspectRatio() : 0.0f,
                dt
            };

            bool renderedToViewport = false;
            if (m_editor && m_editor->HasViewport())
            {
                renderedToViewport = m_renderer->BeginViewportRender(
                    m_editor->ViewportWidth(),
                    m_editor->ViewportHeight());
            }

            m_world.RunSystems(SystemPhase::Render, renderContext);

            if (renderedToViewport)
                m_renderer->EndViewportRender();

            if (m_editor)
                m_editor->Render();
            m_renderer->EndFrame();
        }

        Logger::Info("Application loop ended");
        Shutdown();
        return 0;
    }

    void Application::Shutdown()
    {
        if (!m_initialized)
            return;

        Logger::Info("Application shutdown...");

        m_states.ChangeState(*this, nullptr);
        m_world.ClearEntities();
        m_controlledEntity = {};
        m_fixedUpdateAccumulator = 0.0;
        m_editorPlayMode = false;
        m_cameraControllerSystem = nullptr;
        m_spinSystem = nullptr;
        m_physicsSystem = nullptr;
        m_renderSystem = nullptr;
        m_debugRenderSystem = nullptr;

        if (m_editor)
        {
            m_editor->Shutdown();
            m_editor.reset();
        }

        if (m_renderer)
        {
            m_renderer->Shutdown();
            m_renderer.reset();
        }

        Logger::Shutdown();
        m_initialized = false;
    }
}
