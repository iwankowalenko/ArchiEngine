#include "States.h"

#include "Application.h"
#include "InputManager.h"
#include "Logger.h"
#include "RenderAdapter.h"

#include <memory>

namespace archi
{
    namespace
    {
        void HandleWindowAndShaderShortcuts(Application& app)
        {
            auto& input = app.Input();
            auto& renderer = app.Renderer();

            if (input.WasActionJustPressed("OpenWindow"))
            {
                const RenderConfig cfg{ 640, 480, "ArchiEngine", true };
                renderer.OpenAdditionalWindow(cfg, 0.08f, 0.03f, 0.16f);
            }

            if (input.WasActionJustPressed("ReloadAssets"))
                app.RequestShaderReload();
        }

        void HandleSceneShortcuts(Application& app)
        {
            auto& input = app.Input();

            if (input.WasActionJustPressed("SaveScene"))
                app.SaveScene();
            if (input.WasActionJustPressed("LoadScene"))
                app.LoadScene();
            if (input.WasActionJustPressed("ResetScene"))
                app.ResetSceneToDefault();
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

    void MenuState::OnEnter(Application&)
    {
    }

    void MenuState::HandleInput(Application& app, double)
    {
        auto& input = app.Input();

        HandleWindowAndShaderShortcuts(app);
        HandleSceneShortcuts(app);

        if (input.WasActionJustPressed("MenuAccept"))
            app.States().ChangeState(app, std::make_unique<GameplayState>());

        if (input.WasKeyJustPressed(Key::Escape))
            app.RequestQuit();
    }

    void MenuState::Render(Application&, IRenderAdapter&)
    {
    }

    void GameplayState::OnEnter(Application& app)
    {
        app.SetSceneUpdateEnabled(true);
        app.ResetControlledEntityTransform();
    }

    void GameplayState::HandleInput(Application& app, double)
    {
        auto& input = app.Input();

        HandleWindowAndShaderShortcuts(app);
        HandleSceneShortcuts(app);

        if (input.WasActionJustPressed("Pause"))
            app.States().PushState(app, std::make_unique<PauseState>());

        if (input.WasKeyJustPressed(Key::Escape))
            app.States().ChangeState(app, std::make_unique<MenuState>());
    }

    void GameplayState::Update(Application&, double)
    {
    }

    void GameplayState::Render(Application&, IRenderAdapter&)
    {
    }

    void PauseState::OnEnter(Application& app)
    {
        app.SetSceneUpdateEnabled(false);
    }

    void PauseState::OnExit(Application& app)
    {
        app.SetSceneUpdateEnabled(true);
    }

    void PauseState::HandleInput(Application& app, double)
    {
        auto& input = app.Input();
        HandleWindowAndShaderShortcuts(app);
        HandleSceneShortcuts(app);
        if (input.WasActionJustPressed("Pause") || input.WasKeyJustPressed(Key::Escape))
            app.States().PopState(app);
    }
}
