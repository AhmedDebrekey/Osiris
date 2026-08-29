# Osiris Engine: Development Roadmap

Custom C++23 / Vulkan 1.4 game engine, built from scratch as a general-purpose foundation for
original games, not a horror-only engine. First demo (Phase 9): a local co-op tank game, capture
the flag. A horror title is the planned first full-scope game, further out (Phase 10).
Solo developer, Windows + CLion, CMake `FetchContent`.

| | |
|---|---|
| **Repo** | `github.com/AhmedDebrekey/Osiris` |
| **Local** | `C:\Dev\Osiris` |
| **GPU** | RTX 4060 |

This document is the authoritative phase plan. Status: ✅ done · 🔶 partial · ⬜ not started.
Last reconciled with the implementation: 2026-08-29.

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
| `engine/editor/` | `Editor` (owned and driven by `Engine::RunFrame`), split Hierarchy/Details inspector, Content Browser, `SceneFileMenu`/`GameExporter`, `RenderDebugPanel` |
| `engine/ui/` | Immediate-mode Play UI (`GameUI::DrawText`/`DrawRect`), exposed to Lua as `ui.Text`/`ui.Rect` |
| `assets/shaders/` | Forward, shadow, skybox, post-process, and one-shot IBL compute shaders |
| `assets/scenes/` | JSON scene files |

> **Iron rule:** no raw Vulkan calls outside `rhi_vulkan/`.
> **Handles:** `BufferHandle`, `TextureHandle`, `MaterialHandle`, `PipelineHandle`, all `Handle<Tag>` templates, `INVALID_HANDLE_ID = UINT32_MAX`, backed by a generic `AllocateSlot<T>()` slot map (not per-type allocators).
> **Pipelines:** always through `PipelineManager::GetOrCreate(PipelineDesc)` (graphics) or `GetOrCreateCompute(ComputePipelineDesc)` (compute): hashed caches, never call `vkCreateGraphicsPipelines`/`vkCreateComputePipelines` directly. `PipelineDesc.vertexInput = false` covers bufferless fullscreen-triangle/hardcoded-geometry passes (skybox today; reusable for bloom/SSAO).

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

## Phase 0: Foundation ✅
Window (SDL2), Logger (spdlog), Engine loop, `AssetManager`.

## Phase 1: Raw Vulkan Triangle ✅
Instance, validation layers, physical/logical device, surface, swapchain (FIFO when VSync is enabled; Mailbox/Immediate preference when disabled), dynamic rendering, graphics pipeline, command pool/buffers, 2-frames-in-flight sync, swapchain recreation on resize. `WindowDesc` carries VSync and a configurable engine-side frame cap; Play mode can show measured FPS for verification.

## Phase 2: RHI Abstraction ✅
`IRHI` pure abstract interface, `VulkanRHI` implementation, `Handle<Tag>` system, `VK_CHECK` macro.

## Phase 3: Core Rendering ✅
- VMA integration, staging-buffer upload pattern, `UploadDynamicBuffer` for per-frame data
- `Vertex`: Position, Normal, TexCoord, Tangent (vec4, xyz + handedness)
- `Mesh`/`MeshPrimitive`/`AABB` in `MeshType.h`
- `Camera` (WASD + mouse look), `Input` (SDL scancode-based)
- Depth buffer (`VK_FORMAT_D32_SFLOAT`)
- Descriptor sets split: frame (camera UBO) + material (textures)
- Blinn-Phong → later replaced by PBR (Phase 6C)
- Push constants for model matrix
- `BeginFrame`/`EndFrame`/`DrawIndexed` split: draw calls happen outside `VulkanRHI` internals

## Phase 4: Engine Architecture ✅

**4A: Build system.** CMake auto-compiles `.vert/.frag/.comp` → `.spv` via `add_custom_command`. `GLOB_RECURSE` only re-evaluates on reconfigure, so reload the CMake project in CLion after adding a new shader file. `AssetManager::GetPath()` replaces all hardcoded paths.

**4B: Scene system (EnTT).** `Entity` wrapper (`AddComponent`/`GetComponent`/`HasComponent`), `Scene` owns `entt::registry`; `TransformComponent`/`MeshComponent`/`MaterialComponent`/`TagComponent`/`SpotLightComponent`; `Scene::FindEntityByName()`. `SceneLoader` parses JSON scenes with mesh/texture caching to avoid duplicate GPU uploads.

**4C: Material system.** Per-material descriptor sets (fixed earlier mid-frame `vkUpdateDescriptorSets` validation errors from a shared global set). `CreateMaterial(MaterialDesc)` / `BindMaterial(MaterialHandle)`. `MaterialDesc` holds 5 texture handles (albedo, normal, metallic, roughness, AO).

**4D: Frustum culling.** AABB computed in `MeshLoader` from vertex positions. 6-plane extraction from the view-projection matrix. `Scene::Render()` culls before issuing draw calls; draw/cull counters exposed to ImGui.

**4E: ImGui (original overlay milestone).** `InitImGui/ShutdownImGui/BeginImGuiFrame/RenderImGui` on `IRHI`. Must call `vkDeviceWaitIdle` inside `ShutdownImGui` before destroying its descriptor pool. SDL events are still forwarded via `ImGui_ImplSDL2_ProcessEvent` inside `Window::PollEvents` (known layering violation). The docking/offscreen-target upgrade was completed in 8A.

## Phase 5: Render Graph

**5A: Core graph (API-agnostic) ✅** `ResourceState` enum, `RGTexture` handle, `RenderPass` (builder pattern: `.Read()/.Write()/.SetExecute()`), `RenderGraph` (`AddPass`, `Compile` via Kahn's algorithm, cycle detection, `Execute`). Dependency edges built in two passes (write-map first, then read-edges): a single-pass version misses dependencies declared out of order.

**5B: Vulkan barrier execution ✅** `ResourceState → VkImageLayout/AccessFlags/PipelineStageFlags` mapping. `ImportTexture(RGTexture, VkImage, initialState)`. Barriers batched per-pass into one `vkCmdPipelineBarrier`; aspect mask derived from state (depth vs. color).

**5C: Port existing rendering ✅** `RecordCommandBuffer` deleted. The graph sequences image-state transitions around manually-started dynamic rendering. It now covers the Edit viewport sampling/UI path, Play scene-color/post-process path, optional post-process preview, and the final present transition; pass callbacks still mostly exist for barriers rather than owning the draw bodies.

**5D: CommandBuffer abstraction ⬜ (deferred)** `RenderGraph::Execute` and pass callbacks still take raw `VkCommandBuffer`; `ImportTexture` still stores `VkImage` directly. Not blocking: worth revisiting only if a second graphics backend is attempted, or once compute passes (Forward+) make the Vulkan leakage actively painful.

## Phase 6: Advanced Rendering

**6A: Room geometry ✅** `MeshLoader::CreatePlane(width, height, rhi)`, a procedural quad with inline tangents. Test room built from planes + `BoxTextured.gltf` crates + `DamagedHelmet.gltf` (PBR metal/rough test asset, used to verify specular IBL).

**6B: Cascaded shadow maps ✅** 3 cascades, 2048×2048, `VK_FORMAT_D32_SFLOAT`, `sampler2DShadow` hardware PCF. `UpdateCascades()` computes split distances (log/uniform blend via `cascadeSplitLambda`), fits a stable cascade sphere, and snaps the projection to shadow-map texels to prevent edge swimming during camera motion. `nearClip`, `farClip`, and `cascadeSplitLambda` are exposed live via ImGui; the raster depth-bias values remain baked into the cached shadow pipeline. The shadow pipeline uses `VK_CULL_MODE_FRONT_BIT` to reduce peter-panning. `triangle.frag` also dims IBL ambient partway inside directional shadow as an intentional stylistic choice.

**6C: PBR shading ✅** Cook-Torrance BRDF (GGX, Smith geometry, Schlick fresnel) in `triangle.frag`. 5-texture material system with 1×1 fallback textures via `CreateSolidColorTexture()` (white albedo, flat normal, black metallic, mid-grey roughness, white AO), created after `CreateCommandBuffers()` since they need `BeginOneTimeCommands()`. `MeshLoader::GenerateTangents` runs automatically when a glTF primitive has no `TANGENT` attribute (UV-gradient method, Gram-Schmidt orthogonalized, handedness from bitangent sign). `MeshLoader::LoadFromGLTF` returns `std::vector<GltfNode>` with local transforms, child/parent indices, and PBR-loaded primitives; `Scene::SpawnModel` mirrors that hierarchy into ECS entities. Validated against DamagedHelmet, MetalRoughSpheres-class tests, and a scaled Sponza (`0.01` cm→m).

**6D: Post-processing 🔶**

- ✅ **ACES filmic tonemapping** (replaced Reinhard).
- ✅ **IBL (Image-Based Lighting)**: full split-sum pipeline (Karis 2013):
  - Compute pipeline support in `PipelineManager` (`ComputePipelineDesc`/`GetOrCreateCompute`), mirroring the graphics pipeline cache. `PipelineDesc.vertexInput = false` added for bufferless fullscreen passes.
  - `brdf_lut.comp`: environment-independent BRDF integration LUT (512×512, `rg16f`).
  - `equirect_to_cubemap.comp`: equirect HDR → 512×512×6 cubemap. One `VkImage`, two views (`2D_ARRAY` for compute `imageStore`, `CUBE` for `samplerCube` reads).
  - `irradiance_convolve.comp`: 32×32×6 diffuse irradiance cubemap, cosine-weighted hemisphere convolution.
  - `prefilter_env.comp`: 128×128×6 specular prefiltered cubemap, 5 mips (roughness 0/0.25/0.5/0.75/1.0), GGX importance sampling. One storage view per mip plus one `CUBE` view spanning all mips for `textureLod` reads.
  - `IRHI::LoadEnvironmentMap(pixels, width, height)` takes raw RGBA32F pixels (RHI stays agnostic to HDR file formats); `TextureLoader::LoadHDR(path)` does file loading (`stbi_loadf`) at the call site.
  - Skybox (`skybox.vert/frag`): hardcoded 36-vertex unit cube in the vertex shader, camera-locked (view translation dropped), depth forced to the far plane via `gl_Position = clipPos.xyww`.
  - `IRHI::GetEnvironmentExposure()`: mutable float (ImGui-tunable), applied to skybox and IBL terms via `CameraUBO.cascadeSplits.w` (previously unused padding). HDR radiance has no fixed display scale, tuned empirically per environment (0.25–0.5 for `EveningRoad.hdr`).
  - Fallback: `CreateDefaultEnvironmentCubemap()`, a 1×1×6 black placeholder written into bindings 6–8 at `Init()`, since `triangle.frag` reads those bindings unconditionally every draw (unlike the skybox draw, gated behind `m_EnvironmentLoaded`).
- ✅ **Fullscreen cinematic post-process pass.** Play mode renders forward shading into an offscreen scene-color image, then runs a bufferless fullscreen triangle into the swapchain. Current effects are Bloom, vignette, chromatic aberration, and animated film grain. Settings are live-editable in the editor, available to Lua through `postprocess`, and can be previewed in Edit mode without replacing the normal viewport.
- 🔶 **Bloom:** an initial single-pass LDR Bloom is implemented in `postprocess.frag`. It uses a configurable soft brightness threshold plus two blur sample rings, then composites the glow before vignette and film grain. `bloomIntensity`, `bloomThreshold`, and `bloomRadius` are exposed in the Post-Process editor panel and through Lua's `postprocess` global. The shader compiles; visual tuning in both Play mode and the Edit-mode post-process preview is still required before marking it complete.
- ⬜ **HDR multi-resolution Bloom upgrade:** move the Play and Edit scene-color targets to `RGBA16_FLOAT`, keep lighting untonemapped until the final composite, and replace the single-pass blur with a downsample/upsample chain. This becomes worthwhile if the initial Bloom is too narrow, too expensive at high resolutions, or cannot preserve enough highlight range after the current forward and skybox ACES tonemapping.
- ⬜ **SSAO**: needs a geometry buffer (positions/normals), hemisphere sampling, blur pass.

**6E: Spot lights (with shadows) ✅** `SpotLightComponent` (color, intensity, innerCone, outerCone, range, enabled, castsShadow), position/direction sourced from `TransformComponent` (`GetForward()`, rest direction `(0,-1,0)`). Perspective shadow projection (FOV from `outerCone * 2`, square aspect, near/far from a small constant and `range`), reuses the existing depth-only `m_ShadowPipeline`. Separate `SpotLightUBO` at frame set bindings 4–5. Up to `MAX_SPOT_LIGHTS` (8) illuminate; shadow-casting capped at `MAX_SPOT_SHADOW_CASTERS` (3, in `engine/renderer/Light.h`): `Scene::GatherSpotLights` assigns the 3 shadow slots to the nearest `castsShadow=true` lights each frame. All 3 slot passes still begin/end every frame so their maps reach the descriptor-set layout, but unclaimed slots only clear instead of redrawing the entire scene. Fully ECS-driven end-to-end.

**6F: Point lights (omnidirectional, cubemap shadows) ⬜** Deferred behind spot lights deliberately: cubemap shadows cost 6× per light, and spot lights (flashlight) are the more immediately useful horror-game source. Revisit once Forward+ (6G) exists, since point lights are exactly the "many cheap dynamic lights" case Forward+ is for.

**6G: Forward+ light culling ⬜** Tile-based compute shader light culling for many simultaneous dynamic lights (candles, muzzle flashes, supernatural effects). Compute pipeline support now exists (built for 6D's IBL precompute), making this more tractable than originally scoped. `RenderGraph::PassType::Compute` exists as an unused enum value (Phase 5A anticipated this): per-frame Forward+ dispatches would be its first real use, vs. 6D's one-shot precompute dispatches which bypass the render graph entirely.

---

## ⚠️ Before touching shadow/camera code

Twice in this project, a find-and-replace across similarly-named variables (`m_ActiveLightSpaceMatrix` vs. `m_LightSpaceMatrices[i]`) silently broke shadows by writing a shadow-pass scratch variable into a slot that should have held the persistent per-cascade array, or vice versa. Symptom both times: "everything is lit correctly but shadows are gone/black." Caught via an ImGui debug block dumping raw matrix values: NaN, all-zero, or wildly-out-of-range is the tell.

**Before renaming or generalizing any "active"/"current" scratch variable, grep every write site first.** Keep shadow-pass-local scratch state (`m_ActiveLightSpaceMatrix`, used only to get the right matrix into `DrawShadowIndexed`'s push constants) clearly separated by name from persistent per-light-type arrays the forward pass reads (`m_LightSpaceMatrices[cascade]`, `m_SpotShadowMatrices[slot]`).

## Other gotchas

- **`glm::lookAt` near-parallel up vector**: when a light points nearly straight up/down, the fallback threshold needs to trigger earlier than intuition suggests: `0.99f` was not tight enough (produced NaN through the cross product), `0.9f` was. Applies to every light type that builds a view matrix from a direction (cascades, spot lights, point lights' 6 faces if 6F happens).
- **Shutdown ordering**: `vkDeviceWaitIdle` must run before destroying any GPU resource, including inside `ImGui_ImplVulkan_Shutdown()`.
- **Descriptor set layout arrays**: a `VkDescriptorSetLayoutCreateInfo.bindingCount` that doesn't match the actual `pBindings` array length reads uninitialized memory for the extra bindings. Always build the full literal array.
- **Designated initializer field order (C++20)**: fields in `{ .foo = ..., .bar = ... }` must appear in the struct's actual declaration order or MSVC rejects with `error C7560`. Easy to get wrong on large Vulkan structs (e.g. `VkSamplerCreateInfo`, where `maxLod` precedes `borderColor`) when grouping "related" fields feels natural but doesn't match declaration order.
- **sRGB double-gamma**: the swapchain format is `VK_FORMAT_B8G8R8A8_SRGB`, which already linear→sRGB-encodes on write. Never add a manual `pow(color, 1/2.2)` in a shader that writes to it: double-encodes and crushes the image toward white. Hit project-wide (`triangle.frag` and `skybox.frag` both had it); the tell was needing an absurdly low exposure (~0.05) to see any detail.
- **CMake shader globbing**: new `.vert`/`.frag`/`.comp` files are invisible until CMake reconfigures (CLion: File → Reload CMake Project).
- **`Vertex` struct drift**: any field added to `Vertex` needs `PipelineManager`'s `VkVertexInputAttributeDescription` array *and* every hand-built mesh generator (`CreatePlane`, etc.) updated in lockstep: array size, `location` indices, and `offsetof` all silently desync otherwise.

---

## Phase 7: Game Systems 🔶

**7A: Jolt Physics ✅** Full integration behind an `IPhysics`/`JoltPhysics` split mirroring `IRHI`/`VulkanRHI`. No raw Jolt types outside `engine/physics/jolt/`.
- Fixed 60Hz timestep via an accumulator in `IPhysics::Update(deltaTime)`, clamped to `8×` the step so a stalled frame (breakpoint, window drag) can't spiral into an ever-growing catch-up loop.
- Static, Kinematic, and Dynamic box colliders (`ColliderComponent`/`RigidBodyComponent`, box shape only), created through `Scene::CreatePhysicsBodies`/`RebuildPhysicsBody`. Non-static position and rotation sync back into `TransformComponent` every frame through `Scene::SyncPhysicsTransforms`; the rotation decomposition matches `TransformComponent::GetModelMatrix`'s exact `Rx*Ry*Rz` composition order.
- Capsule `CharacterVirtual` controller: camera-relative WASD (flattened to XZ), space-jump, gravity, ground-snap, and step-up, all via Jolt's `ExtendedUpdate`. Automatically pushes `Dynamic` bodies it walks into: Jolt does this natively, but needed `BoxShape` density dropped from Jolt's 1000 kg/m³ default to 200 kg/m³ before pushes were fast enough to notice (a stock box was ~340 kg, heavier than stone).
- Camera position is interpolated between the character's pre/post fixed-step positions (leftover accumulator fraction as the blend weight): reading the raw 60Hz-stepped position directly every render frame caused visible camera jitter, since render rate isn't phase-locked to the fixed physics rate.
- Contact begin/end events are collected from Jolt callbacks, drained on the main thread, and dispatched symmetrically to Lua as `OnCollision(other)`/`OnCollisionEnd(other)`. `RigidBodyComponent.isSensor` provides non-solid trigger volumes; `lockRotationToYAxis` supports upright tanks. Runtime body position/velocity setters and queued entity destruction support projectiles and respawns.
- ✅ `Scene::RebuildPhysicsBody(entity, physics)`: destroys (if valid) and recreates a single entity's body from its current Transform/Collider/RigidBody values. Jolt has no in-place shape-resize or motion-type-change, so this is the direct fix rather than a workaround; `CreatePhysicsBodies` itself is now just this called once per matching entity. The Scene Inspector's Collider/Rigid Body editors call it on every edit, so `halfExtents`/`motionType` changes actually reach the live Jolt body now (previously silent, the editor updated the ECS field only). Lua gets the same fix via two dedicated `Entity` methods (`entity:SetColliderHalfExtents(vec3)`/`entity:SetRigidBodyMotionType(BodyMotionType)`) rather than plain field assignment, since a bound reference write has no interception point a rebuild could hook into (and a sol2 property setter on `halfExtents` alone wouldn't catch `.x = ...`-style partial mutation). See `docs/scripting_api.html`'s Collider/RigidBody sections.
- ⬜ Non-box colliders (sphere/capsule) and character-vs-character-pushes-character interactions aren't implemented. No concrete use case yet.
- ✅ The previously suspected inverted Y rotation was confirmed and corrected: `GetBodyRotationEuler` now uses `asin(m[2][0])`, matching `TransformComponent`'s `Rx*Ry*Rz` convention.

**7B: OpenAL Audio 🔶** Full integration behind an `IAudio`/`OpenALAudio` split mirroring `IRHI`/`IPhysics`. No raw AL types outside `engine/audio/openal/`.
- ✅ **3D positional audio + sound emitters as ECS components.** `AudioSourceComponent` (clip, clip path, gain, pitch, loop, autoplay, reference/max distance, rolloff) gets its position from the resolved world transform, following the same pattern as `SpotLightComponent`. `Scene::CreateAudioSources`/`RebuildAudioSource`/`SyncAudioSources` mirror the physics create/rebuild/sync split. The listener tracks the camera used by `Engine::RenderFrame`.
- ✅ **WAV loading**: hand-rolled RIFF/WAVE parser (`AudioLoader::LoadWAV`, PCM only, no new dependency).
- ✅ Two real gotchas found and fixed during testing:
  - OpenAL only spatializes **mono** sources: a stereo buffer plays back flat with no attenuation/panning regardless of `AL_POSITION`/distance settings. `OpenALAudio::CreateBuffer` now auto-downmixes any stereo PCM to mono unconditionally (averages L+R per sample, 8-bit and 16-bit) before upload, so this can't silently bite on a future stereo asset.
  - OpenAL's default distance model (Inverse Distance Clamped) doesn't make `AL_MAX_DISTANCE` mean "silent beyond this range": it only caps how far attenuation keeps *increasing*; gain plateaus at a low-but-nonzero value and stays audible forever past it. Switched to `AL_LINEAR_DISTANCE_CLAMPED` (set once in `Init()`) so gain actually reaches 0 at `maxDistance`.
- ⬜ **OGG support**: WAV only so far; `stb_vorbis` (already available via the `stb` dependency) not wired up yet.
- ⬜ **Reverb zones**: needs OpenAL's EFX extension (auxiliary effect slots), not touched.
- ⬜ **Audio occlusion through geometry**: needs a raycast against the Jolt collision world per source per frame to attenuate/muffle occluded emitters; physics (7A) now provides what this would need, but it isn't wired up.
- ✅ The Details panel edits gain/pitch/loop/autoplay/reference/max distance/rolloff, shows live handle state, and accepts WAV drops that load the buffer and rebuild the source immediately.

**7C: Player System 🔶** The first-person controller is Lua-driven (`fps_controller.lua`) on an ECS `CameraComponent` + `CharacterComponent` entity, distinct from the free-fly Edit camera. A ray/AABB interaction system is also complete: `InteractableComponent` supplies prompt/range/key, `Engine::RunFrame` draws the Play-mode prompt, and the target script receives `OnInteract(interactor)`. A simple inventory is the remaining planned piece.

**7D: Lua Scripting 🔶** Full integration behind an `IScripting`/`LuaScripting` split mirroring `IRHI`/`IPhysics`/`IAudio`. No raw sol2/`lua_State` types outside `engine/scripting/lua/`.
- ✅ **Checkpoint 1: lifecycle + component access.** `ScriptComponent` (path + instance handle); `Scene::CreateScriptInstances` auto-writes an `OnStart()/OnUpdate(dt)/OnFixedUpdate(fixedDt)` stub file via `CreateScriptFileIfMissing` if the path doesn't exist yet, then loads it. Each script instance gets its own sandboxed `sol::environment` (falls back to the shared globals table for lookups) so lifecycle functions do not clobber other entities. Component access now includes Transform, Tag, hierarchy metadata, Spot Light, Collider, Rigid Body, Audio Source, Camera, Character, Model Source, Interactable, material handles, and procedural mesh creation. Bound fields are live ECS references unless a dedicated rebuild helper is required.
- ✅ **Checkpoint 2: physics/audio/input access.** `physics`, `audio`, `input` bound as true Lua globals (shared across every script instance, set once in `LuaScripting::Init`), nearly the full `IPhysics`/`IAudio` interfaces (skipping only `CreateBuffer`/`DestroyBuffer`, which take raw PCM bytes a script has no legitimate way to construct), plus a `Key`/`MouseButton` constants table so scripts don't need raw `SDL_Scancode` numbers. `RigidBodyComponent.bodyHandle` and `AudioSourceComponent.clip`/`sourceHandle` are exposed so a script can hand a component's handle straight to `physics:GetBodyPosition(handle)`/`audio:PlaySource(handle)`, etc.
- ✅ **Checkpoint 3: gameplay-facing runtime API.** Lua can create and destroy entities safely, attach procedural box/plane meshes, reuse material handles, create live box rigid bodies/sensors, attach scripts immediately, manipulate hierarchy/world transforms, and receive collision/interact callbacks. `ui.Text`/`ui.Rect`/`ui.FadeToBlack`, `camera.Shake`, `postprocess`, transform direction helpers, and `MoveTowards` cover the current CTF and horror-prototype scripting needs without putting game rules in `main.cpp`.
- Needed an MSVC `/bigobj` compile flag on `OsirisEngine`: sol2's `new_usertype<>` calls in `LuaScripting.cpp` are template-heavy enough to exceed the default object-file section limit (`C1128`).
- **Hard rule (see `AGENTS.md`)**: any field added/renamed/removed on a component in `Components.h` must be reflected in both `LuaScripting.cpp`'s `BindAPI()` and `SceneInspectorPanel.cpp`'s `DrawComponents()`.
- Full API reference: `docs/scripting_api.html` (local, searchable). The `AGENTS.md` sync rule also requires relevant `BindAPI()` changes to update the reference.
- ✅ **Add/Remove Component UI** in the Details panel offers Spot Light, Collider, Rigid Body, Camera, Character, Audio Source, Script, and Interactable. Mesh/Material remain asset-driven. Collider/Rigid Body edits rebuild the live body, audio asset drops rebuild the OpenAL source, script drops replace the live instance, and removal tears down owned backend resources before removing the component.
- ⬜ **Hot-reload**: a script loads once at `Scene::CreateScriptInstances` time; editing the `.lua` file mid-session has no effect until the next run.

**7E: Skeletal Animation and Skinned Meshes ⬜** Required for animated characters in the full horror game. The static dark figure planned for the first small horror slice does not depend on this phase.
- ⬜ **glTF skin and clip import.** Extend `MeshLoader` to read `JOINTS_0`, `WEIGHTS_0`, joint hierarchies, inverse bind matrices, and animation channel samplers for translation, rotation, and scale. The initial path is glTF 2.0 through fastgltf; the currently unused ufbx dependency is not part of this milestone.
- ⬜ **Animation runtime.** Add skeleton and clip asset data plus an `AnimatorComponent` that selects a clip, playback time, speed, looping, and play/pause state. Evaluate local joint transforms each frame and build the final joint palette from animated world transforms and inverse bind matrices.
- ⬜ **GPU skinning in every geometry path.** Add a skinned vertex/pipeline variant and upload each visible animator's joint palette through `IRHI`. Forward rendering, directional shadows, and spot-light shadows must use the same deformed pose. Static meshes must keep their current pipeline and vertex layout unchanged.
- ⬜ **Scene, editor, and Lua integration.** Persist animator settings in JSON, expose clip selection and playback controls in the Details panel, bind the runtime controls to Lua, and update `docs/scripting_api.html` in the same change. Animated bounds must remain conservative enough that moving limbs are not incorrectly frustum-culled.
- ⬜ **Acceptance asset and playback test.** Import one licensed glTF character with at least idle and walk clips, verify looping and pause/resume, test a basic two-clip crossfade, and confirm the animated pose matches in the forward and shadow passes.
- **Deferred beyond the first version:** animation state machines, root motion, inverse kinematics, additive layers, morph targets, retargeting between skeletons, and animation compression.

## Phase 8: Editor & Tools 🔶

- **8A: Docked ImGui editor ✅** Edit mode renders the scene into `m_ViewportColorImage`/`m_ViewportDepthImage`, samples it inside a docked Viewport, then renders editor UI to the swapchain. Play mode uses the separate scene-color/post-process path described in 6D. Viewport resizing uses a 100ms trailing-edge debounce before the required `vkDeviceWaitIdle`/resource recreation. `VulkanRHI::ApplyEditorTheme()` supplies the charcoal and amber editor theme.
- **8B: Content Browser ✅** `AssetCatalog::BuildAssetTree` recursively scans `assets/`; the editor shows a folder tree, searchable icon grid, refresh, New Folder/New Script, and confirmed deletion. glTF files spawn into the viewport, while Lua and WAV assets drop onto entities to create or replace live script/audio components. `Scene::SpawnModel` remains the single glTF spawn path used by the browser, Lua, and `SceneLoader`.
- **8C: Scene editor ✅**
  - ✅ **Transform gizmos**, via ImGuizmo (`FetchContent`, pinned to a commit). Translate uses world space; rotate/scale use local space. `TransformComponent::SetFromMatrix`/`ExtractRotation` provide the shared matrix-to-TRS decomposition and choose the nearest equivalent Euler solution to prevent value jumps.
  - ✅ **Viewport mouse picking.** A click casts a CPU ray against transformed mesh AABBs, chooses the nearest hit, and walks to the outermost model root so the selected entity is the one that owns gameplay components. Gizmo clicks are excluded.
  - ✅ **JSON scene round-trip.** Load/Save covers transforms, hierarchy, glTF model roots and their gameplay components, spot lights, colliders, rigid bodies/sensors, audio, scripts, cameras, characters, and interactables. `BoxSourceComponent` preserves procedural box half-extents and texture provenance, while script paths are normalized to one AssetManager-relative convention. A raw `MeshComponent` with no model/box source still has no recoverable provenance and is intentionally skipped.
- **8D: Render graph visualizer 🔶** `RenderDebugPanel` now shows CPU frame time, per-pass Vulkan timestamp results (cascades, spot shadows, forward), all shadow maps, and an on-demand post-process preview. The actual pass/resource dependency graph is not visualized yet.
- **8E: CommandBuffer abstraction ⬜** (completes 5D). Revisit once a second backend is attempted or Forward+ compute passes make the raw `VkCommandBuffer` leakage actively painful. Not started.
- **8F: Entity parent/child hierarchy ✅** `ParentComponent`/`ChildrenComponent` (fields private, `friend class Scene`, only `Scene::SetParent` may write them, keeping the two in sync). `Scene::SetParent(child, newParent)` rejects cycles (walks the proposed new parent's ancestor chain first) and preserves world transform across a reparent. `Scene::GetWorldTransform(entity)` composes the model matrix up the parent chain: every read site that used to treat `TransformComponent` as world space now goes through it instead (`Render`, `RenderShadows`, `GatherSpotLights`, `CreatePhysicsBodies`/`RebuildPhysicsBody`, `SyncPhysicsTransforms`, `RebuildCharacter`/`SyncCharacterTransforms`, the 8C gizmo). A child's `TransformComponent` is local to its parent now; a root entity's is still world space, so nothing about the existing flat scene changed meaning. Known, accepted limitation: a non-uniformly-scaled ancestor can introduce shear a local TRS transform can't represent exactly, same limitation Unity/Unreal/Godot all have, not solved here. `Scene::DestroyEntity` cascades to children (leaf-first). `SceneInspectorPanel`'s entity list became a tree (`ImGui::TreeNodeEx`), reparenting via drag-and-drop (same payload pattern `AssetBrowserPanel` already used), plus a right-click "Delete" and a `Del`-key shortcut (guarded by `IsAnyItemActive()` so it doesn't fire while typing in a text field), both cascade to children through the same `DestroyEntity`. `Scene::SpawnModel` now returns one root entity with mesh-bearing primitives as its children, instead of a flat vector of primitives.
- **8G: Play/Edit mode ✅** `Engine::RunFrame` owns the F5 toggle and gates the editor, scripting, fixed updates, physics, transform sync, collision dispatch, and queued destruction. Entering Play rebuilds live bodies/characters/audio from edited component data, captures the transform snapshot, resets script instances so `OnStart` gets fresh local state, and starts autoplay sources. Exiting restores non-static rigid-body and character transforms, rebuilds their Jolt objects, and stops audio. The snapshot is intentionally not a full scene undo, so entities created during Play remain after returning to Edit. Edit uses the free-fly camera; Play follows `Scene::FindCameraEntity()` and its transform. Character movement is script-driven. Editor windows are hidden in Play, while game-facing `ui.Text`/`ui.Rect` remain visible.

- **8H: `main.cpp`/`Engine` boundary refactor ✅** `Engine` now owns `Editor`, two cameras, frame timing, event/input handling, Play/Edit transitions, gameplay-system ordering, interaction prompts, rendering, ImGui submission, and presentation. `Engine::RunFrame(Scene&)` is the default complete frame loop; lower-level entry points remain available for custom clients. `games/testbed/main.cpp` only initializes the engine/HDR environment, owns a `Scene`, and calls `RunFrame`. First-person and tank controls live in Lua instead of the client executable.

- **8I: Portable Windows game export 🔶** Release builds expose `File > Export Game...`. `GameExporter` copies the running executable and runtime DLLs, clones the asset tree, serializes the current scene as the launch scene, writes `assets/game.json` plus a recipient README, and produces both an `exports/<name>/` folder and `exports/<name>-win64.zip`. Packaged builds resolve assets beside the executable, enter Play automatically, hide the editor/F5 path, and preserve the editor's frame-cap/FPS-overlay settings. Debug exports are rejected because their MSVC debug runtime is not redistributable. Debug and Release compile/link cleanly; an extracted ZIP and a second Windows machine still need end-to-end validation before this is marked complete. Selective dependency-only asset packaging, an installer, and code signing are deliberately outside the current proof-of-shipping scope.

## Phase 9: First Demo (Local Co-op Tank CTF) 🔶

Two players, one machine, capture the flag: each player has a base and a flag; grab the other
player's flag and carry it home to score; getting shot drops the flag and respawns the tank at its
own base.

**Implemented foundation:**

- ✅ Shared elevated/top-down camera through the scene's primary `CameraComponent`, avoiding any split-screen renderer work.
- ✅ Two-player keyboard controls in `tank_controller.lua`: independent hull movement, turret rotation, fire controls, projectile cooldown, and a carry-speed penalty.
- ✅ Runtime projectile spawning with procedural mesh/material reuse, dynamic Jolt bodies, impulses, collision callbacks, timed cleanup, and a safe deferred entity-destruction queue.
- ✅ Collision begin/end events and `isSensor` trigger volumes. The flag/home scripts implement pickup, drop-on-hit, capture, score tracking, return-home behavior, and a win threshold.
- ✅ Immediate-mode Play UI primitives (`ui.Text`/`ui.Rect`) and a built-in interaction prompt. The rendering path composites these after post-processing.
- ✅ `assets/scenes/ctf.json` is the canonical committed demo scene: arena collision/cover, two model-backed tanks, two model-backed flags, home sensors, shared camera, and the CTF coordinator are entirely scene-authored. Tank/flag model files and their local licensing metadata are versioned with it, and `games/testbed/main.cpp` loads it by default.
- ✅ The Play HUD shows both scores, flag state, controls, and transient events. First to three captures gets a win screen; `R` starts a fade-to-black reset that clears projectiles, restores tanks/flags, resets scores and script state, then fades back into the next round.
- ✅ CTF feedback uses transient Play-camera shake on tank hits and a final queued fade overlay that covers all other game UI.

**Still required before Phase 9 is genuinely complete:**

- ⬜ **Run the acceptance playtest.** Complete multiple two-player matches through three captures and at least two round resets. Verify spawn points, self/projectile collisions, simultaneous sensor contacts, repeated Play/Edit sessions, camera framing, flag drop/recovery, score gating while the home flag is away, and cleanup of projectiles across resets.
- ⬜ **Validate the shipped artifact.** Export from Release at a verified frame cap, extract the ZIP to a directory unrelated to the repository, and run it without relying on the development working directory. Then repeat on a second Windows machine to confirm the Vulkan-driver and VC++ Redistributable instructions are sufficient.
- **Controller input is not required for this demo.** The accepted control scheme is the existing two-player split keyboard setup.

## Phase 10: Horror Prototype ⬜

Deliberately small scope, proving the full stack end-to-end, not a full game. The planned first
full-scope game on Osiris, after the Phase 9 demo:

- One small level (room + corridors), built via the JSON `SceneLoader` pipeline
- Player can walk and collide with world geometry (7A)
- Basic interaction (engine support exists; horror-specific interactions are not authored)
- One enemy with simple AI. A static silhouette is acceptable for the first slice; animated idle/walk behavior depends on 7E.
- Ambient audio, footsteps, triggered events (7B)
- A jump-scare mechanism
- Win/lose condition

---

## Suggested session priority from here

1. **Acceptance-test the canonical CTF match.** Play through the complete steal/drop/capture/win/reset loop several times with both keyboard players, including repeated F5 Play/Edit sessions. Fix only issues the test exposes.
2. **Validate the portable Release artifact.** Set a known cap (start with 60 FPS), export the current scene, extract the ZIP outside the repository, run it there, and then send that exact ZIP to a second Windows PC. This closes 8I and proves the game can actually be shipped rather than only compiled.
3. **Runtime-test and tune the initial Bloom.** Check bright HDR environments, spot lights, the CTF HUD, and the horror scene in both Play mode and the Edit-mode post-process preview. Adjust the defaults only after seeing them in motion. Keep the HDR multi-resolution upgrade deferred unless this simpler pass shows a concrete quality or performance problem.
4. **Choose the next content-driven slice.** The strongest next milestone after the CTF proof is Phase 10's small horror prototype; pull inventory, OGG/reverb/occlusion, SSAO, or point lights/Forward+ forward only when that slice needs them.
5. **Add skeletal animation before animated character production.** Phase 10 can start with the documented static dark figure, but complete 7E before committing to animated pedestrians, coworkers, or speaking characters for the larger horror game.
6. **Keep tooling abstractions behind concrete pressure.** Finish 8D's pass/resource graph view when render-graph debugging needs it; revisit 8E only when compute scheduling or a second backend makes raw `VkCommandBuffer` exposure costly.
7. **Script hot-reload** remains a useful small follow-up, but it does not block either the CTF build or the horror slice.
