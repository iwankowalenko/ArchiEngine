#pragma once

#include <filesystem>
#include <memory>

#include "DeltaTimer.h"
#include "ECS.h"
#include "ECSComponents.h"
#include "EventBus.h"
#include "GameStateMachine.h"
#include "InputManager.h"
#include "ResourceManager.h"
#include "RenderAdapter.h"

namespace archi
{
    class Application final
    {
    public:
        Application();
        ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        bool Init();
        int Run();
        void Shutdown();

        void RequestQuit() { m_quitRequested = true; }

        IRenderAdapter& Renderer() { return *m_renderer; }
        const IRenderAdapter& Renderer() const { return *m_renderer; }

        World& SceneWorld() { return m_world; }
        const World& SceneWorld() const { return m_world; }

        Entity ControlledEntity() const { return m_controlledEntity; }
        Transform* ControlledTransform();
        const Transform* ControlledTransform() const;

        void SetSceneUpdateEnabled(bool enabled) { m_sceneUpdateEnabled = enabled; }
        bool IsSceneUpdateEnabled() const { return m_sceneUpdateEnabled; }
        void ResetControlledEntityTransform();
        Entity FindEntityByTag(const char* name) const;
        bool SaveScene();
        bool LoadScene();
        bool ResetSceneToDefault();
        void RequestShaderReload();

        GameStateMachine& States() { return m_states; }
        const GameStateMachine& States() const { return m_states; }
        ResourceManager& Resources() { return m_resources; }
        const ResourceManager& Resources() const { return m_resources; }
        InputManager& Input() { return m_input; }
        const InputManager& Input() const { return m_input; }
        EventBus& Events() { return m_events; }
        const EventBus& Events() const { return m_events; }
        bool IsPhysicsDebugEnabled() const { return m_debugPhysicsEnabled; }
        void TogglePhysicsDebug() { m_debugPhysicsEnabled = !m_debugPhysicsEnabled; }

    private:
        void BuildDemoScene();
        void RefreshSceneBindings();
        bool LoadOrCreateScene();
        bool WarmUpSceneResources();

        bool m_initialized = false;
        bool m_quitRequested = false;
        bool m_sceneUpdateEnabled = true;
        bool m_debugPhysicsEnabled = false;

        std::unique_ptr<IRenderAdapter> m_renderer{};
        GameStateMachine m_states{};
        DeltaTimer m_timer{};
        World m_world{};
        ResourceManager m_resources{};
        InputManager m_input{};
        EventBus m_events{};
        Entity m_controlledEntity{};
        std::filesystem::path m_scenePath{};
        double m_fixedUpdateAccumulator = 0.0;
    };
}

