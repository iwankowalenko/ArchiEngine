#pragma once

#include <filesystem>
#include <memory>

#include "DeltaTimer.h"
#include "ECS.h"
#include "ECSComponents.h"
#include "GameStateMachine.h"
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

        GameStateMachine& States() { return m_states; }
        const GameStateMachine& States() const { return m_states; }

    private:
        void BuildDemoScene();
        void RefreshSceneBindings();
        bool LoadOrCreateScene();

        bool m_initialized = false;
        bool m_quitRequested = false;
        bool m_sceneUpdateEnabled = true;

        std::unique_ptr<IRenderAdapter> m_renderer{};
        GameStateMachine m_states{};
        DeltaTimer m_timer{};
        World m_world{};
        Entity m_controlledEntity{};
        std::filesystem::path m_scenePath{};
    };
}

