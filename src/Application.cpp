#include "Application.h"

#include "Logger.h"
#include "OpenGLRenderAdapter.h"
#include "States.h"

#include <memory>

namespace archi
{
    static double ClampDouble(double v, double lo, double hi)
    {
        return (v < lo) ? lo : (v > hi ? hi : v);
    }

    Application::Application() = default;

    Application::~Application()
    {
        Shutdown();
    }

    bool Application::Init()
    {
        if (m_initialized)
            return true;

        Logger::Init("archi.log");
        Logger::SetMinLevel(LogLevel::Trace);

        Logger::Info("Application init...");

        // Avoid relying on std::make_unique availability/IDE settings.
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

        m_timer.Reset();

        // Start with loading state.
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
        int frameCounter = 0;

        while (!m_quitRequested && !m_renderer->ShouldClose())
        {
            double dt = m_timer.TickSeconds();
            // Avoid crazy jumps after breakpoints / window dragging etc.
            dt = ClampDouble(dt, 0.0, 0.25);

            logAccumulator += dt;
            frameCounter++;
            if (logAccumulator >= 0.5)
            {
                Logger::Info("deltaTime (s) last frame: ", dt, " | approx FPS: ", (dt > 0.0 ? (1.0 / dt) : 0.0));
                logAccumulator = 0.0;
                frameCounter = 0;
            }
            else
            {
                Logger::Trace("dt=", dt);
            }

            m_renderer->PollEvents();

            // Important: HandleInput can change the active state.
            if (auto* s = m_states.Current())
                s->HandleInput(*this, dt);
            if (auto* s = m_states.Current())
                s->Update(*this, dt);

            m_renderer->BeginFrame();
            if (auto* s = m_states.Current())
                s->Render(*this, *m_renderer);
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

        // Drop all states.
        m_states.ChangeState(*this, nullptr);

        if (m_renderer)
        {
            m_renderer->Shutdown();
            m_renderer.reset();
        }

        Logger::Shutdown();
        m_initialized = false;
    }
}

