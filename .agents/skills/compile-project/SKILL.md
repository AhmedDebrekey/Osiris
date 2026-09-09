---
name: compile-project
description: Compile the Osiris C++ engine/testbed project (CMake + Ninja + MSVC) to catch build errors — WITHOUT running the resulting executable. Use whenever the user asks to "build", "compile", "check it compiles", "check for build errors", or after making a non-trivial multi-file C++ change and wanting a compile check before handing off. This is the one exception to the "never build or run the engine" rule in AGENTS.md — running the produced .exe is still off-limits, only compiling/linking is allowed.
---

# Compile Project

Runs the actual CMake+Ninja+MSVC build for the whole engine — catches real compiler/linker errors,
not just the shader-level syntax checks `compile-shaders` does. **This only compiles and links.
Never execute the resulting `OsirisEngine.exe`/testbed binary — that stays the owner's own
workflow**, same as before. If asked to "run" or "test" the program, decline and ask the owner to
build+run it themselves in CLion, per AGENTS.md.

## Why this needs vcvars first

`cl.exe` needs `INCLUDE`/`LIB`/`PATH` set up by MSVC's developer environment (standard headers,
CRT, Windows SDK) — CMake bakes the full path to `cl.exe` into `build.ninja` at configure time, but
not those environment variables. Running `ninja`/`cmake --build` from a plain shell without them
first fails with "cannot open source file" errors on totally ordinary headers like `<vector>`.

`cmd.exe`'s `/c` flag gets mangled by Git Bash's path auto-conversion (`/c` silently becomes `C:/`)
— **use the PowerShell tool for this, not Bash.** Even in PowerShell, don't try to inline
`vcvars64.bat && cmake --build ...` as one command — environment variables a `.bat` sets don't
reliably propagate back out to a parent PowerShell process. Instead, write a tiny `.bat` script
that does both steps together (so they share one `cmd.exe` process) and invoke that script.

## Steps

1. Write a build script to the scratchpad directory (adjust the config dir for Release if asked —
   default to Debug, matching what the owner has been testing all along):
   ```batch
   @echo off
   call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
   cmake --build "C:\Dev\Osiris\cmake-build-debug-visual-studio" > "<scratchpad>\build_output.log" 2>&1
   echo BUILD_SCRIPT_DONE exit=%errorlevel% >> "<scratchpad>\build_output.log"
   ```
   Redirecting to a log file matters — a from-scratch or large incremental build produces far more
   Ninja step output than fits in one tool response (30000 char cap), and reading a truncated tail
   makes it look like the build silently stopped when it's actually still going.
2. Run it via the **PowerShell tool**, `run_in_background: true`:
   ```powershell
   & "<scratchpad>\build_debug.bat"
   ```
   A full from-scratch build (all `_deps` — SDL2, Jolt, OpenAL, ImGui, fastgltf, sol2, etc.) takes
   several minutes; an incremental build touching a handful of engine files is much faster. Either
   way, don't block synchronously waiting — background it and let the completion notification (or
   a `Read` on the log file) tell you when it's done, rather than guessing from a truncated stream.
3. Read the log file once the background command finishes (or periodically, via `Read` with an
   `offset` near the end for a still-running build). Look for the trailing
   `BUILD_SCRIPT_DONE exit=N` line — `exit=0` means it linked successfully; nonzero means look
   upward from there for the actual `error C####`/`FAILED:` lines (report those verbatim, same as
   the `compile-shaders` skill does for `glslc` errors — don't paraphrase compiler output).
4. For an engine-library-only change (nothing under `games/testbed/`), `--target OsirisEngine`
   instead of the default target is faster — it skips linking the testbed executable. Use the
   default (no `--target`) whenever `games/testbed/main.cpp` changed too, since that's the only
   thing that exercises the final link step.

## Scope boundary

This compiles and links. It does not run anything, does not touch the Scene Inspector or any
runtime behavior, and does not substitute for the owner actually playing the built program to
verify a feature works — only they can confirm that. Say so plainly when reporting a clean build:
"compiles cleanly" is not the same claim as "works," and AGENTS.md's guidance to not claim feature
success without the owner's own test still applies.
