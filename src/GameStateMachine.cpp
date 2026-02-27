#include "GameStateMachine.h"

#include "Application.h"
#include "Logger.h"

namespace archi
{
    void GameStateMachine::ChangeState(Application& app, std::unique_ptr<IGameState> next)
    {
        while (!m_stack.empty())
        {
            Logger::Info("Leaving state: ", m_stack.back()->Name());
            m_stack.back()->OnExit(app);
            m_stack.pop_back();
        }

        if (next)
            PushState(app, std::move(next));
    }

    void GameStateMachine::PushState(Application& app, std::unique_ptr<IGameState> next)
    {
        if (!next)
            return;

        Logger::Info("Entering state: ", next->Name());
        next->OnEnter(app);
        m_stack.push_back(std::move(next));
    }

    void GameStateMachine::PopState(Application& app)
    {
        if (m_stack.empty())
            return;

        Logger::Info("Leaving state: ", m_stack.back()->Name());
        m_stack.back()->OnExit(app);
        m_stack.pop_back();

        if (m_stack.empty())
        {
            Logger::Info("State stack empty");
        }
    }
}

