cmake_minimum_required(VERSION 3.25)

project(Osiris VERSION 0.1.0 LANGUAGES C CXX)

# ============================================================
#  Global C++ standard
# ============================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
#  Output directories
# ============================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================
#  FetchContent
# ============================================================
include(FetchContent)

# ------------------------------------------------------------
# GLM — Math library
# ------------------------------------------------------------
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
)
FetchContent_MakeAvailable(glm)

# ------------------------------------------------------------
# SDL2 — Windowing and input
# ------------------------------------------------------------
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.3
)
FetchContent_MakeAvailable(SDL2)

# ------------------------------------------------------------
# EnTT — Entity Component System
# ------------------------------------------------------------
FetchContent_Declare(
    EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG        v3.13.2
)
FetchContent_MakeAvailable(EnTT)

# ------------------------------------------------------------
# spdlog — Logging
# ------------------------------------------------------------
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
)
FetchContent_MakeAvailable(spdlog)

# ------------------------------------------------------------
# nlohmann/json — Serialization
# ------------------------------------------------------------
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# ------------------------------------------------------------
# Dear ImGui
# ------------------------------------------------------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC SDL2::SDL2 Vulkan::Vulkan)

# ------------------------------------------------------------
# Vulkan — must be installed on system
# ------------------------------------------------------------
find_package(Vulkan REQUIRED)
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR
        "Vulkan SDK not found. "
        "Please install it from https://vulkan.lunarg.com/sdk/home"
    )
endif()
message(STATUS "Vulkan found: ${Vulkan_VERSION}")

# ------------------------------------------------------------
# Jolt Physics
# ------------------------------------------------------------
FetchContent_Declare(
        JoltPhysics
        GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
        GIT_TAG        v5.1.0
        SOURCE_SUBDIR  Build
)
FetchContent_MakeAvailable(JoltPhysics)

# ------------------------------------------------------------
# Lua
# ------------------------------------------------------------
FetchContent_Declare(
        lua
        GIT_REPOSITORY https://github.com/marovira/lua.git
        GIT_TAG        4737fb9
)
FetchContent_MakeAvailable(lua)

# ------------------------------------------------------------
# Sol3 — Lua C++ bindings
# ------------------------------------------------------------
FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG        v3.3.0
)
FetchContent_MakeAvailable(sol2)

# ------------------------------------------------------------
# fastgltf — glTF 2.0 loading
# ------------------------------------------------------------
FetchContent_Declare(
    fastgltf
    GIT_REPOSITORY https://github.com/spnda/fastgltf.git
    GIT_TAG        v0.7.1
)
FetchContent_MakeAvailable(fastgltf)

# ------------------------------------------------------------
# ufbx — FBX loading
# ------------------------------------------------------------
FetchContent_Declare(
    ufbx
    GIT_REPOSITORY https://github.com/ufbx/ufbx.git
    GIT_TAG        v0.14.3
)
FetchContent_MakeAvailable(ufbx)

add_library(ufbx STATIC ${ufbx_SOURCE_DIR}/ufbx.c)
target_include_directories(ufbx PUBLIC ${ufbx_SOURCE_DIR})

# ------------------------------------------------------------
# stb — Image loading
# ------------------------------------------------------------
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(stb)

add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})

# ------------------------------------------------------------
# VMA — Vulkan Memory Allocator
# ------------------------------------------------------------
FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

# ============================================================
#  Engine library
# ============================================================
add_subdirectory(engine)

# ============================================================
#  Games
# ============================================================
add_subdirectory(games/testbed)