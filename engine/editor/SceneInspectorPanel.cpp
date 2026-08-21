#include "SceneInspectorPanel.h"

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "physics/IPhysics.h"
#include "audio/IAudio.h"
#include "scripting/IScripting.h"

#include <imgui.h>
#include <cstring>

namespace Osiris {
    namespace {
        constexpr const char* kSceneEntityPayload = "OSIRIS_SCENE_ENTITY";

        // Draws drawFn's fields plus a "Remove" button, only if the entity has component T.
        // Returns true the frame "Remove" is clicked — caller does the actual removal, since
        // some component types need backend cleanup first.
        template<typename T, typename DrawFn>
        bool DrawComponentSection(Entity entity, const char* label, DrawFn&& drawFn) {
            if (!entity.HasComponent<T>()) return false;
            ImGui::PushID(label);

            // AllowOverlap: without it CollapsingHeader's hit-rect eats the Remove button's click.
            bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
            ImGui::SameLine();
            bool remove = ImGui::SmallButton("Remove");

            if (open) {
                drawFn(entity.GetComponent<T>());
            }
            ImGui::PopID();
            return remove;
        }

        template<typename DrawFn>
        void DrawReadOnlyComponentSection(const char* label, DrawFn&& drawFn) {
            ImGui::PushID(label);
            if (ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
                drawFn();
            }
            ImGui::PopID();
        }
    }

    void SceneInspectorPanel::Draw(Scene& scene, IPhysics* physics, IAudio* audio, IScripting* scripting) {
        ImGui::Begin("Scene Inspector");

        DrawEntityList(scene, physics, audio, scripting);
        ImGui::Separator();

        if (m_SelectedEntity != entt::null) {
            Entity entity(m_SelectedEntity, &scene);
            if (entity.IsValid() && entity.HasComponent<TagComponent>()) {
                // Guarded by IsAnyItemActive so Del deletes the entity, not a character out of
                // whatever text field (e.g. the Tag Name box) happens to be focused.
                const bool deletePressed = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
                    && !ImGui::IsAnyItemActive()
                    && ImGui::IsKeyPressed(ImGuiKey_Delete);
                if (deletePressed) {
                    scene.DestroyEntity(entity, physics, audio, scripting);
                    m_SelectedEntity = entt::null;
                } else {
                    DrawComponents(entity, physics, audio, scripting);
                }
            } else {
                m_SelectedEntity = entt::null; // entity no longer exists
            }
        } else {
            ImGui::TextDisabled("Select an entity above to inspect it.");
        }

        ImGui::End();
    }

    void SceneInspectorPanel::DrawEntityList(Scene& scene, IPhysics* physics, IAudio* audio, IScripting* scripting) {
        std::vector<Entity> entities = scene.GetAllEntities();
        ImGui::Text("Entities: %d", static_cast<int>(entities.size()));

        ImGui::BeginChild("EntityList", ImVec2(0.0f, 150.0f), true);

        ImGui::Button("Drop here to make root", ImVec2(-1.0f, 0.0f));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneEntityPayload)) {
                if (payload->DataSize == sizeof(entt::entity)) {
                    const entt::entity draggedHandle = *static_cast<const entt::entity*>(payload->Data);
                    scene.SetParent(Entity(draggedHandle, &scene), Entity{});
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Collected rather than destroyed on the spot — DestroyEntity (which cascades to
        // children) must run after the tree walk below finishes, not while it's still reading
        // ChildrenComponent/GetAllEntities' snapshot.
        entt::entity pendingDelete = entt::null;
        for (Entity entity : entities) {
            if (!entity.HasComponent<ParentComponent>() || !scene.GetParent(entity).IsValid()) {
                DrawEntityNode(scene, entity, pendingDelete);
            }
        }
        ImGui::EndChild();

        if (pendingDelete != entt::null) {
            scene.DestroyEntity(Entity(pendingDelete, &scene), physics, audio, scripting);
        }
    }

    void SceneInspectorPanel::DrawEntityNode(Scene& scene, Entity entity, entt::entity& pendingDelete) {
        const std::vector<Entity> children = scene.GetChildren(entity);
        const std::string& name = entity.GetComponent<TagComponent>().name;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (entity.GetHandle() == m_SelectedEntity) flags |= ImGuiTreeNodeFlags_Selected;
        if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        ImGui::PushID(static_cast<int>(static_cast<uint32_t>(entity.GetHandle())));
        const bool open = ImGui::TreeNodeEx("##Entity", flags, "%s", name.c_str());
        if (ImGui::IsItemClicked()) {
            m_SelectedEntity = entity.GetHandle();
        }

        if (ImGui::BeginPopupContextItem("EntityContextMenu")) {
            if (!children.empty()) {
                ImGui::TextDisabled("Deletes %d child%s too.",
                    static_cast<int>(children.size()), children.size() == 1 ? "" : "ren");
            }
            if (ImGui::MenuItem("Delete")) {
                pendingDelete = entity.GetHandle();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginDragDropSource()) {
            const entt::entity handle = entity.GetHandle();
            ImGui::SetDragDropPayload(kSceneEntityPayload, &handle, sizeof(handle));
            ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSceneEntityPayload)) {
                if (payload->DataSize == sizeof(entt::entity)) {
                    const entt::entity draggedHandle = *static_cast<const entt::entity*>(payload->Data);
                    scene.SetParent(Entity(draggedHandle, &scene), entity);
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (open && !children.empty()) {
            for (Entity child : children) {
                DrawEntityNode(scene, child, pendingDelete);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void SceneInspectorPanel::DrawComponents(Entity entity, IPhysics* physics, IAudio* audio, IScripting* scripting) {
        // Tag/Transform aren't offered Remove — every entity is guaranteed to have both.
        DrawComponentSection<TagComponent>(entity, "Tag", [](TagComponent& tag) {
            char buffer[256] = {};
            strncpy_s(buffer, tag.name.c_str(), sizeof(buffer) - 1);
            if (ImGui::InputText("Name", buffer, sizeof(buffer))) {
                tag.name = buffer;
            }
        });

        DrawComponentSection<TransformComponent>(entity, "Transform", [&](TransformComponent& transform) {
            ImGui::DragFloat3("Position", &transform.position.x, 0.05f);
            ImGui::DragFloat3("Rotation", &transform.rotation.x, 0.5f);
            ImGui::DragFloat3("Scale", &transform.scale.x, 0.05f, 0.001f, 1000.0f);
            if (entity.HasComponent<ParentComponent>()) {
                ImGui::TextDisabled("Local to parent.");
            }
        });

        if (entity.HasComponent<ParentComponent>()) {
            DrawReadOnlyComponentSection("Parent", [&] {
                Entity parent = entity.GetScene()->GetParent(entity);
                if (parent.IsValid() && parent.HasComponent<TagComponent>()) {
                    ImGui::Text("Entity: %s", parent.GetComponent<TagComponent>().name.c_str());
                } else {
                    ImGui::TextDisabled("Parent handle is invalid.");
                }
                ImGui::TextDisabled("Read-only — reparent through the entity tree.");
            });
        }

        if (entity.HasComponent<ChildrenComponent>()) {
            DrawReadOnlyComponentSection("Children", [&] {
                std::vector<Entity> children = entity.GetScene()->GetChildren(entity);
                ImGui::Text("Count: %d", static_cast<int>(children.size()));
                for (Entity child : children) {
                    ImGui::BulletText("%s", child.GetComponent<TagComponent>().name.c_str());
                }
                ImGui::TextDisabled("Read-only — reparent through the entity tree.");
            });
        }

        if (DrawComponentSection<MeshComponent>(entity, "Mesh", [](MeshComponent& mesh) {
            ImGui::Text("Vertices: %u", mesh.mesh.vertexCount);
            ImGui::Text("Indices: %u", mesh.mesh.indexCount);
            ImGui::TextDisabled("Read-only — GPU mesh data.");
        })) {
            entity.RemoveComponent<MeshComponent>();
        }

        if (DrawComponentSection<MaterialComponent>(entity, "Material", [](MaterialComponent& material) {
            ImGui::Text("Material handle: %u", material.material.id);
            ImGui::TextDisabled("Read-only — texture editing isn't supported yet.");
        })) {
            entity.RemoveComponent<MaterialComponent>();
        }

        if (DrawComponentSection<ModelSourceComponent>(entity, "Model Source", [](ModelSourceComponent& source) {
            ImGui::Text("Path: %s", source.relativePath.c_str());
            ImGui::TextDisabled("Read-only — set once by Scene::SpawnModel. This is what\nlets Save Scene write a \"mesh\" path back out for this entity.");
        })) {
            entity.RemoveComponent<ModelSourceComponent>();
        }

        if (DrawComponentSection<SpotLightComponent>(entity, "Spot Light", [](SpotLightComponent& light) {
            ImGui::ColorEdit3("Color", &light.color.x);
            ImGui::DragFloat("Intensity", &light.intensity, 0.1f, 0.0f, 100.0f);
            ImGui::DragFloat("Inner Cone", &light.innerCone, 0.5f, 1.0f, 89.0f);
            ImGui::DragFloat("Outer Cone", &light.outerCone, 0.5f, 1.0f, 89.0f);
            if (light.outerCone < light.innerCone) light.outerCone = light.innerCone;
            ImGui::DragFloat("Range", &light.range, 0.1f, 0.1f, 100.0f);
            ImGui::Checkbox("Enabled", &light.enabled);
            ImGui::Checkbox("Casts Shadow", &light.castsShadow);
        })) {
            entity.RemoveComponent<SpotLightComponent>();
        }

        bool colliderChanged = false;
        if (DrawComponentSection<ColliderComponent>(entity, "Collider", [&](ColliderComponent& collider) {
            if (ImGui::DragFloat3("Half Extents", &collider.halfExtents.x, 0.05f, 0.01f, 100.0f)) {
                colliderChanged = true;
            }
            ImGui::TextDisabled("Box only. Editing this rebuilds the live Jolt body (destroy +\nrecreate) if a Rigid Body is also present.");
        })) {
            entity.RemoveComponent<ColliderComponent>();
        } else if (colliderChanged) {
            entity.GetScene()->RebuildPhysicsBody(entity, physics);
        }

        bool motionTypeChanged = false;
        if (DrawComponentSection<RigidBodyComponent>(entity, "Rigid Body", [&](RigidBodyComponent& rigidBody) {
            const char* motionTypeNames[] = { "Static", "Kinematic", "Dynamic" };
            int motionType = static_cast<int>(rigidBody.motionType);
            if (ImGui::Combo("Motion Type", &motionType, motionTypeNames, 3)) {
                rigidBody.motionType = static_cast<BodyMotionType>(motionType);
                motionTypeChanged = true;
            }
            ImGui::Text("Body handle: %s", rigidBody.bodyHandle.IsValid() ? "valid" : "invalid");
            ImGui::TextDisabled("Changing motion type rebuilds the live Jolt body (destroy +\nrecreate) — needs a Collider on this entity too.");
        })) {
            PhysicsBodyHandle bodyHandle = entity.GetComponent<RigidBodyComponent>().bodyHandle;
            if (bodyHandle.IsValid()) physics->DestroyBody(bodyHandle);
            entity.RemoveComponent<RigidBodyComponent>();
        } else if (motionTypeChanged) {
            entity.GetScene()->RebuildPhysicsBody(entity, physics);
        }

        if (DrawComponentSection<CameraComponent>(entity, "Camera", [](CameraComponent& cam) {
            ImGui::DragFloat("Eye Height", &cam.eyeHeight, 0.05f, 0.0f, 5.0f);
            ImGui::Checkbox("Primary", &cam.isPrimary);
            ImGui::TextDisabled("Play mode's camera follows the primary Camera entity\n(Scene::FindCameraEntity) — position + eyeHeight above it.\nIf more than one is marked primary, first-found wins.");
        })) {
            entity.RemoveComponent<CameraComponent>();
        }

        bool characterChanged = false;
        if (DrawComponentSection<CharacterComponent>(entity, "Character", [&](CharacterComponent& character) {
            if (ImGui::DragFloat("Radius", &character.radius, 0.01f, 0.05f, 2.0f)) characterChanged = true;
            if (ImGui::DragFloat("Height", &character.height, 0.05f, 0.1f, 5.0f)) characterChanged = true;
            if (ImGui::DragFloat("Max Slope Angle", &character.maxSlopeAngleDeg, 0.5f, 0.0f, 89.0f)) characterChanged = true;
            if (ImGui::DragFloat("Mass", &character.mass, 1.0f, 1.0f, 500.0f)) characterChanged = true;
            ImGui::Text("Character handle: %s", character.characterHandle.IsValid() ? "valid" : "invalid");
            ImGui::TextDisabled("Editing these rebuilds the live Jolt character (destroy +\nrecreate), same as Collider/Rigid Body.");
        })) {
            CharacterHandle characterHandle = entity.GetComponent<CharacterComponent>().characterHandle;
            if (characterHandle.IsValid()) physics->DestroyCharacter(characterHandle);
            entity.RemoveComponent<CharacterComponent>();
        } else if (characterChanged) {
            entity.GetScene()->RebuildCharacter(entity, physics);
        }

        if (DrawComponentSection<AudioSourceComponent>(entity, "Audio Source", [](AudioSourceComponent& audioSrc) {
            char clipPathBuffer[256] = {};
            strncpy_s(clipPathBuffer, audioSrc.clipPath.c_str(), sizeof(clipPathBuffer) - 1);
            if (ImGui::InputText("Clip Path", clipPathBuffer, sizeof(clipPathBuffer))) {
                audioSrc.clipPath = clipPathBuffer;
            }
            ImGui::TextDisabled("Just a label Save Scene writes out — editing it here doesn't\nreload the clip. Set clip/sourceHandle via a fresh scene setup.");
            ImGui::DragFloat("Gain", &audioSrc.gain, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("Pitch", &audioSrc.pitch, 0.01f, 0.1f, 4.0f);
            ImGui::Checkbox("Loop", &audioSrc.loop);
            ImGui::Checkbox("Auto Play", &audioSrc.autoPlay);
            ImGui::DragFloat("Reference Distance", &audioSrc.referenceDistance, 0.1f, 0.01f, 100.0f);
            ImGui::DragFloat("Max Distance", &audioSrc.maxDistance, 0.5f, 0.1f, 500.0f);
            ImGui::DragFloat("Rolloff Factor", &audioSrc.rolloffFactor, 0.05f, 0.0f, 10.0f);
            ImGui::Text("Clip: %s", audioSrc.clip.IsValid() ? "valid" : "invalid");
            ImGui::Text("Source handle: %s", audioSrc.sourceHandle.IsValid() ? "valid" : "invalid");
            ImGui::TextDisabled("Audio sources are created once at scene setup — editing fields\nhere doesn't push changes to the live OpenAL source.");
        })) {
            AudioSourceHandle sourceHandle = entity.GetComponent<AudioSourceComponent>().sourceHandle;
            if (sourceHandle.IsValid()) audio->DestroySource(sourceHandle);
            entity.RemoveComponent<AudioSourceComponent>();
        }

        if (DrawComponentSection<ScriptComponent>(entity, "Script", [](ScriptComponent& script) {
            char buffer[256] = {};
            strncpy_s(buffer, script.scriptPath.c_str(), sizeof(buffer) - 1);
            if (ImGui::InputText("Path", buffer, sizeof(buffer))) {
                script.scriptPath = buffer;
            }
            ImGui::Text("Instance: %s", script.instanceHandle.IsValid() ? "valid" : "invalid");
            ImGui::TextDisabled("Scripts are loaded once at scene setup — editing the path here\ndoesn't reload it. A newly-added Script component won't run\nuntil the next full scene setup.");
        })) {
            ScriptInstanceHandle instanceHandle = entity.GetComponent<ScriptComponent>().instanceHandle;
            if (instanceHandle.IsValid()) scripting->DestroyInstance(instanceHandle);
            entity.RemoveComponent<ScriptComponent>();
        }

        ImGui::Separator();
        DrawAddComponentButton(entity);
    }

    void SceneInspectorPanel::DrawAddComponentButton(Entity entity) {
        if (ImGui::Button("+ Add Component", ImVec2(-1, 0))) {
            ImGui::OpenPopup("AddComponentPopup");
        }

        if (ImGui::BeginPopup("AddComponentPopup")) {
            bool anyOffered = false;

            // Tag/Transform always present. Mesh/Material need real GPU resources, not offered.
            if (!entity.HasComponent<SpotLightComponent>()) {
                anyOffered = true;
                if (ImGui::MenuItem("Spot Light")) entity.AddComponent<SpotLightComponent>();
            }
            if (!entity.HasComponent<ColliderComponent>()) {
                anyOffered = true;
                if (ImGui::MenuItem("Collider")) entity.AddComponent<ColliderComponent>();
            }
            if (!entity.HasComponent<RigidBodyComponent>()) {
                anyOffered = true;
                if (ImGui::MenuItem("Rigid Body")) entity.AddComponent<RigidBodyComponent>();
            }
            if (!entity.HasComponent<CameraComponent>()) {
                anyOffered = true;
                if (ImGui::MenuItem("Camera")) entity.AddComponent<CameraComponent>();
            }
            if (!entity.HasComponent<CharacterComponent>()) {
                anyOffered = true;
                if (ImGui::MenuItem("Character")) entity.AddComponent<CharacterComponent>();
            }
            if (!entity.HasComponent<AudioSourceComponent>()) {
                anyOffered = true;
                if (ImGui::MenuItem("Audio Source")) entity.AddComponent<AudioSourceComponent>();
            }
            if (!entity.HasComponent<ScriptComponent>()) {
                anyOffered = true;
                if (ImGui::MenuItem("Script")) entity.AddComponent<ScriptComponent>();
            }

            if (!anyOffered) {
                ImGui::TextDisabled("All addable components are already on this entity.");
            }

            ImGui::Separator();
            ImGui::TextDisabled(
                "Collider + Rigid Body get a live body once both are present\n"
                "and edited. Character/Audio Source/Script need a fresh\n"
                "scene setup pass. Camera is just a marker, no setup needed.");

            ImGui::EndPopup();
        }
    }
}
