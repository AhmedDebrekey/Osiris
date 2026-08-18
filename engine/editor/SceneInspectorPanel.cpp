#include "SceneInspectorPanel.h"

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"

#include <imgui.h>
#include <cstring>

namespace Osiris {
    namespace {
        // Draws drawFn's fields inside a CollapsingHeader, but only if the entity actually has
        // component T. Each component type gets one call here — adding a new one to the panel
        // means adding a line, not touching a hardcoded switch/if-chain.
        template<typename T, typename DrawFn>
        void DrawComponentSection(Entity entity, const char* label, DrawFn&& drawFn) {
            if (!entity.HasComponent<T>()) return;
            ImGui::PushID(label);
            if (ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
                drawFn(entity.GetComponent<T>());
            }
            ImGui::PopID();
        }
    }

    void SceneInspectorPanel::Draw(Scene& scene) {
        ImGui::Begin("Scene Inspector");

        DrawEntityList(scene);
        ImGui::Separator();

        if (m_SelectedEntity != entt::null) {
            Entity entity(m_SelectedEntity, &scene);
            if (entity.IsValid() && entity.HasComponent<TagComponent>()) {
                DrawComponents(entity);
            } else {
                m_SelectedEntity = entt::null; // entity no longer exists
            }
        } else {
            ImGui::TextDisabled("Select an entity above to inspect it.");
        }

        ImGui::End();
    }

    void SceneInspectorPanel::DrawEntityList(Scene& scene) {
        std::vector<Entity> entities = scene.GetAllEntities();
        ImGui::Text("Entities: %d", static_cast<int>(entities.size()));

        ImGui::BeginChild("EntityList", ImVec2(0.0f, 150.0f), true);
        for (Entity entity : entities) {
            const std::string& name = entity.GetComponent<TagComponent>().name;
            bool isSelected = entity.GetHandle() == m_SelectedEntity;

            ImGui::PushID(static_cast<int>(static_cast<uint32_t>(entity.GetHandle())));
            if (ImGui::Selectable(name.c_str(), isSelected)) {
                m_SelectedEntity = entity.GetHandle();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void SceneInspectorPanel::DrawComponents(Entity entity) {
        DrawComponentSection<TagComponent>(entity, "Tag", [](TagComponent& tag) {
            char buffer[256] = {};
            strncpy_s(buffer, tag.name.c_str(), sizeof(buffer) - 1);
            if (ImGui::InputText("Name", buffer, sizeof(buffer))) {
                tag.name = buffer;
            }
        });

        DrawComponentSection<TransformComponent>(entity, "Transform", [](TransformComponent& transform) {
            ImGui::DragFloat3("Position", &transform.position.x, 0.05f);
            ImGui::DragFloat3("Rotation", &transform.rotation.x, 0.5f);
            ImGui::DragFloat3("Scale", &transform.scale.x, 0.05f, 0.001f, 1000.0f);
        });

        DrawComponentSection<MeshComponent>(entity, "Mesh", [](MeshComponent& mesh) {
            ImGui::Text("Vertices: %u", mesh.mesh.vertexCount);
            ImGui::Text("Indices: %u", mesh.mesh.indexCount);
            ImGui::TextDisabled("Read-only — GPU mesh data.");
        });

        DrawComponentSection<MaterialComponent>(entity, "Material", [](MaterialComponent& material) {
            ImGui::Text("Material handle: %u", material.material.id);
            ImGui::TextDisabled("Read-only — texture editing isn't supported yet.");
        });

        DrawComponentSection<SpotLightComponent>(entity, "Spot Light", [](SpotLightComponent& light) {
            ImGui::ColorEdit3("Color", &light.color.x);
            ImGui::DragFloat("Intensity", &light.intensity, 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat("Inner Cone", &light.innerCone, 0.5f, 1.0f, 89.0f);
            ImGui::DragFloat("Outer Cone", &light.outerCone, 0.5f, 1.0f, 89.0f);
            if (light.outerCone < light.innerCone) light.outerCone = light.innerCone;
            ImGui::DragFloat("Range", &light.range, 0.1f, 0.1f, 100.0f);
            ImGui::Checkbox("Enabled", &light.enabled);
            ImGui::Checkbox("Casts Shadow", &light.castsShadow);
        });

        DrawComponentSection<ColliderComponent>(entity, "Collider", [](ColliderComponent& collider) {
            ImGui::DragFloat3("Half Extents", &collider.halfExtents.x, 0.05f, 0.01f, 100.0f);
            ImGui::TextDisabled("Box only. Physics bodies are created once at scene setup —\nediting this here doesn't resize the live Jolt body.");
        });

        DrawComponentSection<RigidBodyComponent>(entity, "Rigid Body", [](RigidBodyComponent& rigidBody) {
            const char* motionTypeNames[] = { "Static", "Kinematic", "Dynamic" };
            int motionType = static_cast<int>(rigidBody.motionType);
            if (ImGui::Combo("Motion Type", &motionType, motionTypeNames, 3)) {
                rigidBody.motionType = static_cast<BodyMotionType>(motionType);
            }
            ImGui::Text("Body handle: %s", rigidBody.bodyHandle.IsValid() ? "valid" : "invalid");
            ImGui::TextDisabled("Physics bodies are created once at scene setup — changing\nmotion type here doesn't re-create the live Jolt body.");
        });
    }
}
