//
// Created by ahtal on 25/07/2026.
//

#ifndef OSIRIS_ENTITY_H
#define OSIRIS_ENTITY_H
#include <entt/entt.hpp>

namespace Osiris {
    class Scene;
    class Entity {
    public:
        Entity() = default;
        Entity(entt::entity handle, Scene* scene)
            : m_Handle(handle), m_Scene(scene) {}

        template<typename T, typename... Args>
        T& AddComponent(Args&&... args);

        template<typename T>
        T& GetComponent();

        template<typename T>
        bool HasComponent() const;

        bool IsValid() const { return m_Handle != entt::null; }

        entt::entity GetHandle() const { return m_Handle; }
        bool operator==(const Entity& other) const { return m_Handle == other.m_Handle && m_Scene == other.m_Scene; }

    private:
        entt::entity m_Handle = entt::null;
        Scene*       m_Scene  = nullptr;
    };

} // Osiris

#endif //OSIRIS_ENTITY_H