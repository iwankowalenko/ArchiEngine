#include "States.h"

#include "Application.h"
#include "Logger.h"
#include "RenderAdapter.h"

#include <memory>

namespace archi
{
    void LoadingState::OnEnter(Application& app)
    {
        (void)app;
        m_elapsed = 0.0;
        Logger::Info("Loading resources (fake)...");
    }

    void LoadingState::Update(Application& app, double dt)
    {
        m_elapsed += dt;
        if (m_elapsed >= 1.0)
        {
            app.States().ChangeState(app, std::make_unique<MenuState>());
        }
    }

    void LoadingState::Render(Application& app, IRenderAdapter& renderer)
    {
        (void)renderer;
        // Show the same primitive while "loading".
        Transform2D t = app.TestTransform();
        t.scale = { 0.7f, 0.7f };
        renderer.DrawTestPrimitive(t);
    }

    void MenuState::HandleInput(Application& app, double dt)
    {
        auto& r = app.Renderer();

        // Let input be visible even in menu.
        auto& tr = app.TestTransform();
        const float moveSpeed = 0.7f; // NDC units per second
        Vec2 dir{ 0.0f, 0.0f };
        if (r.IsKeyDown(Key::Up))
            dir.y += 1.0f;
        if (r.IsKeyDown(Key::Down))
            dir.y -= 1.0f;
        if (r.IsKeyDown(Key::Right))
            dir.x += 1.0f;
        if (r.IsKeyDown(Key::Left))
            dir.x -= 1.0f;
        tr.position += dir * (moveSpeed * static_cast<float>(dt));

        if (r.IsMouseButtonDown(MouseButton::Left))
        {
            const bool shift = r.IsKeyDown(Key::LeftShift);
            const float dirScale = shift ? -1.0f : 1.0f;
            const float s = 1.0f + dirScale * 1.0f * static_cast<float>(dt);
            tr.scale.x *= s;
            tr.scale.y *= s;
        }
        if (r.IsMouseButtonDown(MouseButton::Right))
        {
            tr.rotationRadians += 2.0f * static_cast<float>(dt);
        }

        // Mouse wheel: zoom in/out.
        const float wheel = r.ConsumeScrollDeltaY();
        if (wheel != 0.0f)
        {
            const float s = 1.0f + wheel * 1.5f * static_cast<float>(dt);
            tr.scale.x *= s;
            tr.scale.y *= s;
        }

        const bool enterDown = r.IsKeyDown(Key::Enter);
        if (enterDown && !m_prevEnterDown)
        {
            app.States().ChangeState(app, std::make_unique<GameplayState>());
        }
        m_prevEnterDown = enterDown;

        // Multi-window: N -> open another window with different background.
        const bool nDown = r.IsKeyDown(Key::N);
        if (nDown && !m_prevNDown)
        {
            RenderConfig cfg = { 640, 480, "ArchiEngine", true };
            r.OpenAdditionalWindow(cfg, 0.12f, 0.02f, 0.18f);
        }
        m_prevNDown = nDown;

        // Hot reload shaders: R -> rebuild shader program from files.
        const bool rDown = r.IsKeyDown(Key::R);
        if (rDown && !m_prevRDown)
        {
            r.RequestShaderReload();
        }
        m_prevRDown = rDown;

        if (r.IsKeyDown(Key::Escape))
        {
            app.RequestQuit();
        }

        // Keep scale sane.
        if (tr.scale.x < 0.1f)
            tr.scale.x = 0.1f;
        if (tr.scale.y < 0.1f)
            tr.scale.y = 0.1f;
    }

    void MenuState::Render(Application& app, IRenderAdapter& renderer)
    {
        Transform2D t = app.TestTransform();
        if (t.scale.x == 0.0f && t.scale.y == 0.0f)
            t.scale = { 0.9f, 0.9f };
        renderer.DrawTestPrimitive(t);
    }

    void GameplayState::OnEnter(Application& app)
    {
        app.TestTransform().position = { 0.0f, 0.0f };
        app.TestTransform().scale = { 1.0f, 1.0f };
        app.TestTransform().rotationRadians = 0.0f;
    }

    void GameplayState::HandleInput(Application& app, double dt)
    {
        auto& r = app.Renderer();
        auto& tr = app.TestTransform();

        const float moveSpeed = 0.9f; // NDC units per second
        Vec2 dir{ 0.0f, 0.0f };
        if (r.IsKeyDown(Key::Up))
            dir.y += 1.0f;
        if (r.IsKeyDown(Key::Down))
            dir.y -= 1.0f;
        if (r.IsKeyDown(Key::Right))
            dir.x += 1.0f;
        if (r.IsKeyDown(Key::Left))
            dir.x -= 1.0f;

        tr.position += dir * (moveSpeed * static_cast<float>(dt));

        // Mouse: left scales, right rotates.
        if (r.IsMouseButtonDown(MouseButton::Left))
        {
            const bool shift = r.IsKeyDown(Key::LeftShift);
            const float dirScale = shift ? -1.0f : 1.0f;
            const float s = 1.0f + dirScale * 1.2f * static_cast<float>(dt);
            tr.scale.x *= s;
            tr.scale.y *= s;
        }
        if (r.IsMouseButtonDown(MouseButton::Right))
        {
            tr.rotationRadians += 2.2f * static_cast<float>(dt);
        }

        // Mouse wheel: zoom in/out.
        const float wheel = r.ConsumeScrollDeltaY();
        if (wheel != 0.0f)
        {
            const float s = 1.0f + wheel * 1.8f * static_cast<float>(dt);
            tr.scale.x *= s;
            tr.scale.y *= s;
        }

        // Pause toggle.
        const bool pDown = r.IsKeyDown(Key::P);
        if (pDown && !m_prevPDown)
        {
            app.States().PushState(app, std::make_unique<PauseState>());
        }
        m_prevPDown = pDown;

        const bool nDown = r.IsKeyDown(Key::N);
        if (nDown && !m_prevNDown)
        {
            RenderConfig cfg = { 640, 480, "ArchiEngine", true };
            r.OpenAdditionalWindow(cfg, 0.02f, 0.12f, 0.20f);
        }
        m_prevNDown = nDown;

        const bool rDown = r.IsKeyDown(Key::R);
        if (rDown && !m_prevRDown)
        {
            r.RequestShaderReload();
        }
        m_prevRDown = rDown;

        if (r.IsKeyDown(Key::Escape))
        {
            app.States().ChangeState(app, std::make_unique<MenuState>());
        }

        if (tr.scale.x < 0.1f)
            tr.scale.x = 0.1f;
        if (tr.scale.y < 0.1f)
            tr.scale.y = 0.1f;
    }

    void GameplayState::Update(Application& app, double /*dt*/)
    {
        // Keep transform in visible range (simple clamp).
        auto& tr = app.TestTransform();
        tr.position.x = (tr.position.x < -1.2f) ? -1.2f : (tr.position.x > 1.2f ? 1.2f : tr.position.x);
        tr.position.y = (tr.position.y < -1.2f) ? -1.2f : (tr.position.y > 1.2f ? 1.2f : tr.position.y);
    }

    void GameplayState::Render(Application& app, IRenderAdapter& renderer)
    {
        renderer.DrawTestPrimitive(app.TestTransform());
    }

    void PauseState::HandleInput(Application& app, double /*dt*/)
    {
        auto& r = app.Renderer();
        const bool pDown = r.IsKeyDown(Key::P);
        const bool escDown = r.IsKeyDown(Key::Escape);

        if ((pDown && !m_prevPDown) || escDown)
        {
            app.States().PopState(app);
        }
        m_prevPDown = pDown;
    }

    void PauseState::Render(Application& app, IRenderAdapter& renderer)
    {
        // Render underlying object, but make it smaller to "hint" pause.
        Transform2D t = app.TestTransform();
        t.scale = { t.scale.x * 0.85f, t.scale.y * 0.85f };
        renderer.DrawTestPrimitive(t);
    }
}

