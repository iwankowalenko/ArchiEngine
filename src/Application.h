#pragma once

#include <memory>

#include "DeltaTimer.h"
#include "GameStateMachine.h"
#include "Math2D.h"
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

        GameStateMachine& States() { return m_states; }
        const GameStateMachine& States() const { return m_states; }

        Transform2D& TestTransform() { return m_testTransform; }
        const Transform2D& TestTransform() const { return m_testTransform; }

    private:
        bool m_initialized = false;
        bool m_quitRequested = false;

        std::unique_ptr<IRenderAdapter> m_renderer{};
        GameStateMachine m_states{};
        DeltaTimer m_timer{};

        Transform2D m_testTransform{};
    };
}

