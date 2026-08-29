add_executable(Testbed
        main.cpp
)

target_link_libraries(Testbed PRIVATE
        OsirisEngine
)

target_compile_definitions(Testbed PRIVATE
        OSIRIS_GAME_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)
