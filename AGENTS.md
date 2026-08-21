# Osiris Engine

Custom C++23 / Vulkan 1.4 game engine, solo project, building toward a horror game (see
`docs/roadmap.md` — the owner's own phase plan, authoritative for status; don't edit it). RHI
abstraction over a Vulkan backend, PBR forward renderer, EnTT-based scene, ImGui debug tooling.
See `docs/architecture.md` and `docs/tech-stack.md` for details.

## Working with this repo

- **You may compile the project, but never run it.** The `compile-project` skill does a real
  CMake+Ninja+MSVC build (catches actual compiler/linker errors, not just shader syntax) — use it
  after a non-trivial multi-file C++ change instead of guessing whether it compiles. What stays
  off-limits is executing the resulting `OsirisEngine.exe`/testbed binary: the owner builds and
  *tests* — runs the program, plays the feature, judges whether it works — in their own environment
  (CLion on Windows) and reports back what they see. "Compiles cleanly" is not "works"; don't blur
  the two when reporting results.
- **Shader changes**: CMake *does* auto-compile `assets/shaders/*.{vert,frag,comp}` to `.spv` as
  part of a normal build (a `CompileShaders` custom target `OsirisEngine` depends on) — but since
  you don't run builds, compile the specific file yourself to catch syntax errors before handing
  off: `glslc assets/shaders/x.vert -o assets/shaders/x.vert.spv` (glslc ships with the Vulkan
  SDK, already on PATH). The `compile-shaders` skill does this. Note CMake's shader glob only
  re-evaluates on reconfigure — irrelevant to your direct `glslc` calls, but worth knowing if the
  owner says a *new* shader file isn't picking up.
- Windows + Git Bash/PowerShell. `cmake-build-debug-visual-studio/` and
  `cmake-build-release-visual-studio/` are build directories — never edit generated files there.
- **Commit messages**: match the owner's own style — a single short line, no body paragraph, no
  trailers. No `Co-Authored-By: Codex` line, even though that's this harness's default; the
  owner writes every commit in this repo themselves and wants it to stay that way. Look at
  `git log --oneline` for the actual voice (e.g. `Add Jolt physics, scene inspector panel, and
  OpenAL audio`, `Directional light casts shadows again`) rather than defaulting to Conventional
  Commits (`feat:`/`fix:`) or a multi-paragraph body — this repo uses neither.

## Hard rules

- **No raw Vulkan calls outside `engine/rhi_vulkan/`.** Everything above the RHI talks to
  `IRHI` (`engine/rhi/RHI.h`) only.
- **Pipelines only through `PipelineManager::GetOrCreate(PipelineDesc)`** (graphics) or
  **`GetOrCreateCompute(ComputePipelineDesc)`** (compute) — never call `vkCreateGraphicsPipelines`/
  `vkCreateComputePipelines` directly outside it. Both are hashed caches. `PipelineDesc` has a
  `vertexInput = false` option for bufferless fullscreen-triangle/hardcoded-geometry passes (the
  skybox hardcodes a cube in the vertex shader instead of using a vertex buffer) — reuse it for
  bloom/SSAO rather than adding a new mechanism.
- **GPU resource handles are `Handle<Tag>` templates** (`BufferHandle`, `TextureHandle`,
  `MaterialHandle`, `PipelineHandle`, ...), `INVALID_HANDLE_ID = UINT32_MAX`. Slot storage goes
  through the generic `AllocateSlot<T>()` template (a null-check lambda picks a free slot or
  appends) — don't write a new per-type allocator.
- **Any change to a component in `engine/scene/Components.h`** — a field added, renamed, or
  removed, or a new component struct entirely — **must be reflected in both**
  `engine/scripting/lua/LuaScripting.cpp`'s `BindAPI()` (the Lua `new_usertype` binding) **and**
  `engine/editor/SceneInspectorPanel.cpp`'s `DrawComponents()` (the ImGui editor section). Neither
  omission errors at compile time — a field missing from `BindAPI()` is just silently unreachable
  from Lua, and one missing from `DrawComponents()` is just silently absent from the inspector.
  Update all three in the same change, not as a follow-up.
- **Any change to `engine/scripting/lua/LuaScripting.cpp`'s `BindAPI()`** — a binding added,
  renamed, removed, or a caveat that changes — **must be reflected in `docs/scripting_api.html`
  if relevant** (new/changed globals, types, fields, or functions; a caveat that's no longer
  true, e.g. "not exposed to scripts yet" once it is). This is the same silent-drift risk as the
  component rule above, just one hop further downstream: the doc is the only place a script author
  looks, so it rotting out of sync makes it actively misleading rather than just incomplete.

## Before touching shadow/camera/light code — read this first

Twice in this project, a find-and-replace across similarly-named matrix variables silently broke
shadows by writing a shadow-pass scratch value into a slot that should've held the persistent
per-light array (or vice versa). Both times the symptom was "everything's lit correctly but
shadows are gone/black," caught only by dumping raw matrix values in ImGui (NaN / all-zero /
wildly-out-of-range is the tell).

Keep shadow-pass-local scratch state (e.g. `m_ActiveLightSpaceMatrix` — only used to get the
right matrix into `DrawShadowIndexed`'s push constants during whichever pass is currently active)
clearly separated by name from persistent per-light arrays the forward pass reads
(`m_LightSpaceMatrices[cascade]`, `m_SpotShadowMatrices[slot]`). **Before renaming or generalizing
any "active"/"current" scratch variable, grep every write site first** — don't assume a rename is
complete just because it compiles.

## Other gotchas

- `glm::lookAt` with a near-parallel up vector produces NaN through the cross product. The
  fallback threshold needs to trigger earlier than intuition suggests: `0.99f` was not tight
  enough in practice, `0.9f` was. Applies to every light type that builds a view matrix from a
  direction (cascades, spot lights, future point-light cube faces).
- `vkDeviceWaitIdle` must run before destroying any GPU resource, including inside
  `ImGui_ImplVulkan_Shutdown()` — ImGui's own descriptor pool has the same "in use" validation
  failure mode as engine-owned resources if you skip it.
- A `VkDescriptorSetLayoutCreateInfo.bindingCount` that doesn't match the actual `pBindings` array
  length reads uninitialized memory for the extra bindings. Always build the full literal array —
  never claim a count larger than what's declared.
- Any field added to `Vertex` (`engine/renderer/MeshType.h`) needs `PipelineManager`'s
  `VkVertexInputAttributeDescription` array *and* every hand-built mesh generator (`CreatePlane`,
  etc.) updated in lockstep — array size, `location` indices, and `offsetof` all silently desync
  otherwise.
- Designated initializers (`.foo = ..., .bar = ...`) must list fields in the struct's actual
  declaration order — C++20 requirement, MSVC rejects violations with `error C7560`. Easy to get
  wrong on large Vulkan structs where it's tempting to group "related" fields together instead of
  following declaration order — e.g. `VkSamplerCreateInfo` declares `maxLod` *before*
  `borderColor`, so `.borderColor = ..., .maxLod = ...` in that order fails to compile. Hit this
  for real during the IBL work; double-check field order against the actual struct (not intuition)
  whenever a new field is added to an existing designated-init block, not just on new structs.
- The swapchain format is `VK_FORMAT_B8G8R8A8_SRGB` — hardware already linear→sRGB-encodes on
  write. Never add a manual `pow(color, 1/2.2)` gamma correction in a shader that writes to it;
  that double-encodes and crushes the image toward white. Hit this project-wide (both
  `triangle.frag` and `skybox.frag` had it) — the symptom was needing an absurdly low exposure
  multiplier (~0.05) to see any detail, which is the tell for double-gamma, not "the source HDR is
  just bright."

## Conventions

- Member variables: `m_PascalCase`. Locals/params: `camelCase`.
- No doc-comment blocks. A comment only earns its place when it explains a non-obvious *why* (a
  hidden invariant, a workaround, a cross-file sync requirement) — never a *what*.
- GLSL uniform buffer structs are mirrored by hand on the C++ side (`CameraBufferFull` /
  `SpotLightBufferFull` in `VulkanRHI.cpp` vs. the `CameraUBO`/`SpotLightUBO` blocks in the
  shaders) — std140 layout, field order and vec4/mat4 alignment must match exactly. Any shared
  array-size constant (e.g. `MAX_SPOT_LIGHTS`) needs a comment on both the C++ and GLSL side
  pointing at the other, since GLSL can't `#include` `Light.h`.
- New engine-wide constants/shared render-data structs go in `engine/renderer/Light.h`-style
  headers, visible to both `engine/rhi/RHI.h` and `engine/rhi_vulkan/VulkanRHI.cpp` without a
  circular include.
- No premature abstraction: solo/small-scope project, prefer the direct fix over a new interface
  layer unless a second concrete use case already exists.
- Never use an em dash in code comments, commit messages, documentation, or chat responses about
  this project. Use a comma, a colon, parentheses, or split into two sentences instead.

## Directory map

- `engine/rhi/` — `IRHI` interface, `RHITypes.h` (handles/descs). `CommandBuffer.h` is a
  partial abstraction (Phase 5D, deferred — pass callbacks still take raw `VkCommandBuffer`).
- `engine/rhi_vulkan/` — the only backend: instance/device/swapchain, descriptor sets, shadow
  maps, `PipelineManager`.
- `engine/renderer/` — `Camera`, `Light` structs, `MeshType` (`Vertex`/`Mesh`/`MeshPrimitive`/
  `AABB`), `Frustum`, `RenderGraph` (topological-sort pass sequencer + Vulkan barrier execution;
  `PassType::Compute` exists as an enum value but is unused — reserved for Forward+).
- `engine/scene/` — the actual ECS: `entt::registry` wrapped by `Scene`/`Entity`, `Components.h`.
  (`engine/ecs/` is an empty placeholder — ignore it, the real ECS lives here.)
- `engine/assets/` — `MeshLoader` (glTF via fastgltf + procedural planes + tangent generation),
  `TextureLoader`, `SceneLoader` (JSON scene format — not currently wired into `games/testbed`,
  which builds its scene by hand).
- `engine/core/` — `Engine` bootstrap, `Log`, `AssetManager` (asset path resolution).
- `engine/platform/` — SDL2 `Window`/`Input`.
- `engine/physics/` — `IPhysics` interface, `PhysicsTypes.h`; `physics/jolt/JoltPhysics` is the
  only backend (Phase 7A, integrated). Fixed 60Hz accumulator, box colliders, `CharacterVirtual`
  first-person controller.
- `engine/audio/` — `IAudio` interface, `AudioTypes.h`; `audio/openal/OpenALAudio` is the only
  backend (Phase 7B, integrated). 3D positional sources via `AudioSourceComponent`, hand-rolled
  WAV loading (`assets/AudioLoader`), no OGG/reverb/occlusion yet.
- `engine/scripting/` — `IScripting` interface, `ScriptTypes.h`, `ScriptTemplate`;
  `scripting/lua/LuaScripting` (sol2 + Lua) is the only backend (Phase 7D, integrated).
  `ScriptComponent` + `OnStart`/`OnUpdate`/`OnFixedUpdate` lifecycle, full component read/write
  access, `physics`/`audio`/`input` bound as globals. Full API reference: `docs/scripting_api.html`.
  Needs MSVC `/bigobj` on `OsirisEngine` (sol2's `new_usertype<>` calls hit `C1128` without it).
- `engine/editor/` — `SceneInspectorPanel`: ImGui entity list, per-component editor, and
  Add/Remove Component (Spot Light/Collider/Rigid Body/Audio Source/Script are addable —
  Mesh/Material excluded, no sensible default without asset loading; everything except
  Tag/Transform is removable). Remove destroys the live Jolt body/OpenAL source/Lua instance
  first if the component owns one (`Draw()` takes `IPhysics*`/`IAudio*`/`IScripting*` for this).
  Ahead-of-schedule detour outside the phase plan, built during 7A; see Phase 8C in
  `docs/roadmap.md`.
- Tracy (profiler) is a locked tech-stack choice that isn't even fetched yet. `ufbx` is fetched
  and linked but has no `#include` anywhere (glTF via fastgltf is the only mesh-loading path in
  use). Don't assume either is wired up just because it's planned/available.
- `games/testbed/` — the sample app (`main.cpp`) exercising the engine.
- `assets/shaders/` — hand-written GLSL, auto-compiled to `.spv` by CMake's `CompileShaders`
  target (see above for why you still compile manually). `triangle.vert/frag` (forward),
  `shadow.vert` (depth-only cascades/spot), `skybox.vert/frag`, and four one-shot IBL precompute
  shaders: `brdf_lut.comp`, `equirect_to_cubemap.comp`, `irradiance_convolve.comp`,
  `prefilter_env.comp`.

## Renderer at a glance

Vulkan dynamic rendering (no framebuffers/render passes). Forward PBR (Cook-Torrance,
metallic/roughness) with ACES filmic tonemapping. No manual gamma correction in shaders — the
sRGB-format swapchain does that on write (see the gotcha above). Y-up, right-handed, column-major,
1 unit = 1 meter.

Two shadow systems, both `sampler2DShadow` hardware-compare:
- Directional light: 3 cascaded shadow maps, 2048×2048 each, split distances from
  `ShadowSettings::cascadeSplitLambda` (log/uniform blend).
- Spot lights: up to `MAX_SPOT_LIGHTS` (8) illuminate; up to `MAX_SPOT_SHADOW_CASTERS` (3) get a
  1024×1024 shadow map each, chosen per-frame by `Scene::GatherSpotLights` (nearest
  `castsShadow=true` lights to the camera). All 3 shadow-caster slots render every frame
  regardless of whether a light claims them, to keep every shadow map image in the layout the
  descriptor set expects — see the comment at the spot shadow pass loop in `main.cpp`.

**IBL (Phase 6D, complete)**: split-sum image-based lighting, precomputed once at startup from an
equirectangular HDR (`IRHI::LoadEnvironmentMap`, one-shot compute dispatches via
`BeginOneTimeCommands`/`EndOneTimeCommands`, entirely bypassing the render graph). Produces a
512×512×6 environment cubemap, a 32×32×6 diffuse irradiance cubemap, a 128×128×6/5-mip specular
prefiltered cubemap, and a 512×512 BRDF integration LUT — sampled by `triangle.frag`'s ambient term
and by the skybox (`skybox.vert/frag`, camera-locked hardcoded cube, `vertexInput = false`).
`IRHI::GetEnvironmentExposure()` (mutable float ref, ImGui-tunable) scales both — HDR radiance has
no fixed display scale, so this is found empirically per environment map, not computed. A 1×1×6
black placeholder cubemap (`CreateDefaultEnvironmentCubemap`) backs the IBL descriptor bindings
until a real environment loads, since `triangle.frag` reads them unconditionally every draw call
(unlike the skybox draw, which is gated behind `m_EnvironmentLoaded`).

Descriptor set 0 = frame data: binding 0 camera UBO (note: `cascadeSplits.w` doubles as environment
exposure — was unused padding), 1–3 cascade shadow samplers, 4 spot shadow sampler array[3], 5 spot
light UBO, 6–8 IBL environment/irradiance/prefiltered cubemaps, 9 IBL BRDF LUT. Set 1 = material
data: 5 bindings (albedo/normal/metallic/roughness/AO, no emissive slot yet).
