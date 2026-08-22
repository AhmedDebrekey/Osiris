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
        rhi_vulkan/PipelineManager.cpp
        rhi_vulkan/PipelineManager.h
        renderer/Light.h
        physics/PhysicsTypes.h
        physics/IPhysics.h
        physics/jolt/JoltPhysics.h
        physics/jolt/JoltPhysics.cpp
        editor/SceneInspectorPanel.h
        editor/SceneInspectorPanel.cpp
        audio/AudioTypes.h
        audio/IAudio.h
        audio/openal/OpenALAudio.h
        audio/openal/OpenALAudio.cpp
        assets/AudioLoader.h
        assets/AudioLoader.cpp
        assets/AssetCatalog.h
        assets/AssetCatalog.cpp
        editor/AssetBrowserPanel.h
        editor/AssetBrowserPanel.cpp
        editor/RenderDebugPanel.h
        editor/RenderDebugPanel.cpp
        editor/SceneFileMenu.h
        editor/SceneFileMenu.cpp
        editor/Editor.h
        editor/Editor.cpp
        scripting/ScriptTypes.h
        scripting/IScripting.h
        scripting/ScriptTemplate.h
        scripting/ScriptTemplate.cpp
        scripting/lua/LuaScripting.h
        scripting/lua/LuaScripting.cpp
)

target_include_directories(OsirisEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(OsirisEngine PUBLIC
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        SOL_ALL_SAFETIES_ON=1
)

# sol2's new_usertype<> calls hit MSVC's object-file section limit (C1128) without this.
if (MSVC)
    target_compile_options(OsirisEngine PRIVATE /bigobj)
endif()

target_link_libraries(OsirisEngine PUBLIC
    glm
    SDL2::SDL2
    Vulkan::Vulkan
    VulkanMemoryAllocator
    EnTT::EnTT
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    imgui
    imguizmo
    Jolt
    OpenAL::OpenAL
    liblua
    sol2
    fastgltf
    ufbx
    stb
)
