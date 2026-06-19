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
