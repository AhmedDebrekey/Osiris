#include "Editor.h"

#include "core/Engine.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "renderer/Camera.h"
#include "rhi/RHI.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Osiris {
    void Editor::BeginFrame() {
        ImGuizmo::BeginFrame();
    }

    void Editor::Draw(Scene& scene, Camera& camera, Engine& engine, float deltaTime) {
        if (ImGui::BeginMainMenuBar()) {
            m_SceneFileMenu.Draw(scene, engine.GetRHI(), engine.GetPhysics(), engine.GetAudio(),
                engine.GetScripting(), m_SceneInspector);
            ImGui::EndMainMenuBar();
        }

        ImGui::DockSpaceOverViewport();
        ImGui::Begin("Viewport");
        if (ImGui::RadioButton("Translate", m_GizmoOperation == ImGuizmo::TRANSLATE)) {
            m_GizmoOperation = ImGuizmo::TRANSLATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", m_GizmoOperation == ImGuizmo::ROTATE)) {
            m_GizmoOperation = ImGuizmo::ROTATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", m_GizmoOperation == ImGuizmo::SCALE)) {
            m_GizmoOperation = ImGuizmo::SCALE;
        }
        const ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        const uint32_t viewportWidth = static_cast<uint32_t>(glm::max(viewportSize.x, 1.0f));
        const uint32_t viewportHeight = static_cast<uint32_t>(glm::max(viewportSize.y, 1.0f));
        engine.SetEditorViewportSize(viewportWidth, viewportHeight);
        const bool viewportHovered = ImGui::IsWindowHovered();
        camera.Update(*engine.GetInput(), deltaTime, true, viewportHovered);
        engine.UpdateCameraAspect(camera);
        const uint64_t viewportTexture = engine.GetEditorViewportTextureID();
        if (viewportTexture != 0) {
            ImGui::Image(viewportTexture,
                ImVec2(static_cast<float>(viewportWidth), static_cast<float>(viewportHeight)));
            const ImVec2 viewportMin = ImGui::GetItemRectMin();
            const ImVec2 viewportMax = ImGui::GetItemRectMax();
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo::SetRect(viewportMin.x, viewportMin.y,
                viewportMax.x - viewportMin.x, viewportMax.y - viewportMin.y);
            const entt::entity selectedHandle = m_SceneInspector.GetSelectedEntity();
            if (selectedHandle != entt::null) {
                Entity selectedEntity(selectedHandle, &scene);
                if (selectedEntity.HasComponent<TransformComponent>()) {
                    auto& transform = selectedEntity.GetComponent<TransformComponent>();
                    glm::mat4 model = scene.GetWorldTransform(selectedEntity);
                    const glm::mat4 view = camera.GetViewMatrix();
                    glm::mat4 projection = camera.GetProjectionMatrix();
                    // ImGuizmo applies the screen-space Y flip itself.
                    projection[1][1] *= -1.0f;
                    const ImGuizmo::MODE gizmoMode = m_GizmoOperation == ImGuizmo::TRANSLATE
                        ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
                    if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
                        m_GizmoOperation, gizmoMode, glm::value_ptr(model))) {
                        glm::mat4 localModel = model;
                        Entity parent = scene.GetParent(selectedEntity);
                        if (parent.IsValid()) {
                            // Non-uniformly scaled ancestors can introduce shear that local TRS cannot represent exactly.
                            localModel = glm::inverse(scene.GetWorldTransform(parent)) * model;
                        }
                        transform.SetFromMatrix(localModel);
                    }
                }
            }
            m_AssetBrowser.DrawViewportDropTarget(scene, camera, engine.GetRHI());
        }
        ImGui::End();

        m_AssetBrowser.Draw(scene, camera, engine.GetRHI());

        ImGui::Begin("Stats");
        ImGui::Text("FPS: %.1f", deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f);
        ImGui::Text("Frame time: %.3f ms", deltaTime * 1000.0f);
        ImGui::Separator();
        ImGui::Text("Camera: %.2f %.2f %.2f",
            camera.GetPosition().x, camera.GetPosition().y, camera.GetPosition().z);
        ImGui::SliderFloat("Camera Speed", &camera.GetSpeed(), 0.1f, 20.0f);
        ImGui::Separator();

        // Scene::FindCameraEntity() is the same lookup Play mode itself uses, so this reflects
        // whatever entity is actually driving the game, not a testbed-specific hardcoded one.
        Entity primaryCamera = scene.FindCameraEntity();
        if (primaryCamera.IsValid() && primaryCamera.HasComponent<CharacterComponent>()) {
            auto& character = primaryCamera.GetComponent<CharacterComponent>();
            glm::vec3 feet = glm::vec3(scene.GetWorldTransform(primaryCamera)[3]);
            ImGui::Text("Character feet: %.2f %.2f %.2f", feet.x, feet.y, feet.z);
            ImGui::Text("Grounded: %s", engine.GetPhysics()->IsCharacterGrounded(character.characterHandle) ? "yes" : "no");
            ImGui::Text("WASD to move, Space to jump, mouse (RMB held) to look");
            ImGui::Separator();
        }
        ImGui::Text("Draw calls: %d", scene.GetDrawCallCount());
        ImGui::Text("Culled: %d", scene.GetCulledCount());
        ImGui::End();

        ImGui::Begin("Lighting");
        auto& light = engine.GetRHI()->GetDirectionalLight();
        ImGui::SliderFloat3("Light Direction", &light.direction.x, -1.0f, 1.0f);
        light.direction = glm::normalize(light.direction);
        if (glm::abs(light.direction.y) > 0.999f) {
            light.direction.y = glm::sign(light.direction.y) * 0.999f;
            light.direction = glm::normalize(light.direction);
        }
        ImGui::ColorEdit3("Light Color", &light.color.x);
        ImGui::SliderFloat("Light Intensity", &light.intensity, 0.0f, 5.0f);
        ImGui::Separator();

        ImGui::SliderFloat("Environment Exposure", &engine.GetRHI()->GetEnvironmentExposure(),
            0.001f, 4.0f, "%.4f", ImGuiSliderFlags_Logarithmic);
        ImGui::End();

        ImGui::Begin("Shadow Debug");
        auto& shadowSettings = engine.GetRHI()->GetShadowSettings();
        ImGui::SliderFloat("Shadow Near", &shadowSettings.nearClip, 0.01f, 5.0f);
        ImGui::SliderFloat("Shadow Far", &shadowSettings.farClip, 5.0f, 50.0f);
        ImGui::SliderFloat("Cascade Lambda", &shadowSettings.cascadeSplitLambda, 0.0f, 1.0f);
        ImGui::Separator();

        ImGui::Checkbox("Debug: Light View", &m_DebugLightView);
        if (m_DebugLightView) {
            ImGui::SliderInt("Cascade", &m_DebugCascade, 0, 2);
        }

        if (ImGui::CollapsingHeader("Light Space Matrices")) {
            for (int cascade = 0; cascade < 3; cascade++) {
                glm::mat4 m = engine.GetRHI()->GetLightSpaceMatrix(cascade);
                ImGui::Text("Cascade %d Light Space Matrix:", cascade);
                for (int row = 0; row < 4; row++) {
                    ImGui::Text("%.3f %.3f %.3f %.3f",
                        m[0][row], m[1][row], m[2][row], m[3][row]);
                }
                ImGui::Separator();
            }
        }
        ImGui::End();

        m_SceneInspector.Draw(scene, engine.GetPhysics(), engine.GetAudio(), engine.GetScripting());
    }
}
