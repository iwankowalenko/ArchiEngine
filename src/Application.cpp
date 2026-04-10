#include "Application.h"

#include "AssetPaths.h"
#include "ECSSystems.h"
#include "Logger.h"
#include "OpenGLRenderAdapter.h"
#include "SceneSerializer.h"
#include "States.h"

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
        return SceneSerializer::SaveWorld(m_world, m_scenePath);
    }

    bool Application::LoadScene()
    {
        if (m_scenePath.empty())
            m_scenePath = GetWritableAssetPath("scenes/physics_demo_scene.json");

        if (!SceneSerializer::LoadWorld(m_world, m_scenePath))
            return false;

        RefreshSceneBindings();
        const bool warmedUp = WarmUpSceneResources();
        if (!warmedUp)
            Logger::Warn("Scene loaded, but some resources failed to warm up");
        return warmedUp;
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

    void Application::RequestShaderReload()
    {
        m_resources.ForceReloadShaders();
        Logger::Info("Shader reload requested");
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

        m_world.AddSystem<CameraControllerSystem>();
        m_world.AddSystem<SpinSystem>();
        m_world.AddSystem<PhysicsSystem>();
        m_world.AddSystem<RenderSystem>();
        m_world.AddSystem<DebugRenderSystem>();

        if (!LoadOrCreateScene())
        {
            Logger::Error("Scene initialization failed");
            return false;
        }

        m_timer.Reset();
        m_sceneUpdateEnabled = true;
        m_fixedUpdateAccumulator = 0.0;

        m_states.ChangeState(*this, std::make_unique<LoadingState>());

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

            if (m_input.WasActionJustPressed("TogglePhysicsDebug"))
            {
                TogglePhysicsDebug();
                Logger::Info("Physics debug rendering: ", m_debugPhysicsEnabled ? "ON" : "OFF");
            }

            if (auto* state = m_states.Current())
                state->HandleInput(*this, dt);
            if (auto* state = m_states.Current())
                state->Update(*this, dt);

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
                dt
            };
            m_world.RunSystems(SystemPhase::Render, renderContext);
            if (auto* state = m_states.Current())
                state->Render(*this, *m_renderer);
            m_renderer->EndFrame();

            if (m_states.Depth() == 0)
            {
                Logger::Info("No states left -> quitting");
                break;
            }
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

        if (m_renderer)
        {
            m_renderer->Shutdown();
            m_renderer.reset();
        }

        Logger::Shutdown();
        m_initialized = false;
    }
}
