# Animation

The first implementation supports glTF 2.0 `.gltf` and `.glb` models with skeletons and named animation clips, including ordinary node translation, rotation and scale animation. Meshes, materials and clip data are cached; each spawned model has an independent Animator and pose.

## Try the included scene

1. Build the Horror target in CLion and launch it yourself.
2. Save any unsaved apartment work first. In Edit mode, choose **File > Load Scene... > animation_test**.
3. Press **F5**. Two small block characters should be visible. The left character starts Idle. Hold **W** for Walk, hold **Shift + W** for Run, and release them for Idle.
4. The right character keeps walking at 70% speed, independently of the left. Watch the ground shadows while their limbs move.
5. Press **F5** to return to Edit. Select the `GraphCharacter` root in the hierarchy and enable **Animator > Preview in editor**. Try the clip selector, pause/resume, time scrubber and playback speed.
6. Load `apartment` again when finished. Neither its scene file nor the Horror launch configuration was changed by this feature.

The fixture is intentionally a simple six-joint block character, not a finished character asset. It uses in-place loops and a stationary camera so animation can be checked without a movement controller. Source files are in `games/horror/assets/models/animation_test/`; `tools/generate_animation_fixture.cjs` regenerates them with Node.js. The GLB includes its texture, and the glTF uses an external PNG and buffer, to exercise both import paths.

## Asset requirements

- Include the mesh, its skeleton and the desired clips in the same glTF/GLB export. Clip names must be unique. Unnamed clips are exposed as `Animation_0`, `Animation_1`, and so on.
- Use at most four joint influences per vertex (`JOINTS_0` and `WEIGHTS_0`). A second influence set is rejected with an import error. Weights are normalized during loading.
- All joints referenced by a skin must belong to the file's selected/default scene.
- Translation, quaternion rotation and scale tracks support STEP, LINEAR and CUBICSPLINE interpolation. Animation cannot target matrix-authored nodes.
- For controller-driven movement, supply **in-place** Idle/Walk/Run clips. The controller moves the spawned model root. Animation does not move that root, extract root motion or drive physics bodies. Authored translation on imported bones/nodes is still played, so a non-in-place clip can visibly drift within its controller root.
- Direct FBX/BVH imports, separate-file clip attachment, retargeting, morph targets, animation events, IK, ragdolls, layers, additive blending and blend trees are not implemented. Convert other source formats to an appropriate glTF/GLB before importing.

## C++ playback

`Scene::SpawnModel` attaches `AnimatorComponent` to the **spawned model root**, not each imported bone or material primitive. The first clip starts automatically. It only advances during Play unless editor preview is enabled.

```cpp
#include "scene/Scene.h"

Osiris::Entity character = scene.SpawnModel(
    "Character", "models/animation_test/test_character.glb", rhi);
if (character.IsValid() && character.HasComponent<Osiris::AnimatorComponent>()) {
    auto& animator = character.GetComponent<Osiris::AnimatorComponent>().player;
    animator.Play("Idle", 0.0f, true);
    animator.Play("Walk", 0.2f, true);
    animator.SetSpeed(1.0f);
}
```

`Play(clip, fadeSeconds = 0.2, loop = true)` returns false for an unknown clip or invalid fade duration. Repeatedly requesting the current clip does not rewind it. Use `Seek(0)` to restart, then `Resume()` if paused. `Play` leaves graph-driven playback.

`Pause`, `Resume`, `Stop`, `Seek(seconds)`, `SetSpeed`, `GetTime`, `GetDuration`, `GetCurrentClip`, `IsPlaying`, `IsLooping` and `IsFinished` are available in both C++ and Lua. `Stop` returns to the imported bind pose. A non-looping clip holds its final pose; `IsFinished()` becomes true. Playback speed must be finite and non-negative. Zero freezes clip time; use `Pause()` to freeze transitions and fades too.

Normal engine frames update Animators automatically. If writing your own frame loop, call `scene.UpdateAnimations(dt, playMode)` after controller/physics changes, then `scene.PrepareSkinning(rhi)` after `rhi->BeginFrame()` and before shadow/forward passes. Both render passes use the same palette for the frame.

## A programmatic graph

A graph is a small state machine: states select clips, and transitions compare named numeric parameters. Crossfades blend translation/scale and use quaternion interpolation for rotation. Both source and target advance during a normal fade. Interrupting a fade starts the next fade from the displayed pose.

```cpp
auto& animator = character.GetComponent<Osiris::AnimatorComponent>().player;
auto& graph = animator.GetGraph();
graph.Clear();
graph.AddState("Idle", "Idle");
graph.AddState("Walk", "Walk");
graph.AddState("Run", "Run");

using Compare = Osiris::AnimationComparison;
graph.AddTransition("Idle", "Run", "speed", Compare::Greater, 3.0f, 0.2f);
graph.AddTransition("Idle", "Walk", "speed", Compare::Greater, 0.1f, 0.2f);
graph.AddTransition("Walk", "Idle", "speed", Compare::LessEqual, 0.1f, 0.2f);
graph.AddTransition("Walk", "Run", "speed", Compare::Greater, 3.0f, 0.2f);
graph.AddTransition("Run", "Idle", "speed", Compare::LessEqual, 0.1f, 0.2f);
graph.AddTransition("Run", "Walk", "speed", Compare::LessEqual, 3.0f, 0.2f);
animator.StartGraph("Idle");

// Each frame, supply the controller's actual horizontal speed in metres per second.
animator.SetFloat("speed", horizontalSpeed);
```

- `AddState(name, clip, loop = true, speed = 1)` and `AddTransition(from, to, parameter, comparison, threshold, fadeSeconds = 0.2)` return success/failure. Check these results in game code.
- Define states before transitions. `StartGraph(state)` validates all clip references and restarts at time zero. Stop playback before rebuilding a live graph, then call `StartGraph` again.
- The **first matching transition** wins, at most once per update. Put a direct Idle-to-Run transition before Idle-to-Walk because both conditions can be true.
- Comparisons are `Less`, `LessEqual`, `Greater`, `GreaterEqual`, `Equal` and `NotEqual`. A missing parameter reads as zero. `SetBool(name, value)` stores 0 or 1 for the same comparisons.
- State speed multiplies Animator playback speed. Fade duration is measured in real seconds, independent of those speeds. This is timed crossfading, not a continuous speed blend tree or synchronized foot-cycle system.

## Lua

The same player and graph are available from a script attached to the imported model root:

```lua
local hasAnimator = false

function OnStart()
    hasAnimator = self:HasAnimator()
    if not hasAnimator then return end
    local animator = self:GetAnimator()
    local graph = animator:GetGraph()
    graph:Clear()
    assert(graph:AddState("Idle", "Idle"))
    assert(graph:AddState("Walk", "Walk"))
    assert(graph:AddTransition("Idle", "Walk", "speed", AnimationComparison.Greater, 0.1, 0.2))
    assert(graph:AddTransition("Walk", "Idle", "speed", AnimationComparison.LessEqual, 0.1, 0.2))
    assert(animator:StartGraph("Idle"))
end

function OnUpdate(dt)
    if hasAnimator then
        -- Fetched fresh each frame; see the caveat below about not caching player/graph.
        self:GetAnimator():SetFloat("speed", input:IsKeyHeld(Key.W) and 2.0 or 0.0)
    end
end
```

Use `ipairs(animator:GetClips())` to inspect available clip names. `self:GetAnimatorComponent()` exposes `player` and `previewInEditor`. `self:AddAnimator()` restores a removed Animator on an imported animated model root; it does not create a skeleton on an arbitrary entity. Do not retain player/graph references after removing their component, destroying the entity or loading another scene. This also applies indirectly: adding or removing an Animator on any entity (including a different one, such as another script spawning a new animated model) can relocate every entity's AnimatorComponent storage, so do not cache `player`/`graph` across frames either. Call `self:GetAnimator()` again each time you need it instead.

The complete three-state example is `games/horror/assets/scripts/animation_demo.lua`. Full Lua signatures are also documented in the local `docs/scripting_api.html` reference.

## Saving, previews and current boundaries

Scene saving includes the graph, parameters, selected clip/state, loop setting, playback speed and playing/paused flag. Loading restarts at time zero, without an unfinished crossfade. Removing the Animator is persisted as `"animator": null`; the model can still render its bind pose. `previewInEditor` is intentionally not saved.

Animation uses separate pose transforms, so editor previews do not overwrite saved node transforms. F5 restores the pre-Play Animator state as well as transforms. Editing a transform directly on an animated imported node is overridden while that node is animated; position the overall model with its root instead.

Skinned forward rendering and directional/spot shadows share the same joint data. Render culling and transparent-object sorting use conservative deformed bounds. Transparent and double-sided materials retain their existing behavior, including the existing whole-primitive transparency sorting limitation. Editor triangle picking, grounding and physics colliders still use the imported mesh geometry, not a CPU-skinned triangle mesh. For now, select the animated model root through the hierarchy when necessary and use a controller collider for movement. Do not delete bones or change the imported skeleton topology.

## Automated verification

`OsirisAnimationTests` is an opt-in CMake target (`EXCLUDE_FROM_ALL`). It tests sampling, looping, crossfade interruption, independent instances, graphs, serialization, glTF/GLB import, embedded/external textures, cache reuse, palette selection in forward/shadow draws, deformed bounds, Lua bindings and F5 reset. Its recording RHI does not initialize the engine, a window, Vulkan, physics or audio.

From an MSVC developer shell:

```text
cmake --build cmake-build-debug-visual-studio --target OsirisAnimationTests
cmake-build-debug-visual-studio\bin\OsirisAnimationTests.exe C:\Dev\Osiris
```

This is a headless correctness check, not a visual rendering test. The owner still runs the Horror scene in CLion to check deformation, materials, shadows, controls and Vulkan validation output.
