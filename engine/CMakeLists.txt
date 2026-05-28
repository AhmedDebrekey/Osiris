# ============================================================
#  Osiris Engine Library
# ============================================================

add_library(OsirisEngine STATIC
        Engine.cpp
)

target_include_directories(OsirisEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
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
    lua_static
    sol2
    fastgltf
    ufbx
    stb
)
