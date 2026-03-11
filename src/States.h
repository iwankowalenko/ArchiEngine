#pragma once

#include "GameState.h"

namespace archi
{
    class LoadingState final : public IGameState
    {
    public:
        const char* Name() const override { return "Loading"; }
        void OnEnter(Application& app) override;
        void Update(Application& app, double dt) override;
        void Render(Application& app, IRenderAdapter& renderer) override;

    private:
        double m_elapsed = 0.0;
    };

    class MenuState final : public IGameState
    {
    public:
        const char* Name() const override { return "Menu"; }
        void OnEnter(Application& app) override;
        void HandleInput(Application& app, double dt) override;
        void Render(Application& app, IRenderAdapter& renderer) override;

    private:
        bool m_prevEnterDown = false;
        bool m_prevNDown = false;
        bool m_prevRDown = false;
        bool m_prevKDown = false;
        bool m_prevLDown = false;
    };

    class GameplayState final : public IGameState
    {
    public:
        const char* Name() const override { return "Gameplay"; }
        void OnEnter(Application& app) override;
        void HandleInput(Application& app, double dt) override;
        void Update(Application& app, double dt) override;
        void Render(Application& app, IRenderAdapter& renderer) override;

    private:
        bool m_prevPDown = false;
        bool m_prevNDown = false;
        bool m_prevRDown = false;
        bool m_prevKDown = false;
        bool m_prevLDown = false;
    };

    class PauseState final : public IGameState
    {
    public:
        const char* Name() const override { return "Pause"; }
        void OnEnter(Application& app) override;
        void OnExit(Application& app) override;
        void HandleInput(Application& app, double dt) override;

    private:
        bool m_prevPDown = false;
    };
}

