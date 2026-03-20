#include "Application.h"

#include "AssetPaths.h"
#include "ECSSystems.h"
#include "Logger.h"
#include "OpenGLRenderAdapter.h"
#include "SceneSerializer.h"
#include "States.h"

#include <memory>

namespace archi
{
    namespace
    {
        double ClampDouble(double value, double lo, double hi)
        {
            return (value < lo) ? lo : (value > hi ? hi : value);
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
            transform->position = { 0.0f, 0.0f, 0.0f };
            transform->rotation = { 0.0f, 0.0f, 0.0f };
            transform->scale = { 0.65f, 0.65f, 0.65f };
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
            m_scenePath = GetWritableAssetPath("scenes/demo_scene.json");
        return SceneSerializer::SaveWorld(m_world, m_scenePath);
    }

    bool Application::LoadScene()
    {
        if (m_scenePath.empty())
            m_scenePath = GetWritableAssetPath("scenes/demo_scene.json");

        if (!SceneSerializer::LoadWorld(m_world, m_scenePath))
            return false;

        RefreshSceneBindings();
        return true;
    }

    void Application::RequestShaderReload()
    {
        m_resources.ForceReloadAll();
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

        const Entity cameraEntity = m_world.CreateEntity();
        m_world.AddComponent<Tag>(cameraEntity).name = "MainCamera";
        auto& cameraTransform = m_world.AddComponent<Transform>(cameraEntity);
        cameraTransform.position = { 0.0f, 0.0f, 0.0f };
        cameraTransform.rotation = { 0.0f, 0.0f, 0.0f };
        cameraTransform.scale = { 1.0f, 1.0f, 1.0f };

        auto& camera = m_world.AddComponent<Camera>(cameraEntity);
        camera.isPrimary = true;
        camera.orthographicHalfHeight = 2.35f;
        camera.nearPlane = -20.0f;
        camera.farPlane = 20.0f;

        auto& cameraController = m_world.AddComponent<CameraController>(cameraEntity);
        cameraController.moveSpeed = 2.4f;
        cameraController.zoomSpeed = 1.4f;

        auto createRenderable =
            [this](const char* name,
                   const std::string& meshAsset,
                   const Vec3& position,
                   const Vec3& rotation,
                   const Vec3& scale,
                   const Vec4& tintColor,
                   const std::string& materialAsset = std::string{},
                   bool asyncLoad = true) {
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

        m_controlledEntity = createRenderable(
            "Player",
            "models/pyramid.obj",
            { 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.0f },
            { 0.65f, 0.65f, 0.65f },
            { 0.15f, 0.85f, 0.35f, 1.0f },
            checkerMaterial);

        const Entity floorQuad = createRenderable(
            "Floor",
            "models/plane.obj",
            { -1.30f, -0.75f, 0.0f },
            { 0.0f, 0.0f, 0.15f },
            { 1.60f, 0.10f, 1.60f },
            { 0.95f, 0.85f, 0.85f, 1.0f },
            checkerMaterial);

        const Entity beam = createRenderable(
            "Beam",
            "models/cheburashka.obj",
            { 1.35f, 0.55f, 0.0f },
            { 0.5f, 0.5f, 0.5f },
            { 0.5f, 0.5f, 0.5f },
            { 0.50f, 0.67f, 0.97f, 1.0f },
            checkerMaterial);

        const Entity mover = createRenderable(
            "Mover",
            "models/negr.obj",
            { 1.05f, -0.15f, 0.0f },
            { 0.0f, 0.0f, 0.0f },
            { 0.35f, 0.35f, 0.35f },
            { 0.95f, 0.85f, 0.23f, 1.0f },
            std::string{});

        auto& moverAnimation = m_world.AddComponent<SpinAnimation>(mover);
        moverAnimation.angularVelocity = { 0.0f, 0.0f, 1.1f };
        moverAnimation.translationAxis = { 1.0f, 0.0f, 0.0f };
        moverAnimation.translationAmplitude = 0.45f;
        moverAnimation.translationSpeed = 1.5f;
        moverAnimation.anchorPosition = { 1.05f, -0.15f, 0.0f };

        const Entity pivot = createRenderable(
            "Pivot",
            "models/pyramid.obj",
            { -1.10f, 0.55f, 0.0f },
            { 0.0f, 0.0f, 0.0f },
            { 0.45f, 0.45f, 0.45f },
            { 0.79f, 0.35f, 0.89f, 1.0f },
            checkerMaterial);

        auto& pivotAnimation = m_world.AddComponent<SpinAnimation>(pivot);
        pivotAnimation.angularVelocity = { 0.0f, 0.0f, 0.85f };
        pivotAnimation.anchorPosition = { -1.10f, 0.55f, 0.0f };

        const Entity childCube = createRenderable(
            "SatelliteCube",
            "models/negr.obj",
            { 0.70f, 0.0f, 0.0f },
            { 0.6f, 0.4f, 0.2f },
            { 0.24f, 0.24f, 0.24f },
            { 0.95f, 0.95f, 0.95f, 1.0f },
            std::string{});

        auto& cubeAnimation = m_world.AddComponent<SpinAnimation>(childCube);
        cubeAnimation.angularVelocity = { 1.3f, 1.7f, 0.5f };
        cubeAnimation.anchorPosition = { 0.70f, 0.0f, 0.0f };

        AttachToParent(m_world, childCube, pivot);

        createRenderable(
            "FarMarker",
            "models/plane.obj",
            { 4.50f, 2.40f, 0.0f },
            { 0.0f, 0.0f, 0.0f },
            { 0.50f, 0.50f, 0.50f },
            { 0.90f, 0.40f, 0.25f, 1.0f },
            checkerMaterial);

        (void)floorQuad;
        (void)beam;
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
        m_scenePath = GetWritableAssetPath("scenes/demo_scene.json");

        if (std::filesystem::exists(m_scenePath) && LoadScene())
        {
            Logger::Info("Loaded scene from JSON");
            return WarmUpSceneResources();
        }

        BuildDemoScene();
        RefreshSceneBindings();

        if (!SaveScene())
            Logger::Warn("Failed to write demo scene JSON to disk");

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

        m_resources.SetRenderAdapter(m_renderer.get());

        m_world.AddSystem<CameraControllerSystem>();
        m_world.AddSystem<SpinSystem>();
        m_world.AddSystem<RenderSystem>();

        if (!LoadOrCreateScene())
        {
            Logger::Error("Scene initialization failed");
            return false;
        }

        m_timer.Reset();
        m_sceneUpdateEnabled = true;

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

            if (auto* state = m_states.Current())
                state->HandleInput(*this, dt);
            if (auto* state = m_states.Current())
                state->Update(*this, dt);

            m_resources.UpdateHotReload();

            if (m_sceneUpdateEnabled)
                m_world.RunSystems(SystemPhase::Update, SystemContext{ m_renderer.get(), &m_resources, dt });

            m_renderer->BeginFrame();
            m_world.RunSystems(SystemPhase::Render, SystemContext{ m_renderer.get(), &m_resources, dt });
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

        if (m_renderer)
        {
            m_renderer->Shutdown();
            m_renderer.reset();
        }

        Logger::Shutdown();
        m_initialized = false;
    }
}
