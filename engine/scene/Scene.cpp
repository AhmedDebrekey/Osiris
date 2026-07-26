//
// Created by ahtal on 25/07/2026.
//

#include "Scene.h"

#include "renderer/Frustum.h"
#include "renderer/Camera.h"
#include "rhi/RHI.h"

namespace Osiris {
    Entity Scene::CreateEntity(const std::string& name) {
        const entt::entity handle = m_Registry.create();
        Entity entity(handle, this);
        entity.AddComponent<TagComponent>(name);
        entity.AddComponent<TransformComponent>();
        return entity;
    }

    void Scene::Render(IRHI* rhi, const Camera& camera) {
        const Frustum frustum = Frustum::FromViewProjection(
            camera.GetProjectionMatrix() * camera.GetViewMatrix()
        );

        uint32_t drawCalls = 0;
        uint32_t culled = 0;

        const auto view = m_Registry.view<TransformComponent, MeshComponent, MaterialComponent>();
        for (auto entity : view) {
            auto& transform = view.get<TransformComponent>(entity);
            auto& mesh      = view.get<MeshComponent>(entity);
            auto& material  = view.get<MaterialComponent>(entity);

            glm::mat4 model = transform.GetModelMatrix();
            if (!frustum.IsVisible(mesh.mesh.bounds, model)) {
                culled++;
                continue;
            }
            drawCalls++;
            rhi->SetModelMatrix(model);
            rhi->SetMeshData(mesh.mesh);
            rhi->BindMaterial(material.material);
            rhi->DrawIndexed(mesh.mesh.indexCount);
        }
    //OSIRIS_INFO("Draw calls: {} Culled: {}", drawCalls, culled);    }
} // Osiris