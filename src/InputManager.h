#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>

#include "Math2D.h"
#include "RenderAdapter.h"

namespace archi
{
    class InputManager final
    {
    public:
        void Update(IRenderAdapter& renderer)
        {
            m_previousKeys = m_currentKeys;
            m_previousMouseButtons = m_currentMouseButtons;
            m_previousMousePosition = m_mousePosition;

            for (std::size_t index = 0; index < m_currentKeys.size(); ++index)
                m_currentKeys[index] = renderer.IsKeyDown(static_cast<Key>(index)) ? 1u : 0u;

            for (std::size_t index = 0; index < m_currentMouseButtons.size(); ++index)
                m_currentMouseButtons[index] = renderer.IsMouseButtonDown(static_cast<MouseButton>(index)) ? 1u : 0u;

            m_mousePosition = renderer.MousePosition();
            m_mouseDelta = m_mousePosition - m_previousMousePosition;
            m_scrollDeltaY = renderer.ConsumeScrollDeltaY();
        }

        void BindAction(const std::string& actionName, Key key)
        {
            m_actions[actionName] = key;
        }

        bool IsKeyPressed(Key key) const
        {
            const std::size_t index = static_cast<std::size_t>(key);
            return index < m_currentKeys.size() ? (m_currentKeys[index] != 0u) : false;
        }

        bool WasKeyJustPressed(Key key) const
        {
            const std::size_t index = static_cast<std::size_t>(key);
            return index < m_currentKeys.size()
                ? (m_currentKeys[index] != 0u && m_previousKeys[index] == 0u)
                : false;
        }

        bool IsMouseButtonPressed(MouseButton button) const
        {
            const std::size_t index = static_cast<std::size_t>(button);
            return index < m_currentMouseButtons.size() ? (m_currentMouseButtons[index] != 0u) : false;
        }

        bool WasMouseButtonJustPressed(MouseButton button) const
        {
            const std::size_t index = static_cast<std::size_t>(button);
            return index < m_currentMouseButtons.size()
                ? (m_currentMouseButtons[index] != 0u && m_previousMouseButtons[index] == 0u)
                : false;
        }

        bool IsActionActive(const std::string& actionName) const
        {
            const auto it = m_actions.find(actionName);
            return it != m_actions.end() ? IsKeyPressed(it->second) : false;
        }

        bool WasActionJustPressed(const std::string& actionName) const
        {
            const auto it = m_actions.find(actionName);
            return it != m_actions.end() ? WasKeyJustPressed(it->second) : false;
        }

        Vec2 MousePosition() const
        {
            return m_mousePosition;
        }

        Vec2 MouseDelta() const
        {
            return m_mouseDelta;
        }

        float ScrollDeltaY() const
        {
            return m_scrollDeltaY;
        }

    private:
        std::unordered_map<std::string, Key> m_actions{};
        std::array<unsigned char, static_cast<std::size_t>(Key::Count)> m_currentKeys{};
        std::array<unsigned char, static_cast<std::size_t>(Key::Count)> m_previousKeys{};
        std::array<unsigned char, 2> m_currentMouseButtons{};
        std::array<unsigned char, 2> m_previousMouseButtons{};
        Vec2 m_mousePosition{};
        Vec2 m_previousMousePosition{};
        Vec2 m_mouseDelta{};
        float m_scrollDeltaY = 0.0f;
    };
}
