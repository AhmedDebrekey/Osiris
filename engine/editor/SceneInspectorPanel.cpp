#include "SceneInspectorPanel.h"

#include "assets/AudioLoader.h"
#include "core/AssetManager.h"
#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "physics/IPhysics.h"
#include "audio/IAudio.h"
#include "scripting/IScripting.h"

#include <imgui.h>
#include <cstring>
#include <limits>
#include <SDL2/SDL_keyboard.h>

namespace Osiris {
    namespace {
        constexpr const char* kSceneEntityPayload = "OSIRIS_SCENE_ENTITY";
        // Must match AssetBrowserPanel.cpp's kScriptAssetPayload/kAudioAssetPayload: the source
        // (asset rows) and target (entity rows here) live in different files with no shared header.
        constexpr const char* kScriptAssetPayload = "OSIRIS_SCRIPT_ASSET";
        constexpr const char* kAudioAssetPayload = "OSIRIS_AUDIO_ASSET";

        bool FitColliderToMeshHierarchy(Entity entity, ColliderComponent& collider) {
            Scene* scene = entity.GetScene();
            const glm::mat4 toEntityLocal = glm::inverse(scene->GetWorldTransform(entity));
            glm::vec3 boundsMin(std::numeric_limits<float>::max());
            glm::vec3 boundsMax(-std::numeric_limits<float>::max());
            bool foundMesh = false;

            std::vector<Entity> pending = {entity};
            for (std::size_t i = 0; i < pending.size(); i++) {
                Entity candidate = pending[i];
                if (candidate.HasComponent<MeshComponent>()) {
                    const AABB& bounds = candidate.GetComponent<MeshComponent>().mesh.bounds;
                    const glm::mat4 meshToEntity = toEntityLocal * scene->GetWorldTransform(candidate);
                    for (const glm::vec3& localCorner : bounds.GetWorldCorners(meshToEntity)) {
                        boundsMin = glm::min(boundsMin, localCorner);
                        boundsMax = glm::max(boundsMax, localCorner);
                    }
                    foundMesh = true;
                }

                std::vector<Entity> children = scene->GetChildren(candidate);
                pending.insert(pending.end(), children.begin(), children.end());
            }

            if (!foundMesh) return false;
            collider.center = (boundsMin + boundsMax) * 0.5f;
            collider.halfExtents = glm::max((boundsMax - boundsMin) * 0.5f, glm::vec3(0.01f));
            return true;
        }

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

    void SceneInspectorPanel::DrawHierarchy(Scene& scene, IPhysics* physics, IAudio* audio, IScripting* scripting) {
        ImGui::Begin("Scene Hierarchy");

        std::vector<Entity> entities = scene.GetAllEntities();
        ImGui::Text("Entities: %d", static_cast<int>(entities.size()));
        if (ImGui::Button("+ New Entity", ImVec2(-1.0f, 0.0f))) {
            SetSelectedEntity(scene.CreateEntity("New Entity").GetHandle());
        }

        ImGui::BeginChild("EntityList", ImVec2(0.0f, 0.0f), true);

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
                DrawEntityNode(scene, entity, pendingDelete, audio, scripting);
            }
        }

        // NoOpenOverItems: right-clicking an actual entity row still opens that row's own
        // EntityContextMenu (set up in DrawEntityNode), not this empty-space one.
        if (ImGui::BeginPopupContextWindow("HierarchyEmptyContext",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Add Entity")) {
                SetSelectedEntity(scene.CreateEntity("New Entity").GetHandle());
            }
            ImGui::EndPopup();
        }

        ImGui::EndChild();

        if (pendingDelete != entt::null) {
            scene.DestroyEntity(Entity(pendingDelete, &scene), physics, audio, scripting);
        }

        ImGui::End();
    }

    void SceneInspectorPanel::DrawEntityNode(
        Scene& scene, Entity entity, entt::entity& pendingDelete, IAudio* audio, IScripting* scripting) {
        const std::vector<Entity> children = scene.GetChildren(entity);
        const std::string& name = entity.GetComponent<TagComponent>().name;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (entity.GetHandle() == m_SelectedEntity) flags |= ImGuiTreeNodeFlags_Selected;
        if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        ImGui::PushID(static_cast<int>(static_cast<uint32_t>(entity.GetHandle())));
        const bool open = ImGui::TreeNodeEx("##Entity", flags, "%s", name.c_str());
        if (ImGui::IsItemClicked()) {
            SetSelectedEntity(entity.GetHandle());
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
            HandleAssetDrop(entity, audio, scripting);
            ImGui::EndDragDropTarget();
        }

        if (open && !children.empty()) {
            for (Entity child : children) {
                DrawEntityNode(scene, child, pendingDelete, audio, scripting);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void SceneInspectorPanel::HandleAssetDrop(Entity entity, IAudio* audio, IScripting* scripting) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kScriptAssetPayload)) {
            // ScriptComponent stores the same AssetManager-relative path the catalog provides.
            // Scene::CreateScriptInstances resolves it only when the file is actually loaded.
            const std::string scriptPath = static_cast<const char*>(payload->Data);
            // Only one ScriptComponent fits per entity (entt allows one of each type), so a drop
            // on an already-scripted entity replaces it rather than being rejected: tear down the
            // live instance first, same as the Remove button does in DrawComponents below.
            if (entity.HasComponent<ScriptComponent>()) {
                auto& script = entity.GetComponent<ScriptComponent>();
                if (script.instanceHandle.IsValid()) scripting->DestroyInstance(script.instanceHandle);
                script.scriptPath = scriptPath;
                script.instanceHandle = ScriptInstanceHandle{};
            } else {
                entity.AddComponent<ScriptComponent>().scriptPath = scriptPath;
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAudioAssetPayload)) {
            // Unlike scripts, AudioSourceComponent::clipPath is stored AssetManager-relative
            // (matching SceneLoader.cpp's own read/write convention: it resolves clipPath through
            // AssetManager::GetPath itself at load time). Resolving it here too, the way the
            // script branch above does, would double-prefix it on the next Save/Load.
            const std::string relativePath = static_cast<const char*>(payload->Data);
            PCMAudioData pcm = AudioLoader::LoadWAV(AssetManager::GetPath(relativePath));
            if (!pcm.pcmData.empty()) {
                // One AudioSourceComponent per entity, same reasoning as Script above: a drop on
                // an already-sourced entity replaces its clip rather than being rejected.
                // RebuildAudioSource (destroy-if-valid, then recreate from the current fields)
                // picks up the new clip immediately, same path the Inspector's own field edits use.
                if (!entity.HasComponent<AudioSourceComponent>()) entity.AddComponent<AudioSourceComponent>();
                auto& audioSrc = entity.GetComponent<AudioSourceComponent>();
                audioSrc.clip = audio->CreateBuffer(pcm);
                audioSrc.clipPath = relativePath;
                entity.GetScene()->RebuildAudioSource(entity, audio);
            }
        }
    }

    void SceneInspectorPanel::DrawProperties(Scene& scene, IPhysics* physics, IAudio* audio, IScripting* scripting) {
        ImGui::Begin("Details");

        // Called right after Begin(), before any other widget, so the whole window (not just
        // some specific widget) counts as the drop target: drag a script/clip from the Asset
        // Browser anywhere onto this panel to attach it to the selected entity, same as dropping
        // it on that entity's row in the Scene Hierarchy panel.
        if (m_SelectedEntity != entt::null && ImGui::BeginDragDropTarget()) {
            HandleAssetDrop(Entity(m_SelectedEntity, &scene), audio, scripting);
            ImGui::EndDragDropTarget();
        }

        if (m_SelectedEntity != entt::null) {
            Entity entity(m_SelectedEntity, &scene);
            if (entity.IsValid() && entity.HasComponent<TagComponent>()) {
                DrawComponents(entity, physics, audio, scripting);
            } else {
                ClearSelection(); // entity no longer exists
            }
        } else {
            ImGui::TextDisabled("Select an entity in the Scene Hierarchy to inspect it.");
        }

        ImGui::End();
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

        if (DrawComponentSection<EmissiveComponent>(entity, "Emissive", [](EmissiveComponent& emissive) {
            ImGui::ColorEdit3("Color", &emissive.color.x);
            ImGui::DragFloat("Intensity", &emissive.intensity, 0.05f, 0.0f, 100.0f);
            ImGui::TextDisabled(
                "Adds self-lit color without creating a light. On a model root,\n"
                "the setting is inherited by all mesh children and drives Bloom.");
        })) {
            entity.RemoveComponent<EmissiveComponent>();
        }

        if (DrawComponentSection<ModelSourceComponent>(entity, "Model Source", [](ModelSourceComponent& source) {
            ImGui::Text("Path: %s", source.relativePath.c_str());
            ImGui::TextDisabled("Read-only — set once by Scene::SpawnModel. This is what\nlets Save Scene write a \"mesh\" path back out for this entity.");
        })) {
            entity.RemoveComponent<ModelSourceComponent>();
        }

        if (DrawComponentSection<BoxSourceComponent>(entity, "Box Source", [](BoxSourceComponent& source) {
            ImGui::Text("Half extents: %.3f, %.3f, %.3f",
                source.halfExtents.x, source.halfExtents.y, source.halfExtents.z);
            ImGui::Text("Texture: %s", source.texturePath.empty() ? "<default>" : source.texturePath.c_str());
            ImGui::TextDisabled("Read-only source metadata used by Save Scene. Recreate the box\nto change its generated mesh or material.");
        })) {
            entity.RemoveComponent<BoxSourceComponent>();
        }

        if (DrawComponentSection<InteractableComponent>(entity, "Interactable", [](InteractableComponent& interactable) {
            char buffer[256] = {};
            strncpy_s(buffer, interactable.prompt.c_str(), sizeof(buffer) - 1);
            if (ImGui::InputText("Prompt", buffer, sizeof(buffer))) {
                interactable.prompt = buffer;
            }
            ImGui::DragFloat("Max Distance", &interactable.maxDistance, 0.1f, 0.1f, 100.0f);
            int keyCode = static_cast<int>(interactable.keyCode);
            if (ImGui::InputInt("Key Code (SDL Scancode)", &keyCode)) {
                // Input::IsKeyPressed/IsKeyHeld index a fixed-size array with this value, no
                // bounds check of their own — clamp here so a stray value typed into this field
                // can't turn into an out-of-bounds read every frame during Play.
                if (keyCode < 0) keyCode = 0;
                if (keyCode >= SDL_NUM_SCANCODES) keyCode = SDL_NUM_SCANCODES - 1;
                interactable.keyCode = static_cast<SDL_Scancode>(keyCode);
            }
            ImGui::TextDisabled("%s", SDL_GetScancodeName(interactable.keyCode));
            ImGui::TextDisabled("This is what shows when looking at this entity");
        })) {
            entity.RemoveComponent<InteractableComponent>();
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

        bool colliderEditFinished = false;
        if (DrawComponentSection<ColliderComponent>(entity, "Collider", [&](ColliderComponent& collider) {
            bool editInViewport = m_ColliderEditEntity == entity.GetHandle();
            if (ImGui::Checkbox("Edit in Viewport", &editInViewport)) {
                m_ColliderEditEntity = editInViewport ? entity.GetHandle() : entt::null;
            }
            if (ImGui::Button("Fit to Mesh Bounds", ImVec2(-1.0f, 0.0f))) {
                colliderEditFinished = FitColliderToMeshHierarchy(entity, collider);
            }
            ImGui::DragFloat3("Center", &collider.center.x, 0.05f);
            if (ImGui::IsItemDeactivatedAfterEdit()) colliderEditFinished = true;
            ImGui::DragFloat3("Half Extents", &collider.halfExtents.x, 0.05f, 0.01f, 100.0f);
            if (ImGui::IsItemDeactivatedAfterEdit()) colliderEditFinished = true;
            ImGui::TextDisabled("Box only. Changes rebuild the live Jolt body when editing finishes\nif a Rigid Body is also present.");
        })) {
            m_ColliderEditEntity = entt::null;
            if (entity.HasComponent<RigidBodyComponent>()) {
                auto& rigidBody = entity.GetComponent<RigidBodyComponent>();
                if (rigidBody.bodyHandle.IsValid()) physics->DestroyBody(rigidBody.bodyHandle);
                rigidBody.bodyHandle = PhysicsBodyHandle{};
            }
            entity.RemoveComponent<ColliderComponent>();
        } else if (colliderEditFinished) {
            entity.GetScene()->RebuildPhysicsBody(entity, physics);
        }

        bool rigidBodyChanged = false;
        if (DrawComponentSection<RigidBodyComponent>(entity, "Rigid Body", [&](RigidBodyComponent& rigidBody) {
            const char* motionTypeNames[] = { "Static", "Kinematic", "Dynamic" };
            int motionType = static_cast<int>(rigidBody.motionType);
            if (ImGui::Combo("Motion Type", &motionType, motionTypeNames, 3)) {
                rigidBody.motionType = static_cast<BodyMotionType>(motionType);
                rigidBodyChanged = true;
            }
            if (ImGui::Checkbox("Lock Rotation to Y Axis", &rigidBody.lockRotationToYAxis)) rigidBodyChanged = true;
            if (ImGui::Checkbox("Sensor", &rigidBody.isSensor)) rigidBodyChanged = true;
            ImGui::Text("Body handle: %s", rigidBody.bodyHandle.IsValid() ? "valid" : "invalid");
            ImGui::TextDisabled("Changing creation settings rebuilds the live Jolt body (destroy +\nrecreate). A Collider is required on this entity too.");
        })) {
            PhysicsBodyHandle bodyHandle = entity.GetComponent<RigidBodyComponent>().bodyHandle;
            if (bodyHandle.IsValid()) physics->DestroyBody(bodyHandle);
            entity.RemoveComponent<RigidBodyComponent>();
        } else if (rigidBodyChanged) {
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

        bool audioChanged = false;
        if (DrawComponentSection<AudioSourceComponent>(entity, "Audio Source", [&](AudioSourceComponent& audioSrc) {
            char clipPathBuffer[256] = {};
            strncpy_s(clipPathBuffer, audioSrc.clipPath.c_str(), sizeof(clipPathBuffer) - 1);
            if (ImGui::InputText("Clip Path", clipPathBuffer, sizeof(clipPathBuffer))) {
                audioSrc.clipPath = clipPathBuffer;
            }
            ImGui::TextDisabled("Just a label Save Scene writes out: editing it here doesn't\nload a clip. Drag a .wav from the Asset Browser onto this entity instead.");
            if (ImGui::DragFloat("Gain", &audioSrc.gain, 0.01f, 0.0f, 2.0f)) audioChanged = true;
            if (ImGui::DragFloat("Pitch", &audioSrc.pitch, 0.01f, 0.1f, 4.0f)) audioChanged = true;
            if (ImGui::Checkbox("Loop", &audioSrc.loop)) audioChanged = true;
            if (ImGui::Checkbox("Auto Play", &audioSrc.autoPlay)) audioChanged = true;
            if (ImGui::DragFloat("Reference Distance", &audioSrc.referenceDistance, 0.1f, 0.01f, 100.0f)) audioChanged = true;
            if (ImGui::DragFloat("Max Distance", &audioSrc.maxDistance, 0.5f, 0.1f, 500.0f)) audioChanged = true;
            if (ImGui::DragFloat("Rolloff Factor", &audioSrc.rolloffFactor, 0.05f, 0.0f, 10.0f)) audioChanged = true;
            ImGui::Text("Clip: %s", audioSrc.clip.IsValid() ? "valid" : "invalid");
            ImGui::Text("Source handle: %s", audioSrc.sourceHandle.IsValid() ? "valid" : "invalid");
            ImGui::TextDisabled("Editing these rebuilds the live OpenAL source (destroy +\nrecreate), same as Collider/Rigid Body/Character. Restarts\nplayback from the beginning if it was already playing.");
        })) {
            AudioSourceHandle sourceHandle = entity.GetComponent<AudioSourceComponent>().sourceHandle;
            if (sourceHandle.IsValid()) audio->DestroySource(sourceHandle);
            entity.RemoveComponent<AudioSourceComponent>();
        } else if (audioChanged) {
            entity.GetScene()->RebuildAudioSource(entity, audio);
        }

        if (DrawComponentSection<ScriptComponent>(entity, "Script", [](ScriptComponent& script) {
            char buffer[256] = {};
            strncpy_s(buffer, script.scriptPath.c_str(), sizeof(buffer) - 1);
            if (ImGui::InputText("Path", buffer, sizeof(buffer))) {
                script.scriptPath = AssetManager::GetRelativePath(buffer);
            }
            ImGui::Text("Instance: %s", script.instanceHandle.IsValid() ? "valid" : "invalid");
            ImGui::TextDisabled("Scripts are loaded once at scene setup — editing the path here\ndoesn't reload it. A newly-added Script component won't run\nuntil the next full scene setup.");
        })) {
            ScriptInstanceHandle instanceHandle = entity.GetComponent<ScriptComponent>().instanceHandle;
            if (instanceHandle.IsValid()) scripting->DestroyInstance(instanceHandle);
            entity.RemoveComponent<ScriptComponent>();
        }

        ImGui::Separator();
        DrawAddComponentButton(entity, physics);
    }

    void SceneInspectorPanel::DrawAddComponentButton(Entity entity, IPhysics* physics) {
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
            if (!entity.HasComponent<EmissiveComponent>()) {
                anyOffered = true;
                if (ImGui::MenuItem("Emissive")) entity.AddComponent<EmissiveComponent>();
            }
            if (!entity.HasComponent<ColliderComponent>()) {
                anyOffered = true;
                if (ImGui::MenuItem("Collider")) {
                    auto& collider = entity.AddComponent<ColliderComponent>();
                    FitColliderToMeshHierarchy(entity, collider);
                    if (entity.HasComponent<RigidBodyComponent>()) {
                        entity.GetScene()->RebuildPhysicsBody(entity, physics);
                    }
                }
            }
            if (!entity.HasComponent<RigidBodyComponent>()) {
                anyOffered = true;
                if (ImGui::MenuItem("Rigid Body")) {
                    entity.AddComponent<RigidBodyComponent>();
                    if (entity.HasComponent<ColliderComponent>()) {
                        entity.GetScene()->RebuildPhysicsBody(entity, physics);
                    }
                }
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

            if (!entity.HasComponent<InteractableComponent>()) {
                anyOffered = true;
                if (ImGui::MenuItem("Interactable")) entity.AddComponent<InteractableComponent>();
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
