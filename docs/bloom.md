# HDR bloom

Based on Jorge Jimenez's SIGGRAPH 2014 presentation, *Next Generation Post Processing
in Call of Duty: Advanced Warfare*, using the supplied v18 deck's bloom section and notes.
The PowerPoint is a reference only and is not needed to build or export a game.

## Reference mapping

| Slides | Presentation approach | Osiris implementation |
|---|---|---|
| 145 | Non-thresholded HDR input | Forward shading and skybox write linear HDR into RGBA16F scene targets. No bright-pass threshold. |
| 147-157 | Filter while downsampling, 13 bilinear fetches | `bloom_downsample.frag` averages five overlapping four-fetch groups, weighted 0.5 and four times 0.125. |
| 159-161 | Progressive reconstruction with a 3x3 tent filter | `bloom_upsample.frag` filters the next coarser result and combines it with the current downsample level. |
| 164-168 | Partial Karis average on the first downsample | Each four-fetch group uses normalized weights `1 / (1 + luminance)`, only from full resolution to half resolution. Later downsampling is linear. |
| 170 | Six R11G11B10 levels | Up to six levels, starting at half resolution. Packed floating-point targets fall back to RGBA16F if format support is unavailable. |

The complete bloom is added to HDR scene color before the existing ACES approximation.
Tone mapping no longer runs inside the material or skybox shader. Vignette and film grain
remain after tone mapping. The sRGB destination performs display encoding, with no extra
manual gamma correction.

## Adaptations

- Osiris stores the progressive sum as a running average. At level `i` of `N`, the current
  downsample receives weight `1 / (N - i)`, and the filtered coarser accumulation receives
  the remaining weight. This is the presentation's linear reconstruction divided by the
  number of participating levels. It avoids overflow in the packed target and keeps constant
  image brightness stable when small viewports use fewer levels.
- Separate downsample and reconstruction images avoid sampling a current render attachment.
  The six-level chain uses six downsample targets and five reconstruction targets, rather than
  accumulating additively into the downsample images. All are private backend resources.
- The radius uses source-mip texels. This gives a useful per-level spread control, not a fixed
  full-resolution pixel radius. It is not intended to reproduce an exact pixel-identical CoD preset.
- RGBA16F scene output is bounded to 64000 to avoid infinities in HDR attachments. This is
  a storage safety limit, not the old LDR brightness threshold.

## Controls and compatibility

`postprocess.bloomIntensity` remains available, defaults to 0, and is clamped to 0-1 for rendering.
Zero skips all bloom draws. `postprocess.bloomRadius` now defaults to 1.0 and is clamped to
0.5-3.0 source-mip texels. Non-finite bloom settings receive safe fallbacks.

The old `postprocess.bloomThreshold` field and its editor slider were removed. No current
game script referenced it. Scripts written elsewhere must remove that assignment. Old radius
values should be retuned because their units changed.

The normal Edit viewport resolves HDR without effects. The Render Debugger's Post-Process
Preview includes bloom and other effects. Play mode and exported games use the same final
composite. Game UI, subtitles, and fade overlays draw afterward and cannot feed bloom.
No scene, material, or emissive component format changes are required.

## Resource lifetime

- Descriptor sets are distinct per frame in flight, per pyramid pass, and per resolve.
  The normal Edit viewport and optional preview can both render without rewriting each
  other's descriptors. Settings use push constants rather than a shared writable UBO.
- Every bloom target transitions from shader-read to color-write and back through the existing
  render graph. New targets begin undefined. No pass samples its own destination.
- The chain is created lazily, rebuilt when the active output extent changes, and released at
  shutdown. A device-idle wait protects extent-driven recreation. There is no steady-state wait.
- Odd extents use integer halving, clamped to at least one pixel per dimension. A 1x1 level
  ends the chain. The editor's display/preview targets resize with the viewport.
- The GPU timing panel includes a Bloom scope. The new fragment shaders are picked up by
  CMake's shader glob automatically after reconfiguration, including future added shaders.

## Verification and visual test

The five changed fragment shaders compile with `glslc` and pass `spirv-val --target-env vulkan1.4`.
SPIR-V push-constant offsets match the C++ static assertions. The Debug `OsirisEngine` library
compiles with MSVC. The engine executable has not been run for this change.

`node tools/check_bloom.cjs` reads kernel coordinates and weights from the GLSL and checks
constant HDR fields, dark input, first-pass outlier suppression, normalized linear upsampling,
odd/tiny extents, radius-dependent spread, and bloom-before-tone-map composition. It is a CPU
reference check, not a Vulkan rendering or temporal-stability test.

For the owner's in-engine check:

1. Rebuild the game target and restart it so the changed pipelines and formats reload.
2. Start with Bloom Intensity 0.10 and Bloom Radius 1.0. Compare against intensity 0.
3. Use both a bright window and a small emissive object. Move the camera slowly to inspect
   halo shape and flicker. Increase the radius to spread the glow farther.
4. Compare Play against the Edit Post-Process Preview. The plain Edit viewport should remain
   free of bloom. Check transparent glass and the skybox as well as opaque surfaces.
5. Resize the viewport and game window, toggle preview/bloom, and switch F5 several times.
   Confirm there are no stale frames or validation errors. Check subtitles and fade overlays.

`docs/roadmap.md` is intentionally unchanged because the repository instructions reserve it
for the owner. Its older LDR-bloom description no longer describes this implementation.
