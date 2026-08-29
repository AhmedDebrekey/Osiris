add_executable(Horror
        main.cpp
)

target_link_libraries(Horror PRIVATE
        OsirisEngine
)

target_compile_definitions(Horror PRIVATE
        OSIRIS_GAME_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)
