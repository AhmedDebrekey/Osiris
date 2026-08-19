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
        // Draws drawFn's fields inside a CollapsingHeader plus a "Remove" button, but only if the
        // entity actually has component T. Each component type gets one call here — adding a new
        // one to the panel means adding a line, not touching a hardcoded switch/if-chain. Returns
        // true the one frame "Remove" is clicked; the caller (not this helper) is responsible for
        // actually removing the component, since some component types need backend cleanup first
        // (see DrawComponents' RigidBody/AudioSource/Script call sites).
        template<typename T, typename DrawFn>
        bool DrawComponentSection(Entity entity, const char* label, DrawFn&& drawFn) {
            if (!entity.HasComponent<T>()) return false;
            ImGui::PushID(label);

            // AllowOverlap is required — CollapsingHeader's hit-rect otherwise spans the entire
            // row width, so the Remove button drawn via SameLine() on the same row never
            // receives the click; the header just toggles instead. (Renamed from
            // ImGuiTreeNodeFlags_AllowItemOverlap in ImGui 1.89.7 — this repo tracks the docking
            // branch, which is past that rename.)
            bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
            ImGui::SameLine();
            bool remove = ImGui::SmallButton("Remove");

            if (open) {
                drawFn(entity.GetComponent<T>());
            }
            ImGui::PopID();
            return remove;
        }
    }

    void SceneInspectorPanel::Draw(Scene& scene, IPhysics* physics, IAudio* audio, IScripting* scripting) {
        ImGui::Begin("Scene Inspector");

        DrawEntityList(scene);
        ImGui::Separator();

        if (m_SelectedEntity != entt::null) {
            Entity entity(m_SelectedEntity, &scene);
            if (entity.IsValid() && entity.HasComponent<TagComponent>()) {
                DrawComponents(entity, physics, audio, scripting);
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

    void SceneInspectorPanel::DrawComponents(Entity entity, IPhysics* physics, IAudio* audio, IScripting* scripting) {
        // Tag/Transform aren't offered Remove — Scene::CreateEntity guarantees every entity has
        // both, and other code leans on that (e.g. DrawEntityList's Tag lookup, Scene::Render's
        // Transform+Mesh+Material view). DrawComponentSection's return value is ignored here.
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

        if (DrawComponentSection<ColliderComponent>(entity, "Collider", [](ColliderComponent& collider) {
            ImGui::DragFloat3("Half Extents", &collider.halfExtents.x, 0.05f, 0.01f, 100.0f);
            ImGui::TextDisabled("Box only. Physics bodies are created once at scene setup —\nediting this here doesn't resize the live Jolt body.");
        })) {
            entity.RemoveComponent<ColliderComponent>();
        }

        if (DrawComponentSection<RigidBodyComponent>(entity, "Rigid Body", [](RigidBodyComponent& rigidBody) {
            const char* motionTypeNames[] = { "Static", "Kinematic", "Dynamic" };
            int motionType = static_cast<int>(rigidBody.motionType);
            if (ImGui::Combo("Motion Type", &motionType, motionTypeNames, 3)) {
                rigidBody.motionType = static_cast<BodyMotionType>(motionType);
            }
            ImGui::Text("Body handle: %s", rigidBody.bodyHandle.IsValid() ? "valid" : "invalid");
            ImGui::TextDisabled("Physics bodies are created once at scene setup — changing\nmotion type here doesn't re-create the live Jolt body.");
        })) {
            // The component only owns a live Jolt body if it made it through
            // Scene::CreatePhysicsBodies — one added via "Add Component" mid-session never did.
            PhysicsBodyHandle bodyHandle = entity.GetComponent<RigidBodyComponent>().bodyHandle;
            if (bodyHandle.IsValid()) physics->DestroyBody(bodyHandle);
            entity.RemoveComponent<RigidBodyComponent>();
        }

        if (DrawComponentSection<AudioSourceComponent>(entity, "Audio Source", [](AudioSourceComponent& audioSrc) {
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

            // Tag/Transform are added automatically by Scene::CreateEntity, so every entity
            // already has them. Mesh/Material aren't offered — they need real GPU resources
            // (asset loading), not a sensible default a button click could construct.
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
                "Collider/Rigid Body/Audio Source/Script only spawn a live\n"
                "Jolt body/OpenAL source/script instance if present before\n"
                "Scene::CreateX runs at scene setup — adding one here\n"
                "mid-session updates the component data only.");

            ImGui::EndPopup();
        }
    }
}
