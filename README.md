# Osiris

A custom 3D game engine written from scratch in C++23 and Vulkan 1.4.

Osiris is a solo hobby project: a full RHI abstraction over a Vulkan backend, a PBR forward
renderer, an EnTT-based ECS, and a growing set of gameplay systems, all built up incrementally
with an eye toward shipping small original games on top of it.

## Features

- **Renderer**: Vulkan 1.4 dynamic rendering (no framebuffers or render passes), a forward
  Cook-Torrance PBR pipeline with ACES filmic tonemapping, cascaded shadow maps for the
  directional light, shadowed spot lights, and full split-sum image-based lighting (irradiance
  and prefiltered specular cubemaps, a BRDF LUT, precomputed once at startup from an
  equirectangular HDR).
- **Scene**: an EnTT-backed entity-component system with a JSON scene format, entity parent/child
  hierarchies, and glTF model loading.
- **Physics**: Jolt Physics, with static/dynamic rigid bodies and a first-person character
  controller.
- **Audio**: OpenAL, with 3D positional sources driven by the ECS.
- **Scripting**: Lua via sol2, with full read/write access to entity components and physics/audio/
  input bound as globals.
- **Editor**: a docked ImGui-based level editor built into the engine itself, with an asset
  browser, transform gizmos, and an entity inspector with drag-and-drop reparenting.

## Building

Prerequisites:

- Windows, Visual Studio 2022 (MSVC), CMake 3.25+, Ninja
- [Vulkan SDK](https://vulkan.lunarg.com/sdk/home) 1.4+

Every other dependency (glm, SDL2, EnTT, Dear ImGui, Jolt, OpenAL, Lua, sol2, and more) is fetched
automatically via CMake `FetchContent`; there's nothing else to install by hand.

```
cmake -B cmake-build-debug-visual-studio -G Ninja
cmake --build cmake-build-debug-visual-studio
```

The `Testbed` target under `games/testbed` is the sample application exercising the engine.

## Roadmap

See [`docs/roadmap.md`](docs/roadmap.md) for the current development phase and what's planned
next.

## License

GPL v3. See [`LICENSE`](LICENSE).
