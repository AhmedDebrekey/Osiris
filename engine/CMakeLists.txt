# ============================================================
#  Osiris Engine Library
# ============================================================

add_library(OsirisEngine STATIC
        core/Engine.cpp
        platform/Window.cpp
        core/Log.cpp
        rhi_vulkan/VulkanRHI.cpp
        renderer/MeshType.h
        renderer/Camera.cpp
        renderer/Camera.h
        platform/Input.cpp
        platform/Input.h
        assets/MeshLoader.cpp
        assets/MeshLoader.h
        assets/TextureLoader.cpp
        assets/TextureLoader.h
        core/AssetManager.cpp
        core/AssetManager.h
        scene/Components.h
        scene/Components.cpp
        scene/Scene.cpp
        scene/Scene.h
        scene/Entity.h
        assets/SceneLoader.cpp
        assets/SceneLoader.h
        renderer/Frustum.h
        renderer/Frustum.cpp
        renderer/RenderGraph.cpp
        renderer/RenderGraph.h
)

target_include_directories(OsirisEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(OsirisEngine PUBLIC
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

target_link_libraries(OsirisEngine PUBLIC
    glm
    SDL2::SDL2
    Vulkan::Vulkan
    VulkanMemoryAllocator
    EnTT::EnTT
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    imgui
    Jolt
    liblua
    sol2
    fastgltf
    ufbx
    stb
)
