#include "Editor.h"

#include "core/Engine.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "renderer/Camera.h"
#include "renderer/Frustum.h"
#include "rhi/RHI.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <array>
#include <cmath>
#include <limits>

namespace Osiris {
    namespace {
        bool ProjectToViewport(const glm::vec3& worldPosition, const glm::mat4& viewProjection,
                               const ImVec2& viewportMin, const ImVec2& viewportMax, ImVec2& screenPosition) {
            const glm::vec4 clip = viewProjection * glm::vec4(worldPosition, 1.0f);
            if (clip.w <= 0.0001f) return false;

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            screenPosition = {
                viewportMin.x + (ndc.x * 0.5f + 0.5f) * (viewportMax.x - viewportMin.x),
                viewportMin.y + (ndc.y * 0.5f + 0.5f) * (viewportMax.y - viewportMin.y),
            };
            return ndc.z >= 0.0f && ndc.z <= 1.0f;
        }

        float DistanceSquared(const ImVec2& a, const ImVec2& b) {
            const float x = a.x - b.x;
            const float y = a.y - b.y;
            return x * x + y * y;
        }
    }

    void Editor::BeginFrame() {
        ImGuizmo::BeginFrame();
    }

    bool Editor::DrawBoxColliderOverlay(Scene& scene, Entity entity, const glm::mat4& entityWorld,
                                        const Camera& camera, IPhysics* physics,
                                        const ImVec2& viewportMin, const ImVec2& viewportMax) {
        auto& collider = entity.GetComponent<ColliderComponent>();
        const glm::mat4 view = camera.GetViewMatrix();
        const glm::mat4 viewProjection = camera.GetProjectionMatrix() * view;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const bool editing = m_SceneInspector.IsColliderEditing();
        const ImU32 lineColor = editing
            ? IM_COL32(255, 178, 54, 255)
            : IM_COL32(255, 178, 54, 180);

        // AABB::GetWorldCorners orders corners by min/max-per-axis bit (bit0=X, bit1=Y, bit2=Z),
        // matching the edge-drawing XOR below (see AABB::GetWorldCorners in MeshType.h).
        const AABB colliderBounds = { collider.center - collider.halfExtents,
                                       collider.center + collider.halfExtents };
        const std::array<glm::vec3, 8> worldCorners = colliderBounds.GetWorldCorners(entityWorld);
        std::array<ImVec2, 8> screenCorners{};
        std::array<bool, 8> cornerVisible{};
        for (int i = 0; i < 8; i++) {
            cornerVisible[i] = ProjectToViewport(
                worldCorners[i], viewProjection, viewportMin, viewportMax, screenCorners[i]);
        }

        drawList->PushClipRect(viewportMin, viewportMax, true);
        for (int i = 0; i < 8; i++) {
            for (int axisBit : {1, 2, 4}) {
                const int other = i ^ axisBit;
                if (i < other && cornerVisible[i] && cornerVisible[other]) {
                    drawList->AddLine(screenCorners[i], screenCorners[other], lineColor, 2.0f);
                }
            }
        }

        if (!editing) {
            drawList->PopClipRect();
            m_ColliderFaceDragEntity = entt::null;
            m_ColliderFaceDragAxis = -1;
            m_ColliderCenterWasUsing = false;
            m_ColliderCenterChanged = false;
            return false;
        }

        bool blocksViewportPicking = false;
        glm::mat4 colliderCenterModel = entityWorld
            * glm::translate(glm::mat4(1.0f), collider.center);
        glm::mat4 gizmoProjection = camera.GetProjectionMatrix();
        gizmoProjection[1][1] *= -1.0f;
        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(gizmoProjection),
            ImGuizmo::TRANSLATE, ImGuizmo::LOCAL, glm::value_ptr(colliderCenterModel))) {
            collider.center = glm::vec3(
                glm::inverse(entityWorld) * glm::vec4(glm::vec3(colliderCenterModel[3]), 1.0f));
            m_ColliderCenterChanged = true;
            blocksViewportPicking = true;
        }

        const bool colliderCenterUsing = ImGuizmo::IsUsing();
        if (m_ColliderCenterWasUsing && !colliderCenterUsing && m_ColliderCenterChanged) {
            scene.RebuildPhysicsBody(entity, physics);
            m_ColliderCenterChanged = false;
        }
        m_ColliderCenterWasUsing = colliderCenterUsing;
        blocksViewportPicking |= colliderCenterUsing || ImGuizmo::IsOver();

        int hoveredAxis = -1;
        float hoveredSign = 1.0f;
        float nearestHandleDistance = 64.0f;
        ImVec2 hoveredHandlePosition{};
        ImVec2 hoveredAxisDirection{};
        float hoveredPixelsPerUnit = 0.0f;
        const ImVec2 mousePosition = ImGui::GetMousePos();
        const bool mouseInsideViewport = ImGui::IsMouseHoveringRect(viewportMin, viewportMax);

        for (int axis = 0; axis < 3; axis++) {
            glm::vec3 localAxis(0.0f);
            localAxis[axis] = 1.0f;
            for (float sign : {-1.0f, 1.0f}) {
                const glm::vec3 localFace = collider.center
                    + localAxis * (collider.halfExtents[axis] * sign);
                const glm::vec3 worldFace = glm::vec3(entityWorld * glm::vec4(localFace, 1.0f));
                const glm::vec3 worldAxisPoint = glm::vec3(
                    entityWorld * glm::vec4(localFace + localAxis, 1.0f));
                ImVec2 faceScreen{};
                ImVec2 axisScreen{};
                if (!ProjectToViewport(worldFace, viewProjection, viewportMin, viewportMax, faceScreen)
                    || !ProjectToViewport(worldAxisPoint, viewProjection, viewportMin, viewportMax, axisScreen)) {
                    continue;
                }

                const ImVec2 screenAxis = {axisScreen.x - faceScreen.x, axisScreen.y - faceScreen.y};
                const float pixelsPerUnit = std::sqrt(
                    screenAxis.x * screenAxis.x + screenAxis.y * screenAxis.y);
                const float distance = DistanceSquared(mousePosition, faceScreen);
                if (mouseInsideViewport && pixelsPerUnit > 1.0f && distance < nearestHandleDistance) {
                    hoveredAxis = axis;
                    hoveredSign = sign;
                    nearestHandleDistance = distance;
                    hoveredHandlePosition = faceScreen;
                    hoveredAxisDirection = {
                        screenAxis.x / pixelsPerUnit,
                        screenAxis.y / pixelsPerUnit,
                    };
                    hoveredPixelsPerUnit = pixelsPerUnit;
                }

                drawList->AddCircleFilled(faceScreen, 5.0f,
                    IM_COL32(255, 178, 54, 235), 12);
                drawList->AddCircle(faceScreen, 7.0f, IM_COL32(30, 30, 30, 255), 12, 2.0f);
            }
        }

        if (hoveredAxis >= 0 && m_ColliderFaceDragAxis < 0 && !ImGuizmo::IsOver()) {
            drawList->AddCircle(hoveredHandlePosition, 10.0f,
                IM_COL32(255, 235, 170, 255), 16, 2.0f);
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            blocksViewportPicking = true;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_ColliderFaceDragEntity = entity.GetHandle();
                m_ColliderFaceDragAxis = hoveredAxis;
                m_ColliderFaceDragSign = hoveredSign;
                m_ColliderFaceDragMouseStart = mousePosition;
                m_ColliderFaceDragAxisScreen = hoveredAxisDirection;
                m_ColliderFaceDragPixelsPerUnit = hoveredPixelsPerUnit;
                m_ColliderFaceDragStartCenter = collider.center;
                m_ColliderFaceDragStartHalfExtents = collider.halfExtents;
            }
        }

        if (m_ColliderFaceDragEntity == entity.GetHandle() && m_ColliderFaceDragAxis >= 0) {
            blocksViewportPicking = true;
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const ImVec2 mouseDelta = {
                    mousePosition.x - m_ColliderFaceDragMouseStart.x,
                    mousePosition.y - m_ColliderFaceDragMouseStart.y,
                };
                const float localMove =
                    (mouseDelta.x * m_ColliderFaceDragAxisScreen.x
                        + mouseDelta.y * m_ColliderFaceDragAxisScreen.y)
                    / m_ColliderFaceDragPixelsPerUnit;
                const int axis = m_ColliderFaceDragAxis;
                const float requestedHalfExtent = m_ColliderFaceDragStartHalfExtents[axis]
                    + m_ColliderFaceDragSign * localMove * 0.5f;
                const float newHalfExtent = glm::max(requestedHalfExtent, 0.01f);
                const float appliedFaceMove =
                    (newHalfExtent - m_ColliderFaceDragStartHalfExtents[axis])
                    * 2.0f / m_ColliderFaceDragSign;

                collider.halfExtents = m_ColliderFaceDragStartHalfExtents;
                collider.halfExtents[axis] = newHalfExtent;
                collider.center = m_ColliderFaceDragStartCenter;
                collider.center[axis] += appliedFaceMove * 0.5f;
            } else {
                scene.RebuildPhysicsBody(entity, physics);
                m_ColliderFaceDragEntity = entt::null;
                m_ColliderFaceDragAxis = -1;
            }
        }

        drawList->PopClipRect();
        return blocksViewportPicking;
    }

    void Editor::Draw(Scene& scene, Camera& camera, Engine& engine, float deltaTime) {
        const bool shortcutsAvailable = !ImGui::GetIO().WantTextInput
            && !ImGui::IsAnyItemActive()
            && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)
            && !ImGuizmo::IsUsing()
            && m_ColliderFaceDragAxis < 0;
        if (shortcutsAvailable) {
            if (ImGui::IsKeyPressed(ImGuiKey_T, false)) {
                m_GizmoOperation = ImGuizmo::TRANSLATE;
            } else if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
                m_GizmoOperation = ImGuizmo::ROTATE;
            } else if (ImGui::IsKeyPressed(ImGuiKey_G, false)) {
                m_GizmoOperation = ImGuizmo::SCALE;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                const entt::entity selectedHandle = m_SceneInspector.GetSelectedEntity();
                if (selectedHandle != entt::null) {
                    Entity selectedEntity(selectedHandle, &scene);
                    if (selectedEntity.IsValid()) {
                        scene.DestroyEntity(selectedEntity, engine.GetPhysics(), engine.GetAudio(),
                            engine.GetScripting());
                    }
                    m_SceneInspector.ClearSelection();
                }
            }
        }

        if (ImGui::BeginMainMenuBar()) {
            m_SceneFileMenu.Draw(scene, engine.GetRHI(), engine.GetPhysics(), engine.GetAudio(),
                engine.GetScripting(), m_SceneInspector, engine.GetMaxFps(), engine.IsFpsVisible());
            ImGui::EndMainMenuBar();
        }

        ImGui::DockSpaceOverViewport();
        ImGui::Begin("Viewport");
        if (ImGui::RadioButton("Translate (T)", m_GizmoOperation == ImGuizmo::TRANSLATE)) {
            m_GizmoOperation = ImGuizmo::TRANSLATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate (R)", m_GizmoOperation == ImGuizmo::ROTATE)) {
            m_GizmoOperation = ImGuizmo::ROTATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale (G)", m_GizmoOperation == ImGuizmo::SCALE)) {
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
            const bool viewportItemHovered = ImGui::IsItemHovered();
            const bool viewportClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            const bool viewportMiddleClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
            const ImVec2 viewportMin = ImGui::GetItemRectMin();
            const ImVec2 viewportMax = ImGui::GetItemRectMax();
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo::SetRect(viewportMin.x, viewportMin.y,
                viewportMax.x - viewportMin.x, viewportMax.y - viewportMin.y);
            if (viewportItemHovered && viewportMiddleClicked) {
                m_SceneInspector.ClearSelection();
            }
            bool colliderOverlayBlocksPicking = false;
            const entt::entity selectedHandle = m_SceneInspector.GetSelectedEntity();
            if (selectedHandle != entt::null) {
                Entity selectedEntity(selectedHandle, &scene);
                const glm::mat4 selectedWorld = scene.GetWorldTransform(selectedEntity);
                const bool colliderEditing = selectedEntity.HasComponent<ColliderComponent>()
                    && m_SceneInspector.IsColliderEditing();
                if (selectedEntity.HasComponent<TransformComponent>() && !colliderEditing) {
                    auto& transform = selectedEntity.GetComponent<TransformComponent>();
                    glm::mat4 model = selectedWorld;
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
                if (selectedEntity.HasComponent<ColliderComponent>()) {
                    colliderOverlayBlocksPicking = DrawBoxColliderOverlay(
                        scene, selectedEntity, selectedWorld, camera, engine.GetPhysics(),
                        viewportMin, viewportMax);
                }
            }

            // Checked after Manipulate() (not before) so IsOver()/IsUsing() reflect this frame's
            // gizmo state, not the previous frame's: otherwise grabbing a handle would also fire
            // a pick underneath it on the same click.
            if (viewportItemHovered && viewportClicked && !colliderOverlayBlocksPicking
                && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
                const ImVec2 mousePos = ImGui::GetMousePos();
                const float viewportPixelWidth = viewportMax.x - viewportMin.x;
                const float viewportPixelHeight = viewportMax.y - viewportMin.y;
                const float ndcX = 2.0f * (mousePos.x - viewportMin.x) / viewportPixelWidth - 1.0f;
                const float ndcY = 2.0f * (mousePos.y - viewportMin.y) / viewportPixelHeight - 1.0f;

                // GetProjectionMatrix() is already Vulkan-native (Y-down, [0,1] depth), matching
                // what was actually rasterized to screen, so no extra flip is needed here (unlike
                // the ImGuizmo projection above, which un-does that flip for ImGuizmo's own
                // OpenGL-convention assumptions).
                const glm::mat4 invViewProj =
                    glm::inverse(camera.GetProjectionMatrix() * camera.GetViewMatrix());
                glm::vec4 nearPoint = invViewProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
                glm::vec4 farPoint = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
                nearPoint /= nearPoint.w;
                farPoint /= farPoint.w;
                const glm::vec3 rayOrigin = glm::vec3(nearPoint);
                const glm::vec3 rayDir = glm::normalize(glm::vec3(farPoint - nearPoint));

                entt::entity closestEntity = entt::null;
                float closestDistance = std::numeric_limits<float>::max();
                for (Entity candidate : scene.GetAllEntities()) {
                    if (!candidate.HasComponent<MeshComponent>()) continue;
                    float distance = 0.0f;
                    const glm::mat4 model = scene.GetWorldTransform(candidate);
                    const AABB& bounds = candidate.GetComponent<MeshComponent>().mesh.bounds;
                    if (RayIntersectsAABB(rayOrigin, rayDir, bounds, model, distance, true)
                        && distance < closestDistance) {
                        closestDistance = distance;
                        closestEntity = candidate.GetHandle();
                    }
                }
                // SpawnModel only puts MeshComponent on per-glTF-node children (possibly nested
                // several levels deep), never on its own root, so the hit above is always some
                // sub-mesh part. Walk up to the outermost ancestor, since that's the entity that
                // actually owns the Collider/RigidBody/Script components a user expects to select.
                if (closestEntity != entt::null) {
                    Entity walker(closestEntity, &scene);
                    Entity ancestor = scene.GetParent(walker);
                    while (ancestor.IsValid()) {
                        walker = ancestor;
                        ancestor = scene.GetParent(walker);
                    }
                    closestEntity = walker.GetHandle();
                }
                m_SceneInspector.SetSelectedEntity(closestEntity);
            }

            Entity droppedEntity = m_AssetBrowser.DrawViewportDropTarget(
                scene, camera, engine.GetRHI());
            if (droppedEntity.IsValid()) {
                m_SceneInspector.SetSelectedEntity(droppedEntity.GetHandle());
            }
        }
        ImGui::End();

        Entity spawnedEntity = m_AssetBrowser.Draw(scene, camera, engine.GetRHI());
        if (spawnedEntity.IsValid()) {
            m_SceneInspector.SetSelectedEntity(spawnedEntity.GetHandle());
        }

        ImGui::Begin("Stats");
        ImGui::Text("FPS: %.1f", deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f);
        ImGui::Text("Frame time: %.3f ms", deltaTime * 1000.0f);
        int maxFps = static_cast<int>(engine.GetMaxFps());
        if (ImGui::InputInt("Max FPS", &maxFps)) {
            engine.SetMaxFps(static_cast<uint32_t>(glm::max(maxFps, 0)));
        }
        ImGui::TextDisabled("0 disables the engine-side frame cap");
        bool showFps = engine.IsFpsVisible();
        if (ImGui::Checkbox("Show FPS in Play", &showFps)) {
            engine.SetFpsVisible(showFps);
        }
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

        m_RenderDebugPanel.Draw(engine.GetRHI(), deltaTime);

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

        ImGui::Begin("Post-Process");
        auto& postProcessSettings = engine.GetRHI()->GetPostProcessSettings();
        ImGui::SliderFloat("Vignette Intensity", &postProcessSettings.vignetteIntensity, 0.0f, 1.0f);
        ImGui::SliderFloat("Vignette Inner Radius", &postProcessSettings.vignetteInnerRadius, 0.0f, 1.0f);
        ImGui::SliderFloat("Vignette Outer Radius", &postProcessSettings.vignetteOuterRadius, 0.0f, 1.0f);
        ImGui::Separator();
        ImGui::SliderFloat("Bloom Intensity", &postProcessSettings.bloomIntensity, 0.0f, 1.0f);
        ImGui::SliderFloat("Bloom Threshold", &postProcessSettings.bloomThreshold, 0.0f, 1.0f);
        ImGui::SliderFloat("Bloom Radius", &postProcessSettings.bloomRadius, 0.5f, 16.0f, "%.1f px");
        ImGui::Separator();
        ImGui::SliderFloat("Chromatic Aberration", &postProcessSettings.chromaticAberrationIntensity, 0.0f, 1.0f);
        ImGui::SliderFloat("Film Grain", &postProcessSettings.filmGrainIntensity, 0.0f, 1.0f);
        ImGui::TextDisabled("Always applied in Play. In Edit mode, see Render Debugger's\nTexture Viewer, \"Post-Process Preview\".");
        ImGui::End();

        m_SceneInspector.DrawHierarchy(scene, engine.GetPhysics(), engine.GetAudio(), engine.GetScripting());
        m_SceneInspector.DrawProperties(scene, engine.GetPhysics(), engine.GetAudio(), engine.GetScripting());
    }
}
