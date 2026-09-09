---
name: compile-shaders
description: Compile Osiris engine GLSL shaders (assets/shaders/*.vert, *.frag) to SPIR-V .spv files with glslc. Use whenever a shader file is edited, added, or the user asks to compile/build/check a shader — Codex doesn't run engine builds, so this is the way to catch GLSL syntax errors without one.
---

# Compile Shaders

CMake auto-compiles `assets/shaders/*.{vert,frag,comp}` to `.spv` as part of a normal engine
build (`CompileShaders` custom target). Codex never runs that build, so use `glslc`
directly to sanity-check a shader edit instead.

## Steps

1. Figure out which shader(s) to compile:
   - If the user named a file, use that.
   - Otherwise check `git status`/`git diff` for modified or new files under `assets/shaders/*.vert`
     or `*.frag`.
   - If neither yields a clear target, ask which shader to compile — don't compile the whole
     directory speculatively.
2. Run `glslc` from the repo root for each file, one invocation per shader:
   ```bash
   glslc assets/shaders/<name>.vert -o assets/shaders/<name>.vert.spv
   glslc assets/shaders/<name>.frag -o assets/shaders/<name>.frag.spv
   ```
   `glslc` ships with the Vulkan SDK and is on PATH.
3. Report the result plainly: which files compiled cleanly, and the exact `glslc` error text
   (file/line included) for any that didn't — don't paraphrase compiler errors.

This only compiles shaders. It does not build or run the engine — that stays the user's own
workflow unless they ask otherwise.
