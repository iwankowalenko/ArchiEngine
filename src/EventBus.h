#pragma once

#include <functional>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ECS.h"
#include "Math2D.h"

namespace archi
{
    struct CollisionEvent
    {
        Entity first{};
        Entity second{};
        Vec3 normal{ 0.0f, 0.0f, 0.0f };
        float penetration = 0.0f;
        Vec3 displacementFirst{ 0.0f, 0.0f, 0.0f };
        Vec3 displacementSecond{ 0.0f, 0.0f, 0.0f };
        Vec3 positionFirst{ 0.0f, 0.0f, 0.0f };
        Vec3 positionSecond{ 0.0f, 0.0f, 0.0f };
        bool began = true;
    };

    struct TriggerEvent
    {
        Entity first{};
        Entity second{};
        Vec3 normal{ 0.0f, 0.0f, 0.0f };
        float penetration = 0.0f;
        Vec3 positionFirst{ 0.0f, 0.0f, 0.0f };
        Vec3 positionSecond{ 0.0f, 0.0f, 0.0f };
        bool began = true;
    };

    class EventBus final
    {
    public:
        template <typename TEvent, typename THandler>
        void Subscribe(THandler&& handler)
        {
            auto& handlers = m_subscribers[std::type_index(typeid(TEvent))];
            handlers.emplace_back(
                [fn = std::function<void(const TEvent&)>(std::forward<THandler>(handler))](const void* eventData) {
                    fn(*static_cast<const TEvent*>(eventData));
                });
        }

        template <typename TEvent>
        void Publish(const TEvent& event) const
        {
            const auto it = m_subscribers.find(std::type_index(typeid(TEvent)));
            if (it == m_subscribers.end())
                return;

            for (const auto& handler : it->second)
                handler(&event);
        }

    private:
        using UntypedHandler = std::function<void(const void*)>;
        std::unordered_map<std::type_index, std::vector<UntypedHandler>> m_subscribers{};
    };
}
