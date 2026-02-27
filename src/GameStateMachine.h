#pragma once

#include <cstddef>
#include <vector>
#include <memory>

#include "GameState.h"

namespace archi
{
    class Application;

    class GameStateMachine final
    {
    public:
        void ChangeState(Application& app, std::unique_ptr<IGameState> next);
        void PushState(Application& app, std::unique_ptr<IGameState> next);
        void PopState(Application& app);

        IGameState* Current() const
        {
            return m_stack.empty() ? nullptr : m_stack.back().get();
        }

        std::size_t Depth() const { return m_stack.size(); }

    private:
        std::vector<std::unique_ptr<IGameState>> m_stack{};
    };
}

