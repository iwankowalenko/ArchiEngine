#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace archi
{
    class IRenderAdapter;

    using EntityId = std::uint32_t;
    constexpr EntityId InvalidEntityId = 0;

    struct Entity
    {
        EntityId id = InvalidEntityId;

        constexpr explicit operator bool() const noexcept
        {
            return id != InvalidEntityId;
        }
    };

    inline bool operator==(Entity lhs, Entity rhs)
    {
        return lhs.id == rhs.id;
    }

    inline bool operator!=(Entity lhs, Entity rhs)
    {
        return !(lhs == rhs);
    }

    inline bool operator<(Entity lhs, Entity rhs)
    {
        return lhs.id < rhs.id;
    }

    enum class SystemPhase
    {
        Update,
        Render
    };

    struct SystemContext
    {
        IRenderAdapter* renderer = nullptr;
        double deltaTime = 0.0;
    };

    class World;

    class ISystem
    {
    public:
        virtual ~ISystem() = default;

        virtual const char* Name() const = 0;
        virtual SystemPhase Phase() const = 0;
        virtual void Update(World& world, const SystemContext& context) = 0;
    };

    namespace detail
    {
        class IComponentStorage
        {
        public:
            virtual ~IComponentStorage() = default;

            virtual void Remove(Entity entity) = 0;
            virtual void Clear() = 0;
        };

        template <typename T>
        class ComponentStorage final : public IComponentStorage
        {
        public:
            template <typename... TArgs>
            T& EmplaceOrReplace(Entity entity, TArgs&&... args)
            {
                auto [it, inserted] = m_components.try_emplace(entity.id, std::forward<TArgs>(args)...);
                if (!inserted)
                    it->second = T(std::forward<TArgs>(args)...);
                return it->second;
            }

            void Remove(Entity entity) override
            {
                m_components.erase(entity.id);
            }

            void Clear() override
            {
                m_components.clear();
            }

            bool Contains(Entity entity) const
            {
                return m_components.find(entity.id) != m_components.end();
            }

            T* Get(Entity entity)
            {
                const auto it = m_components.find(entity.id);
                return it != m_components.end() ? &it->second : nullptr;
            }

            const T* Get(Entity entity) const
            {
                const auto it = m_components.find(entity.id);
                return it != m_components.end() ? &it->second : nullptr;
            }

            std::map<EntityId, T>& Data()
            {
                return m_components;
            }

            const std::map<EntityId, T>& Data() const
            {
                return m_components;
            }

        private:
            std::map<EntityId, T> m_components{};
        };
    }

    class World
    {
    public:
        Entity CreateEntity()
        {
            const Entity entity{ m_nextEntityId++ };
            m_aliveEntities.insert(entity.id);
            return entity;
        }

        Entity CreateEntityWithId(EntityId entityId)
        {
            if (entityId == InvalidEntityId || m_aliveEntities.find(entityId) != m_aliveEntities.end())
                return {};

            m_aliveEntities.insert(entityId);
            if (entityId >= m_nextEntityId)
                m_nextEntityId = entityId + 1;
            return Entity{ entityId };
        }

        void DestroyEntity(Entity entity)
        {
            if (!IsAlive(entity))
                return;

            m_aliveEntities.erase(entity.id);
            for (auto& [_, storage] : m_componentStorages)
                storage->Remove(entity);
        }

        void ClearEntities()
        {
            m_aliveEntities.clear();
            for (auto& [_, storage] : m_componentStorages)
                storage->Clear();
        }

        bool IsAlive(Entity entity) const
        {
            return entity && (m_aliveEntities.find(entity.id) != m_aliveEntities.end());
        }

        std::size_t EntityCount() const
        {
            return m_aliveEntities.size();
        }

        template <typename TFunc>
        void ForEachEntity(TFunc&& func)
        {
            for (const EntityId id : m_aliveEntities)
                func(Entity{ id });
        }

        template <typename TFunc>
        void ForEachEntity(TFunc&& func) const
        {
            for (const EntityId id : m_aliveEntities)
                func(Entity{ id });
        }

        template <typename TComponent, typename... TArgs>
        TComponent& AddComponent(Entity entity, TArgs&&... args)
        {
            return EnsureStorage<TComponent>().EmplaceOrReplace(entity, std::forward<TArgs>(args)...);
        }

        template <typename TComponent>
        void RemoveComponent(Entity entity)
        {
            if (auto* storage = TryGetStorage<TComponent>())
                storage->Remove(entity);
        }

        template <typename TComponent>
        bool HasComponent(Entity entity) const
        {
            const auto* storage = TryGetStorage<TComponent>();
            return storage ? storage->Contains(entity) : false;
        }

        template <typename TComponent>
        TComponent* GetComponent(Entity entity)
        {
            auto* storage = TryGetStorage<TComponent>();
            return storage ? storage->Get(entity) : nullptr;
        }

        template <typename TComponent>
        const TComponent* GetComponent(Entity entity) const
        {
            const auto* storage = TryGetStorage<TComponent>();
            return storage ? storage->Get(entity) : nullptr;
        }

        template <typename TSystem, typename... TArgs>
        TSystem& AddSystem(TArgs&&... args)
        {
            static_assert(std::is_base_of<ISystem, TSystem>::value, "TSystem must derive from ISystem");

            auto system = std::make_unique<TSystem>(std::forward<TArgs>(args)...);
            TSystem& ref = *system;
            m_systems.push_back(std::move(system));
            return ref;
        }

        void RunSystems(SystemPhase phase, const SystemContext& context)
        {
            for (auto& system : m_systems)
            {
                if (system && system->Phase() == phase)
                    system->Update(*this, context);
            }
        }

        template <typename TFirst, typename... TRest, typename TFunc>
        void ForEach(TFunc&& func)
        {
            auto* firstStorage = TryGetStorage<TFirst>();
            if (!firstStorage)
                return;

            for (auto& [id, firstComponent] : firstStorage->Data())
            {
                const Entity entity{ id };
                if (!IsAlive(entity))
                    continue;

                if ((HasComponent<TRest>(entity) && ...))
                    func(entity, firstComponent, *GetComponent<TRest>(entity)...);
            }
        }

        template <typename TFirst, typename... TRest, typename TFunc>
        void ForEach(TFunc&& func) const
        {
            const auto* firstStorage = TryGetStorage<TFirst>();
            if (!firstStorage)
                return;

            for (const auto& [id, firstComponent] : firstStorage->Data())
            {
                const Entity entity{ id };
                if (!IsAlive(entity))
                    continue;

                if ((HasComponent<TRest>(entity) && ...))
                    func(entity, firstComponent, *GetComponent<TRest>(entity)...);
            }
        }

    private:
        template <typename TComponent>
        detail::ComponentStorage<TComponent>& EnsureStorage()
        {
            const auto key = std::type_index(typeid(TComponent));
            auto it = m_componentStorages.find(key);
            if (it == m_componentStorages.end())
            {
                auto storage = std::make_unique<detail::ComponentStorage<TComponent>>();
                auto [insertedIt, _] = m_componentStorages.emplace(key, std::move(storage));
                it = insertedIt;
            }
            return *static_cast<detail::ComponentStorage<TComponent>*>(it->second.get());
        }

        template <typename TComponent>
        detail::ComponentStorage<TComponent>* TryGetStorage()
        {
            const auto it = m_componentStorages.find(std::type_index(typeid(TComponent)));
            return it != m_componentStorages.end()
                ? static_cast<detail::ComponentStorage<TComponent>*>(it->second.get())
                : nullptr;
        }

        template <typename TComponent>
        const detail::ComponentStorage<TComponent>* TryGetStorage() const
        {
            const auto it = m_componentStorages.find(std::type_index(typeid(TComponent)));
            return it != m_componentStorages.end()
                ? static_cast<const detail::ComponentStorage<TComponent>*>(it->second.get())
                : nullptr;
        }

    private:
        EntityId m_nextEntityId = 1;
        std::set<EntityId> m_aliveEntities{};
        std::unordered_map<std::type_index, std::unique_ptr<detail::IComponentStorage>> m_componentStorages{};
        std::vector<std::unique_ptr<ISystem>> m_systems{};
    };
}
