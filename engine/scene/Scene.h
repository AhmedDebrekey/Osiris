//
// Created by Debreky on 25/07/2026.
//

#ifndef OSIRIS_SCENE_H
#define OSIRIS_SCENE_H
#include <string>
#include <entt/entt.hpp>
#include "Entity.h"
#include "Components.h"
#include "core/Engine.h"

namespace Osiris {
    class Camera;
    class IRHI;

    class Scene {
    public:
        Entity CreateEntity(const std::string& name);
        void Update(float deltaTime);
        void Render(IRHI* rhi, const Camera& camera);

        void PreRender(IRHI * irhi);

        int GetEntityCount();
        int GetDrawCallCount();
        int GetCulledCount();

    private:
        friend class Entity;
        entt::registry m_Registry;

        uint32_t m_DrawCallCount;
        uint32_t m_CulledCount;
    };
    template<typename T, typename... Args>
    T& Entity::AddComponent(Args&&... args) {
        return m_Scene->m_Registry.emplace<T>(m_Handle, std::forward<Args>(args)...);
    }

    template<typename T>
    T& Entity::GetComponent() {
        return m_Scene->m_Registry.get<T>(m_Handle);
    }

    template<typename T>
    bool Entity::HasComponent() const {
        return m_Scene->m_Registry.all_of<T>(m_Handle);
    }
} // Osiris

#endif //OSIRIS_SCENE_H