//
// Created by ahtal on 25/07/2026.
//

#include "Scene.h"

#include "rhi/RHI.h"

namespace Osiris {
    Entity Scene::CreateEntity(const std::string& name) {
        const entt::entity handle = m_Registry.create();
        Entity entity(handle, this);
        entity.AddComponent<TagComponent>(name);
        entity.AddComponent<TransformComponent>();
        return entity;
    }

    void Scene::Render(IRHI *rhi, const Camera &camera) {
        auto view = m_Registry.view<TransformComponent, MeshComponent, MaterialComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& mesh      = view.get<MeshComponent>(entity);
            auto& material  = view.get<MaterialComponent>(entity);

            rhi->SetModelMatrix(transform.GetModelMatrix());
            rhi->SetMeshData(mesh.mesh);
            rhi->BindTexture(material.albedo);
            rhi->DrawIndexed(mesh.mesh.indexCount);
        }
    }

} // Osiris