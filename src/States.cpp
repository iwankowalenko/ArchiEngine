#include "States.h"

#include "Application.h"
#include "Logger.h"
#include "RenderAdapter.h"

#include <memory>

namespace archi
{
    namespace
    {
        void HandleWindowAndShaderShortcuts(Application& app, bool& prevNDown, bool& prevRDown)
        {
            auto& renderer = app.Renderer();

            const bool nDown = renderer.IsKeyDown(Key::N);
            if (nDown && !prevNDown)
            {
                const RenderConfig cfg{ 640, 480, "ArchiEngine", true };
                renderer.OpenAdditionalWindow(cfg, 0.08f, 0.03f, 0.16f);
            }
            prevNDown = nDown;

            const bool rDown = renderer.IsKeyDown(Key::R);
            if (rDown && !prevRDown)
                app.RequestShaderReload();
            prevRDown = rDown;
        }

        void HandleSceneSerializationShortcuts(Application& app, bool& prevKDown, bool& prevLDown)
        {
            auto& renderer = app.Renderer();

            const bool kDown = renderer.IsKeyDown(Key::K);
            if (kDown && !prevKDown)
                app.SaveScene();
            prevKDown = kDown;

            const bool lDown = renderer.IsKeyDown(Key::L);
            if (lDown && !prevLDown)
                app.LoadScene();
            prevLDown = lDown;
        }

        void UpdateControlledEntityFromInput(Application& app, double dt, float moveSpeed, float rotateSpeed, float scaleSpeed)
        {
            auto* transform = app.ControlledTransform();
            if (!transform)
                return;

            auto& renderer = app.Renderer();

            Vec3 direction{ 0.0f, 0.0f, 0.0f };
            if (renderer.IsKeyDown(Key::Up))
                direction.y += 1.0f;
            if (renderer.IsKeyDown(Key::Down))
                direction.y -= 1.0f;
            if (renderer.IsKeyDown(Key::Right))
                direction.x += 1.0f;
            if (renderer.IsKeyDown(Key::Left))
                direction.x -= 1.0f;

            transform->position += direction * (moveSpeed * static_cast<float>(dt));

            if (renderer.IsMouseButtonDown(MouseButton::Left))
            {
                const bool shrink = renderer.IsKeyDown(Key::LeftShift);
                const float scaleFactor = 1.0f + (shrink ? -1.0f : 1.0f) * scaleSpeed * static_cast<float>(dt);
                transform->scale.x *= scaleFactor;
                transform->scale.y *= scaleFactor;
            }

            if (renderer.IsMouseButtonDown(MouseButton::Right))
                transform->rotation.z += rotateSpeed * static_cast<float>(dt);

            const float wheel = renderer.ConsumeScrollDeltaY();
            if (wheel != 0.0f)
            {
                const float scaleFactor = 1.0f + wheel * scaleSpeed * static_cast<float>(dt);
                transform->scale.x *= scaleFactor;
                transform->scale.y *= scaleFactor;
            }

            if (transform->scale.x < 0.10f)
                transform->scale.x = 0.10f;
            if (transform->scale.y < 0.10f)
                transform->scale.y = 0.10f;

            if (transform->position.x < -1.9f)
                transform->position.x = -1.9f;
            if (transform->position.x > 1.9f)
                transform->position.x = 1.9f;
            if (transform->position.y < -1.1f)
                transform->position.y = -1.1f;
            if (transform->position.y > 1.1f)
                transform->position.y = 1.1f;
        }
    }

    void LoadingState::OnEnter(Application& app)
    {
        app.SetSceneUpdateEnabled(true);
        m_elapsed = 0.0;
        Logger::Info("Loading resources (fake)...");
    }

    void LoadingState::Update(Application& app, double dt)
    {
        m_elapsed += dt;
        if (m_elapsed >= 1.0)
            app.States().ChangeState(app, std::make_unique<MenuState>());
    }

    void LoadingState::Render(Application&, IRenderAdapter&)
    {
    }

    void MenuState::HandleInput(Application& app, double dt)
    {
        auto& renderer = app.Renderer();

        UpdateControlledEntityFromInput(app, dt, 1.0f, 2.2f, 1.2f);
        HandleWindowAndShaderShortcuts(app, m_prevNDown, m_prevRDown);
        HandleSceneSerializationShortcuts(app, m_prevKDown, m_prevLDown);

        const bool enterDown = renderer.IsKeyDown(Key::Enter);
        if (enterDown && !m_prevEnterDown)
            app.States().ChangeState(app, std::make_unique<GameplayState>());
        m_prevEnterDown = enterDown;

        if (renderer.IsKeyDown(Key::Escape))
            app.RequestQuit();
    }

    void MenuState::Render(Application&, IRenderAdapter&)
    {
    }

    void GameplayState::OnEnter(Application& app)
    {
        app.SetSceneUpdateEnabled(true);
        app.ResetControlledEntityTransform();
        m_prevPDown = app.Renderer().IsKeyDown(Key::P);
        m_prevNDown = app.Renderer().IsKeyDown(Key::N);
        m_prevRDown = app.Renderer().IsKeyDown(Key::R);
        m_prevKDown = app.Renderer().IsKeyDown(Key::K);
        m_prevLDown = app.Renderer().IsKeyDown(Key::L);
    }

    void GameplayState::HandleInput(Application& app, double dt)
    {
        auto& renderer = app.Renderer();

        UpdateControlledEntityFromInput(app, dt, 1.4f, 2.6f, 1.5f);
        HandleWindowAndShaderShortcuts(app, m_prevNDown, m_prevRDown);
        HandleSceneSerializationShortcuts(app, m_prevKDown, m_prevLDown);

        const bool pDown = renderer.IsKeyDown(Key::P);
        if (pDown && !m_prevPDown)
            app.States().PushState(app, std::make_unique<PauseState>());
        m_prevPDown = pDown;

        if (renderer.IsKeyDown(Key::Escape))
            app.States().ChangeState(app, std::make_unique<MenuState>());
    }

    void GameplayState::Update(Application& app, double)
    {
        auto* transform = app.ControlledTransform();
        if (!transform)
            return;

        if (transform->position.x < -1.9f)
            transform->position.x = -1.9f;
        if (transform->position.x > 1.9f)
            transform->position.x = 1.9f;
        if (transform->position.y < -1.1f)
            transform->position.y = -1.1f;
        if (transform->position.y > 1.1f)
            transform->position.y = 1.1f;
    }

    void GameplayState::Render(Application&, IRenderAdapter&)
    {
    }

    void PauseState::OnEnter(Application& app)
    {
        app.SetSceneUpdateEnabled(false);
        m_prevPDown = app.Renderer().IsKeyDown(Key::P);
    }

    void PauseState::OnExit(Application& app)
    {
        app.SetSceneUpdateEnabled(true);
    }

    void PauseState::HandleInput(Application& app, double)
    {
        auto& renderer = app.Renderer();

        const bool pDown = renderer.IsKeyDown(Key::P);
        const bool escDown = renderer.IsKeyDown(Key::Escape);
        if ((pDown && !m_prevPDown) || escDown)
            app.States().PopState(app);
        m_prevPDown = pDown;
    }

    void MenuState::OnEnter(Application& app)
    {
        m_prevEnterDown = app.Renderer().IsKeyDown(Key::Enter);
        m_prevNDown = app.Renderer().IsKeyDown(Key::N);
        m_prevRDown = app.Renderer().IsKeyDown(Key::R);
        m_prevKDown = app.Renderer().IsKeyDown(Key::K);
        m_prevLDown = app.Renderer().IsKeyDown(Key::L);
    }
}
