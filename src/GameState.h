#pragma once

namespace archi
{
    class Application;
    class IRenderAdapter;

    class IGameState
    {
    public:
        virtual ~IGameState() = default;

        virtual const char* Name() const = 0;

        virtual void OnEnter(Application&) {}
        virtual void OnExit(Application&) {}

        virtual void HandleInput(Application&, double /*dt*/) {}
        virtual void Update(Application&, double /*dt*/) {}
        virtual void Render(Application&, IRenderAdapter&) {}
    };
}

