# Osiris Engine — Development Roadmap

Custom C++23 / Vulkan 1.4 game engine, built from scratch for a personal horror game.
Solo developer, Windows + CLion, CMake `FetchContent`.

| | |
|---|---|
| **Repo** | `github.com/AhmedDebrekey/Osiris` |
| **Local** | `C:\Dev\Osiris` |
| **GPU** | RTX 4060 |

This document is the authoritative phase plan. Status: ✅ done · 🔶 partial · ⬜ not started.

---

## Tech stack (locked)

| Category | Choices |
|---|---|
| Toolchain | CLion + MSVC, CMake 3.25+ (`FetchContent`), C++23, Git/GitHub (GPL v3) |
| Graphics | Vulkan 1.4 SDK (1.4.350.0), VMA, dynamic rendering (no `VkRenderPass`) |
| Platform / math | SDL2, GLM |
| Engine | EnTT (ECS) |
| Physics / audio / scripting | Jolt Physics *(integrated, Phase 7A)* · OpenAL Soft *(integrated, Phase 7B)* · Lua + Sol3 *(integrated, Phase 7D)* |
| Assets | fastgltf, ufbx, stb_image, nlohmann/json |
| Tools | Dear ImGui (docking branch), spdlog, Tracy *(not yet integrated)* |

Rendering target: Forward+ (planned). Currently forward. Y-up, right-handed, column-major, 1 unit = 1 meter.

---

## Architecture

```
Core → Platform → RHI → Renderer → ECS/Scene → Game
```

| Directory | Contents |
|---|---|
| `engine/core/` | `Engine`, `Log`, `AssetManager` |
| `engine/platform/` | `Window`, `Input` |
| `engine/rhi/` | `IRHI` interface, `RHITypes.h`, `CommandBuffer.h` (partial, Phase 5D) |
| `engine/rhi_vulkan/` | `VulkanRHI`, `VulkanTypes.h`, `PipelineManager` |
| `engine/renderer/` | `Camera`, `RenderGraph`, `Frustum`, `MeshType.h`, `Light.h` |
| `engine/scene/` | `Scene`, `Entity`, `Components.h` (EnTT-based ECS) |
| `engine/assets/` | `MeshLoader`, `TextureLoader`, `SceneLoader`, `AudioLoader` |
| `engine/physics/` | `IPhysics` interface, `PhysicsTypes.h`; `physics/jolt/JoltPhysics` is the only backend |
| `engine/audio/` | `IAudio` interface, `AudioTypes.h`; `audio/openal/OpenALAudio` is the only backend |
| `engine/scripting/` | `IScripting` interface, `ScriptTypes.h`, `ScriptTemplate`; `scripting/lua/LuaScripting` (sol2) is the only backend |
| `engine/editor/` | `SceneInspectorPanel` — ImGui entity list + per-component editor |
| `assets/shaders/` | `triangle.vert/frag`, `shadow.vert`, `skybox.vert/frag`, `brdf_lut.comp`, `equirect_to_cubemap.comp`, `irradiance_convolve.comp`, `prefilter_env.comp` |
| `assets/scenes/` | JSON scene files |

> **Iron rule:** no raw Vulkan calls outside `rhi_vulkan/`.
> **Handles:** `BufferHandle`, `TextureHandle`, `MaterialHandle`, `PipelineHandle` — all `Handle<Tag>` templates, `INVALID_HANDLE_ID = UINT32_MAX`, backed by a generic `AllocateSlot<T>()` slot map (not per-type allocators).
> **Pipelines:** always through `PipelineManager::GetOrCreate(PipelineDesc)` (graphics) or `GetOrCreateCompute(ComputePipelineDesc)` (compute) — hashed caches, never call `vkCreateGraphicsPipelines`/`vkCreateComputePipelines` directly. `PipelineDesc.vertexInput = false` covers bufferless fullscreen-triangle/hardcoded-geometry passes (skybox today; reusable for bloom/SSAO).

**Descriptor set 0 (frame data):**

| Binding | Contents |
|---|---|
| 0 | Camera UBO |
| 1–3 | Directional cascade shadow samplers |
| 4 | Spot shadow sampler array[3] |
| 5 | Spot light UBO |
| 6–8 | IBL environment / irradiance / prefiltered cubemaps |
| 9 | IBL BRDF LUT |

**Descriptor set 1 (material data):** albedo, normal, metallic, roughness, AO (no emissive slot yet).

---

## Phase 0 — Foundation ✅
Window (SDL2), Logger (spdlog), Engine loop, `AssetManager`.

## Phase 1 — Raw Vulkan Triangle ✅
Instance, validation layers, physical/logical device, surface, swapchain (Mailbox/FIFO), dynamic rendering, graphics pipeline, command pool/buffers, 2-frames-in-flight sync, swapchain recreation on resize.

## Phase 2 — RHI Abstraction ✅
`IRHI` pure abstract interface, `VulkanRHI` implementation, `Handle<Tag>` system, `VK_CHECK` macro.

## Phase 3 — Core Rendering ✅
- VMA integration, staging-buffer upload pattern, `UploadDynamicBuffer` for per-frame data
- `Vertex`: Position, Normal, TexCoord, Tangent (vec4, xyz + handedness)
- `Mesh`/`MeshPrimitive`/`AABB` in `MeshType.h`
- `Camera` (WASD + mouse look), `Input` (SDL scancode-based)
- Depth buffer (`VK_FORMAT_D32_SFLOAT`)
- Descriptor sets split: frame (camera UBO) + material (textures)
- Blinn-Phong → later replaced by PBR (Phase 6C)
- Push constants for model matrix
- `BeginFrame`/`EndFrame`/`DrawIndexed` split — draw calls happen outside `VulkanRHI` internals

## Phase 4 — Engine Architecture ✅

**4A — Build system.** CMake auto-compiles `.vert/.frag/.comp` → `.spv` via `add_custom_command`. `GLOB_RECURSE` only re-evaluates on reconfigure — reload the CMake project in CLion after adding a new shader file. `AssetManager::GetPath()` replaces all hardcoded paths.

**4B — Scene system (EnTT).** `Entity` wrapper (`AddComponent`/`GetComponent`/`HasComponent`), `Scene` owns `entt::registry`; `TransformComponent`/`MeshComponent`/`MaterialComponent`/`TagComponent`/`SpotLightComponent`; `Scene::FindEntityByName()`. `SceneLoader` parses JSON scenes with mesh/texture caching to avoid duplicate GPU uploads.

**4C — Material system.** Per-material descriptor sets (fixed earlier mid-frame `vkUpdateDescriptorSets` validation errors from a shared global set). `CreateMaterial(MaterialDesc)` / `BindMaterial(MaterialHandle)`. `MaterialDesc` holds 5 texture handles (albedo, normal, metallic, roughness, AO).

**4D — Frustum culling.** AABB computed in `MeshLoader` from vertex positions. 6-plane extraction from the view-projection matrix. `Scene::Render()` culls before issuing draw calls; draw/cull counters exposed to ImGui.

**4E — ImGui (overlay, not docking).** `InitImGui/ShutdownImGui/BeginImGuiFrame/RenderImGui` on `IRHI`. Must call `vkDeviceWaitIdle` inside `ShutdownImGui` before destroying its descriptor pool. SDL events forwarded via `ImGui_ImplSDL2_ProcessEvent` inside `Window::PollEvents` (known layering violation, cleanup deferred to Phase 8). Docking upgrade deferred to Phase 8 — needs an offscreen render target, built once alongside the render graph rather than twice.

## Phase 5 — Render Graph

**5A — Core graph (API-agnostic) ✅** `ResourceState` enum, `RGTexture` handle, `RenderPass` (builder pattern: `.Read()/.Write()/.SetExecute()`), `RenderGraph` (`AddPass`, `Compile` via Kahn's algorithm, cycle detection, `Execute`). Dependency edges built in two passes (write-map first, then read-edges) — a single-pass version misses dependencies declared out of order.

**5B — Vulkan barrier execution ✅** `ResourceState → VkImageLayout/AccessFlags/PipelineStageFlags` mapping. `ImportTexture(RGTexture, VkImage, initialState)`. Barriers batched per-pass into one `vkCmdPipelineBarrier`; aspect mask derived from state (depth vs. color).

**5C — Port existing rendering ✅** `RecordCommandBuffer` deleted. `BeginFrame` imports resources and runs a barrier-only `ForwardPass` through the graph, then manually begins dynamic rendering. `EndFrame` runs a barrier-only `PresentPass` before `vkEndCommandBuffer`/submit/present.

**5D — CommandBuffer abstraction ⬜ (deferred)** `RenderGraph::Execute` and pass callbacks still take raw `VkCommandBuffer`; `ImportTexture` still stores `VkImage` directly. Not blocking — worth revisiting only if a second graphics backend is attempted, or once compute passes (Forward+) make the Vulkan leakage actively painful.

## Phase 6 — Advanced Rendering

**6A — Room geometry ✅** `MeshLoader::CreatePlane(width, height, rhi)` — procedural quad with inline tangents. Test room built from planes + `BoxTextured.gltf` crates + `DamagedHelmet.gltf` (PBR metal/rough test asset, used to verify specular IBL).

**6B — Cascaded shadow maps ✅** 3 cascades, 2048×2048, `VK_FORMAT_D32_SFLOAT`, `sampler2DShadow` hardware PCF. `UpdateCascades()` computes split distances (log/uniform blend via `cascadeSplitLambda`), frustum corners → world space → light space, tight-fit ortho projection. `ShadowSettings` (`nearClip`, `farClip`, `cascadeSplitLambda`) exposed live via ImGui. Shadow pipeline uses `VK_CULL_MODE_FRONT_BIT` to reduce peter-panning; software PCF bias removed (`bias = 0.0`) since it doubled up with the hardware comparison sampler's implicit bias.

**6C — PBR shading ✅** Cook-Torrance BRDF (GGX, Smith geometry, Schlick fresnel) in `triangle.frag`. 5-texture material system with 1×1 fallback textures via `CreateSolidColorTexture()` (white albedo, flat normal, black metallic, mid-grey roughness, white AO) — created after `CreateCommandBuffers()` since they need `BeginOneTimeCommands()`. `MeshLoader::GenerateTangents` runs automatically when a glTF primitive has no `TANGENT` attribute (UV-gradient method, Gram-Schmidt orthogonalized, handedness from bitangent sign). `MeshLoader::LoadFromGLTF` returns `std::vector<MeshPrimitive>`, materials auto-loaded from the glTF PBR metallic-roughness workflow with a texture cache keyed by image index. Validated against DamagedHelmet, MetalRoughSpheres-class tests, and a scaled Sponza (`0.01` cm→m).

**6D — Post-processing 🔶**

- ✅ **ACES filmic tonemapping** (replaced Reinhard).
- ✅ **IBL (Image-Based Lighting)** — full split-sum pipeline (Karis 2013):
  - Compute pipeline support in `PipelineManager` (`ComputePipelineDesc`/`GetOrCreateCompute`), mirroring the graphics pipeline cache. `PipelineDesc.vertexInput = false` added for bufferless fullscreen passes.
  - `brdf_lut.comp` — environment-independent BRDF integration LUT (512×512, `rg16f`).
  - `equirect_to_cubemap.comp` — equirect HDR → 512×512×6 cubemap. One `VkImage`, two views (`2D_ARRAY` for compute `imageStore`, `CUBE` for `samplerCube` reads).
  - `irradiance_convolve.comp` — 32×32×6 diffuse irradiance cubemap, cosine-weighted hemisphere convolution.
  - `prefilter_env.comp` — 128×128×6 specular prefiltered cubemap, 5 mips (roughness 0/0.25/0.5/0.75/1.0), GGX importance sampling. One storage view per mip plus one `CUBE` view spanning all mips for `textureLod` reads.
  - `IRHI::LoadEnvironmentMap(pixels, width, height)` takes raw RGBA32F pixels (RHI stays agnostic to HDR file formats); `TextureLoader::LoadHDR(path)` does file loading (`stbi_loadf`) at the call site.
  - Skybox (`skybox.vert/frag`) — hardcoded 36-vertex unit cube in the vertex shader, camera-locked (view translation dropped), depth forced to the far plane via `gl_Position = clipPos.xyww`.
  - `IRHI::GetEnvironmentExposure()` — mutable float (ImGui-tunable), applied to skybox and IBL terms via `CameraUBO.cascadeSplits.w` (previously unused padding). HDR radiance has no fixed display scale — tuned empirically per environment (0.25–0.5 for `EveningRoad.hdr`).
  - Fallback: `CreateDefaultEnvironmentCubemap()`, a 1×1×6 black placeholder written into bindings 6–8 at `Init()`, since `triangle.frag` reads those bindings unconditionally every draw (unlike the skybox draw, gated behind `m_EnvironmentLoaded`).
- ⬜ **Bloom** — needs multi-pass render graph use (bright-pass threshold → gaussian blur → composite). Can reuse the `vertexInput = false` fullscreen-triangle pattern from the skybox.
- ⬜ **SSAO** — needs a geometry buffer (positions/normals), hemisphere sampling, blur pass.

**6E — Spot lights (with shadows) ✅** `SpotLightComponent` (color, intensity, innerCone, outerCone, range, enabled, castsShadow) — position/direction sourced from `TransformComponent` (`GetForward()`, rest direction `(0,-1,0)`). Perspective shadow projection (FOV from `outerCone * 2`, square aspect, near/far from a small constant and `range`), reuses the existing depth-only `m_ShadowPipeline`. Separate `SpotLightUBO` at frame set bindings 4–5. Up to `MAX_SPOT_LIGHTS` (8) illuminate; shadow-casting capped at `MAX_SPOT_SHADOW_CASTERS` (3, in `engine/renderer/Light.h`) — `Scene::GatherSpotLights` assigns the 3 shadow slots to the nearest `castsShadow=true` lights each frame. All 3 slots render every frame regardless of claim, to keep every shadow map image in the layout the descriptor set expects. Fully ECS-driven end-to-end.

**6F — Point lights (omnidirectional, cubemap shadows) ⬜** Deferred behind spot lights deliberately — cubemap shadows cost 6× per light, and spot lights (flashlight) are the more immediately useful horror-game source. Revisit once Forward+ (6G) exists, since point lights are exactly the "many cheap dynamic lights" case Forward+ is for.

**6G — Forward+ light culling ⬜** Tile-based compute shader light culling for many simultaneous dynamic lights (candles, muzzle flashes, supernatural effects). Compute pipeline support now exists (built for 6D's IBL precompute), making this more tractable than originally scoped. `RenderGraph::PassType::Compute` exists as an unused enum value (Phase 5A anticipated this) — per-frame Forward+ dispatches would be its first real use, vs. 6D's one-shot precompute dispatches which bypass the render graph entirely.

---

## ⚠️ Before touching shadow/camera code

Twice in this project, a find-and-replace across similarly-named variables (`m_ActiveLightSpaceMatrix` vs. `m_LightSpaceMatrices[i]`) silently broke shadows by writing a shadow-pass scratch variable into a slot that should have held the persistent per-cascade array, or vice versa. Symptom both times: "everything is lit correctly but shadows are gone/black." Caught via an ImGui debug block dumping raw matrix values — NaN, all-zero, or wildly-out-of-range is the tell.

**Before renaming or generalizing any "active"/"current" scratch variable, grep every write site first.** Keep shadow-pass-local scratch state (`m_ActiveLightSpaceMatrix`, used only to get the right matrix into `DrawShadowIndexed`'s push constants) clearly separated by name from persistent per-light-type arrays the forward pass reads (`m_LightSpaceMatrices[cascade]`, `m_SpotShadowMatrices[slot]`).

## Other gotchas

- **`glm::lookAt` near-parallel up vector** — when a light points nearly straight up/down, the fallback threshold needs to trigger earlier than intuition suggests: `0.99f` was not tight enough (produced NaN through the cross product), `0.9f` was. Applies to every light type that builds a view matrix from a direction (cascades, spot lights, point lights' 6 faces if 6F happens).
- **Shutdown ordering** — `vkDeviceWaitIdle` must run before destroying any GPU resource, including inside `ImGui_ImplVulkan_Shutdown()`.
- **Descriptor set layout arrays** — a `VkDescriptorSetLayoutCreateInfo.bindingCount` that doesn't match the actual `pBindings` array length reads uninitialized memory for the extra bindings. Always build the full literal array.
- **Designated initializer field order (C++20)** — fields in `{ .foo = ..., .bar = ... }` must appear in the struct's actual declaration order or MSVC rejects with `error C7560`. Easy to get wrong on large Vulkan structs (e.g. `VkSamplerCreateInfo`, where `maxLod` precedes `borderColor`) when grouping "related" fields feels natural but doesn't match declaration order.
- **sRGB double-gamma** — the swapchain format is `VK_FORMAT_B8G8R8A8_SRGB`, which already linear→sRGB-encodes on write. Never add a manual `pow(color, 1/2.2)` in a shader that writes to it — double-encodes and crushes the image toward white. Hit project-wide (`triangle.frag` and `skybox.frag` both had it); the tell was needing an absurdly low exposure (~0.05) to see any detail.
- **CMake shader globbing** — new `.vert`/`.frag`/`.comp` files are invisible until CMake reconfigures (CLion: File → Reload CMake Project).
- **`Vertex` struct drift** — any field added to `Vertex` needs `PipelineManager`'s `VkVertexInputAttributeDescription` array *and* every hand-built mesh generator (`CreatePlane`, etc.) updated in lockstep — array size, `location` indices, and `offsetof` all silently desync otherwise.

---

## Phase 7 — Game Systems 🔶

**7A — Jolt Physics ✅** Full integration behind an `IPhysics`/`JoltPhysics` split mirroring `IRHI`/`VulkanRHI` — no raw Jolt types outside `engine/physics/jolt/`.
- Fixed 60Hz timestep via an accumulator in `IPhysics::Update(deltaTime)`, clamped to `8×` the step so a stalled frame (breakpoint, window drag) can't spiral into an ever-growing catch-up loop.
- Static + Dynamic box colliders (`ColliderComponent`/`RigidBodyComponent`, box shape only), spawned once by `Scene::CreatePhysicsBodies`. Position *and* rotation synced back into `TransformComponent` every frame by `Scene::SyncPhysicsTransforms` — the rotation decomposition was hand-derived to match `TransformComponent::GetModelMatrix`'s exact `Rx*Ry*Rz` composition order (deliberately not GLM's generic Euler-angle helpers, which use a different convention and would have silently produced wrong rotations).
- Capsule `CharacterVirtual` controller: camera-relative WASD (flattened to XZ), space-jump, gravity, ground-snap, and step-up, all via Jolt's `ExtendedUpdate`. Automatically pushes `Dynamic` bodies it walks into — Jolt does this natively, but needed `BoxShape` density dropped from Jolt's 1000 kg/m³ default to 200 kg/m³ before pushes were fast enough to notice (a stock box was ~340 kg, heavier than stone).
- Camera position is interpolated between the character's pre/post fixed-step positions (leftover accumulator fraction as the blend weight) — reading the raw 60Hz-stepped position directly every render frame caused visible camera jitter, since render rate isn't phase-locked to the fixed physics rate.
- `kPhysicsTestScene` toggle in `main.cpp` swaps the full Sponza/DamagedHelmet room for a minimal ground + static obstacle boxes + dropped dynamic boxes, to test physics in isolation from heavy geometry.
- ⬜ Non-box colliders (sphere/capsule) and character-vs-character-pushes-character interactions aren't implemented — no concrete use case yet.

**7B — OpenAL Audio 🔶** Full integration behind an `IAudio`/`OpenALAudio` split mirroring `IRHI`/`IPhysics` — no raw AL types outside `engine/audio/openal/`.
- ✅ **3D positional audio + sound emitters as ECS components.** `AudioSourceComponent` (clip, gain, pitch, loop, autoplay, reference/max distance, rolloff) — position sourced from `TransformComponent`, same pattern as `SpotLightComponent`. `Scene::CreateAudioSources`/`SyncAudioSources` mirror the physics `CreatePhysicsBodies`/`SyncPhysicsTransforms` split. Listener tracks whichever camera is currently active (`SetListenerTransform`, called every frame regardless of `kPhysicsTestScene`).
- ✅ **WAV loading** — hand-rolled RIFF/WAVE parser (`AudioLoader::LoadWAV`, PCM only, no new dependency).
- ✅ Two real gotchas found and fixed during testing:
  - OpenAL only spatializes **mono** sources — a stereo buffer plays back flat with no attenuation/panning regardless of `AL_POSITION`/distance settings. `OpenALAudio::CreateBuffer` now auto-downmixes any stereo PCM to mono unconditionally (averages L+R per sample, 8-bit and 16-bit) before upload, so this can't silently bite on a future stereo asset.
  - OpenAL's default distance model (Inverse Distance Clamped) doesn't make `AL_MAX_DISTANCE` mean "silent beyond this range" — it only caps how far attenuation keeps *increasing*; gain plateaus at a low-but-nonzero value and stays audible forever past it. Switched to `AL_LINEAR_DISTANCE_CLAMPED` (set once in `Init()`) so gain actually reaches 0 at `maxDistance`.
- ⬜ **OGG support** — WAV only so far; `stb_vorbis` (already available via the `stb` dependency) not wired up yet.
- ⬜ **Reverb zones** — needs OpenAL's EFX extension (auxiliary effect slots), not touched.
- ⬜ **Audio occlusion through geometry** — needs a raycast against the Jolt collision world per source per frame to attenuate/muffle occluded emitters; physics (7A) now provides what this would need, but it isn't wired up.
- ✅ `SceneInspectorPanel` now has an `AudioSourceComponent` editor section (gain/pitch/loop/autoplay/reference-max distance/rolloff, clip/sourceHandle shown read-only).

**7C — Player System 🔶** First-person controller (distinct from the free-fly debug `Camera`) is effectively done as a side effect of 7A's `CharacterVirtual` work. Still missing: interaction system, simple inventory.

**7D — Lua Scripting 🔶** Full integration behind an `IScripting`/`LuaScripting` split mirroring `IRHI`/`IPhysics`/`IAudio` — no raw sol2/`lua_State` types outside `engine/scripting/lua/`.
- ✅ **Checkpoint 1 — lifecycle + component access.** `ScriptComponent` (path + instance handle); `Scene::CreateScriptInstances` auto-writes an `OnStart()/OnUpdate(dt)/OnFixedUpdate(fixedDt)` stub file via `CreateScriptFileIfMissing` if the path doesn't exist yet, then loads it. Each script instance gets its own sandboxed `sol::environment` (falls back to the shared globals table for lookups) so two entities' scripts can both define `OnUpdate` without clobbering each other. `OnFixedUpdate` runs its own internal 60Hz accumulator — same cadence as `IPhysics`'s, kept as an independent constant rather than shared, matching the project's established small-local-duplication convention. Every current component (`TransformComponent`/`TagComponent`/`SpotLightComponent`/`ColliderComponent`/`RigidBodyComponent`/`AudioSourceComponent`) is bound as a live reference — editing a field from Lua writes straight into the ECS storage — via the *same* template instantiations the C++ side calls (e.g. `&Entity::GetComponent<TransformComponent>`), not a separate reflection layer. `scene:FindEntityByName`/`scene:CreateEntity` let a script reach entities other than its own `self`.
- ✅ **Checkpoint 2 — physics/audio/input access.** `physics`, `audio`, `input` bound as true Lua globals (shared across every script instance, set once in `LuaScripting::Init`) — nearly the full `IPhysics`/`IAudio` interfaces (skipping only `CreateBuffer`/`DestroyBuffer`, which take raw PCM bytes a script has no legitimate way to construct), plus a `Key`/`MouseButton` constants table so scripts don't need raw `SDL_Scancode` numbers. `RigidBodyComponent.bodyHandle` and `AudioSourceComponent.clip`/`sourceHandle` are exposed so a script can hand a component's handle straight to `physics:GetBodyPosition(handle)`/`audio:PlaySource(handle)`, etc.
- Needed an MSVC `/bigobj` compile flag on `OsirisEngine` — sol2's `new_usertype<>` calls in `LuaScripting.cpp` are template-heavy enough to exceed the default object-file section limit (`C1128`).
- **Hard rule (see CLAUDE.md)**: any field added/renamed/removed on a component in `Components.h` must be reflected in both `LuaScripting.cpp`'s `BindAPI()` and `SceneInspectorPanel.cpp`'s `DrawComponents()`.
- Full API reference: `docs/scripting_api.html` (local, searchable). Hard rule (see CLAUDE.md) also covers it now: any relevant `BindAPI()` change must update the doc.
- ✅ **Add/Remove Component UI** in `SceneInspectorPanel` — a button + popup offering Spot Light/Collider/Rigid Body/Audio Source/Script (whichever the selected entity doesn't already have; Mesh/Material excluded, no sensible default without asset loading), plus a per-section "Remove" button for every component except Tag/Transform (both guaranteed present by `Scene::CreateEntity`, and other code depends on that). Adding one mid-session doesn't retroactively spawn a live Jolt body/OpenAL source/script instance — same caveat as editing an existing Collider/RigidBody/AudioSource/Script field. Removing a RigidBody/AudioSource/Script that *does* own one destroys it first (`DestroyBody`/`DestroySource`/`DestroyInstance`) rather than orphaning it, which is why `SceneInspectorPanel::Draw` now takes `IPhysics*`/`IAudio*`/`IScripting*`.
- ⬜ **Hot-reload** — a script loads once at `Scene::CreateScriptInstances` time; editing the `.lua` file mid-session has no effect until the next run.

Audio and scripting can both run in parallel with anything else — neither depends on physics or the render graph being finished.

## Phase 8 — Editor & Tools ⬜

- **8A — Docked ImGui editor.** Needs an offscreen render target (`VkImage` the scene renders into, displayed via `ImGui::Image` inside a dockable viewport) plus `ImGui::DockSpaceOverViewport()`. Panel code from 4E/6 transfers directly — docking is additive.
- **8B — Asset browser.** Browse meshes/textures/scenes, drag-and-drop into scene.
- **8C — Scene editor.** Place/move/rotate objects, save scene back to JSON, transform gizmos. Entity/component inspection — a scrollable entity list plus a typed, editable field UI per component type — already exists ahead of schedule via `SceneInspectorPanel` (`engine/editor/`), built as a standalone detour outside the phase plan. Still missing for 8C proper: gizmos, viewport picking, and JSON round-trip.
- **8D — Render graph visualizer.** Debug view of passes, timings, resource dependency graph.
- **8E — CommandBuffer abstraction** (completes 5D). Revisit once a second backend is attempted or Forward+ compute passes make the raw `VkCommandBuffer` leakage actively painful.
- **8F — Entity parent/child hierarchy.** Every glTF primitive is currently an independent flat `Scene` entity — moving a multi-primitive model means moving each part separately. Needs `ParentComponent`/`ChildrenComponent` and transform propagation in `Scene::Render`. Deferred until the editor exists — hierarchies are far more usable with gizmos than JSON editing.

## Phase 9 — First Game (Horror Prototype) ⬜

Deliberately small scope — proving the full stack end-to-end, not a full game:

- One small level (room + corridors), built via the JSON `SceneLoader` pipeline
- Player can walk and collide with world geometry (7A)
- Basic interaction (pick up / use objects)
- One enemy with simple AI
- Ambient audio, footsteps, triggered events (7B)
- A jump-scare mechanism
- Win/lose condition

---

## Suggested session priority from here

1. Rest of **7C** (interaction system, inventory) — physics (7A), audio (7B), and scripting (7D) are all in place now, plus the Scene Inspector can build a test entity's component set entirely by hand (Add Component), so there's finally the full toolkit for a real interaction (a door script that plays a sound and locks a rigid body, etc). **Or** **Bloom + SSAO** (rest of 6D) if visual polish is wanted first.
2. Round out **7B**'s remaining scope (OGG, reverb zones, occlusion) whenever audio needs more than a looping test emitter — none of it blocks anything else.
3. **8A** (docked editor) whenever panel-based iteration starts feeling cramped — `SceneInspectorPanel` already covers basic inspection (plus Add Component) in the meantime.
4. Script hot-reload — smaller follow-up to the scripting work above; doesn't block anything.
